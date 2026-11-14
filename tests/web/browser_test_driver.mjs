import fs from "node:fs";
import path from "node:path";

const cdpOrigin = process.env.DYNLEX_CDP_ORIGIN || "http://127.0.0.1:9222";
export const siteOrigin = process.env.DYNLEX_SITE_ORIGIN || "http://127.0.0.1:8765";
export const screenshotDirectory = process.env.DYNLEX_SCREENSHOT_DIRECTORY;
export const requestedUrls = [];
export const runtimeExceptions = [];
export const consoleErrors = [];
const consoleMessages = [];

const targets = await fetch(`${cdpOrigin}/json/list`).then((response) => response.json());
const pageTarget = targets.find((target) => target.type === "page");
if (!pageTarget?.webSocketDebuggerUrl) {
  throw new Error("Chrome must expose a page target over CDP");
}

const socket = new WebSocket(pageTarget.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
  socket.addEventListener("open", resolve, { once: true });
  socket.addEventListener("error", reject, { once: true });
});

let nextCommandId = 1;
const pendingCommands = new Map();

socket.addEventListener("message", (event) => {
  const message = JSON.parse(event.data);
  if (typeof message.id === "number") {
    const pending = pendingCommands.get(message.id);
    if (!pending) return;
    pendingCommands.delete(message.id);
    if (message.error) pending.reject(new Error(message.error.message));
    else pending.resolve(message.result);
    return;
  }

  if (message.method === "Network.requestWillBeSent") {
    requestedUrls.push(message.params.request.url);
  } else if (message.method === "Runtime.exceptionThrown") {
    runtimeExceptions.push(message.params.exceptionDetails);
  } else if (message.method === "Runtime.consoleAPICalled") {
    if (message.params.type === "error") consoleErrors.push(message.params.args.map(argument=>argument.value ?? argument.description));
    consoleMessages.push({
      type: message.params.type,
      values: message.params.args.map((argument) => argument.value ?? argument.description ?? argument.type)
    });
  }
});

socket.addEventListener("close", () => {
  for (const pending of pendingCommands.values()) {
    pending.reject(new Error("Chrome CDP session closed"));
  }
  pendingCommands.clear();
});

export function command(method, params = {}) {
  const id = nextCommandId++;
  return new Promise((resolve, reject) => {
    pendingCommands.set(id, { resolve, reject });
    socket.send(JSON.stringify({ id, method, params }));
  });
}

export function onProtocolEvent(method, handler) {
  const listener = event => {
    const message = JSON.parse(event.data);
    if (message.method === method) handler(message.params);
  };
  socket.addEventListener("message", listener);
  return () => socket.removeEventListener("message", listener);
}

export async function evaluate(expression) {
  const response = await command("Runtime.evaluate", {
    expression,
    awaitPromise: true,
    returnByValue: true
  });
  if (response.exceptionDetails) {
    throw new Error(response.exceptionDetails.exception?.description || response.exceptionDetails.text);
  }
  return response.result.value;
}

export async function waitFor(expression, description, timeoutMilliseconds = 120000) {
  const deadline = Date.now() + timeoutMilliseconds;
  while (Date.now() < deadline) {
    if (await evaluate(`Boolean(${expression})`)) return;
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  const pageState = await evaluate(`(() => ({
    url: location.href,
    title: document.title,
    canvasCount: document.querySelectorAll('canvas').length,
    notRestoredReasons: performance.getEntriesByType('navigation')[0]?.notRestoredReasons?.toJSON()
  }))()`);
  throw new Error(
    `Timed out waiting for ${description}\n`
      + `Page state: ${JSON.stringify(pageState)}\n`
      + `Console: ${JSON.stringify(consoleMessages.slice(-10))}\n`
      + `Runtime exceptions: ${JSON.stringify(runtimeExceptions.slice(-10))}`
  );
}

export async function navigate(pagePath) {
  await command("Page.navigate", { url: `${siteOrigin}${pagePath}` });
  await waitFor("document.readyState === 'complete'", `${pagePath} to load`);
}

export async function captureScreenshot(name) {
  if (!screenshotDirectory) return;
  fs.mkdirSync(screenshotDirectory, { recursive: true });
  const response = await command("Page.captureScreenshot", { format: "png" });
  fs.writeFileSync(path.join(screenshotDirectory, `${name}.png`), response.data, "base64");
}

export async function dispatchKey(key, code, virtualKeyCode, modifiers = 0, text = undefined) {
  for (const type of ["keyDown", "keyUp"]) {
    const parameters = {
      type,
      key,
      code,
      windowsVirtualKeyCode: virtualKeyCode,
      nativeVirtualKeyCode: virtualKeyCode,
      modifiers
    };
    if (type === "keyDown" && text !== undefined) {
      parameters.text = text;
      parameters.unmodifiedText = text;
    }
    await command("Input.dispatchKeyEvent", parameters);
  }
}

export async function clickElement(selector) {
  const point = await evaluate(`new Promise((resolve, reject) => {
    const element = document.querySelector(${JSON.stringify(selector)});
    if (!element) {
      reject(new Error(${JSON.stringify(`Element is missing: ${selector}`)}));
      return;
    }
    element.scrollIntoView({ block: "center", inline: "center", behavior: "instant" });
    requestAnimationFrame(() => requestAnimationFrame(() => {
      const bounds = element.getBoundingClientRect();
      const point = {
        x: bounds.left + bounds.width / 2,
        y: bounds.top + bounds.height / 2
      };
      if (
        point.x < 0
        || point.y < 0
        || point.x > window.innerWidth
        || point.y > window.innerHeight
      ) {
        reject(new Error(
          ${JSON.stringify(`Element is outside the viewport: ${selector}`)}
          + " " + JSON.stringify({ bounds, point, viewport: [innerWidth, innerHeight] })
        ));
        return;
      }
      resolve(point);
    }));
  })`);
  await command("Input.dispatchMouseEvent", {
    type: "mousePressed",
    button: "left",
    clickCount: 1,
    x: point.x,
    y: point.y
  });
  await command("Input.dispatchMouseEvent", {
    type: "mouseReleased",
    button: "left",
    clickCount: 1,
    x: point.x,
    y: point.y
  });
}

export function closeBrowserSession() {
  socket.close();
}

await command("Page.enable");
await command("Runtime.enable");
await command("Network.enable");
await command("Browser.grantPermissions", {
  origin: siteOrigin,
  permissions: ["clipboardReadWrite", "clipboardSanitizedWrite"]
});
