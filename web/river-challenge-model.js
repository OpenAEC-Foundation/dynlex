const SIDES = new Set(["HOME", "FAR"]);
const SUBJECTS = new Set(["SHEEP", "WOLF", "HAY"]);

function fail(message) {
  throw new Error(`Invalid river challenge output: ${message}`);
}

function parseCommand(parts) {
  const action = parts[2];
  const subject = parts[3] ?? null;
  if (action === "CROSS" && subject === null) {
    return { action, subject, events: [] };
  }
  if ((action === "LOAD" || action === "UNLOAD") && SUBJECTS.has(subject) && parts.length === 4) {
    return { action, subject, events: [] };
  }
  fail(`unknown command '${parts.slice(2).join("|")}'`);
}

function parseEvent(parts) {
  const type = parts[1];
  if (type === "ACTION") {
    const action = parts[2];
    const subject = parts[3] ?? null;
    if (action === "CROSS" && SIDES.has(subject) && parts.length === 4) {
      return { type, action, subject };
    }
    if ((action === "LOAD" || action === "UNLOAD") && SUBJECTS.has(subject) && parts.length === 4) {
      return { type, action, subject };
    }
    fail(`unknown action '${parts.slice(2).join("|")}'`);
  }
  if (type === "DANGER" && SUBJECTS.has(parts[2]) && SUBJECTS.has(parts[3]) && parts.length === 4) {
    return { type, predator: parts[2], prey: parts[3] };
  }
  if (type === "ERROR" && parts.length === 3 && parts[2].length > 0) {
    return { type, message: parts[2] };
  }
  if (type === "SUCCESS" && parts.length === 2) {
    return { type };
  }
  fail(`unknown event '${parts.slice(1).join("|")}'`);
}

export function parseRiverTrace(stdout) {
  if (typeof stdout !== "string") {
    fail("program output is not text");
  }

  const commands = [];
  let currentCommand = null;
  let outcome = "running";
  let message = "";
  for (const line of stdout.replace(/\r\n/g, "\n").split("\n").filter(Boolean)) {
    const parts = line.split("|");
    if (parts[0] !== "RIVER") {
      fail(`unexpected line '${line}'`);
    }
    if (parts[1] === "COMMAND") {
      currentCommand = parseCommand(parts);
      commands.push(currentCommand);
      continue;
    }
    if (!currentCommand) {
      fail(`event appeared before a command: '${line}'`);
    }
    const event = parseEvent(parts);
    currentCommand.events.push(event);
    if (event.type === "ERROR") {
      outcome = "failure";
      message = event.message;
    } else if (event.type === "SUCCESS") {
      outcome = "success";
      message = "Everyone made it across.";
    }
  }

  if (commands.length === 0) {
    fail("program produced no commands");
  }
  return { commands, outcome, message };
}

export function createInitialRiverScene() {
  return {
    farmer: "HOME",
    boat: "HOME",
    boatHeading: "FAR",
    cargo: null,
    sheep: "HOME",
    wolf: "HOME",
    hay: "HOME",
    danger: null,
    complete: false
  };
}

function subjectKey(subject) {
  if (!SUBJECTS.has(subject)) {
    throw new Error(`Unknown river subject '${subject}'`);
  }
  return subject.toLowerCase();
}

export function applyRiverEvent(currentScene, event) {
  const scene = {
    ...currentScene,
    danger: currentScene.danger ? { ...currentScene.danger } : null
  };

  if (event.type === "ACTION" && event.action === "LOAD") {
    const key = subjectKey(event.subject);
    if (scene.cargo !== null || scene[key] !== scene.farmer || scene.boat !== scene.farmer) {
      throw new Error(`Inconsistent LOAD event for ${event.subject}`);
    }
    scene.cargo = event.subject;
    scene[key] = "BOAT";
    return scene;
  }

  if (event.type === "ACTION" && event.action === "UNLOAD") {
    const key = subjectKey(event.subject);
    if (scene.cargo !== event.subject || scene[key] !== "BOAT" || scene.boat !== scene.farmer) {
      throw new Error(`Inconsistent UNLOAD event for ${event.subject}`);
    }
    scene.cargo = null;
    scene[key] = scene.farmer;
    return scene;
  }

  if (event.type === "ACTION" && event.action === "CROSS") {
    if (!SIDES.has(event.subject) || scene.boat !== scene.farmer || event.subject === scene.farmer) {
      throw new Error(`Inconsistent CROSS event to ${event.subject}`);
    }
    scene.boat = event.subject;
    scene.farmer = event.subject;
    scene.boatHeading = event.subject;
    return scene;
  }

  if (event.type === "DANGER") {
    const predatorKey = subjectKey(event.predator);
    const preyKey = subjectKey(event.prey);
    if (event.predator === event.prey) {
      throw new Error("A river danger pair must contain two different subjects");
    }
    if (
      scene[predatorKey] === "BOAT"
      || scene[predatorKey] !== scene[preyKey]
      || scene[predatorKey] === scene.farmer
    ) {
      throw new Error(`Inconsistent DANGER event for ${event.predator} and ${event.prey}`);
    }
    scene.danger = { predator: event.predator, prey: event.prey };
    return scene;
  }

  if (event.type === "SUCCESS") {
    if (
      scene.sheep !== "FAR"
      || scene.wolf !== "FAR"
      || scene.hay !== "FAR"
      || scene.farmer !== "FAR"
      || scene.boat !== "FAR"
    ) {
      throw new Error("River SUCCESS arrived before every subject reached the far bank");
    }
    scene.complete = true;
    return scene;
  }

  if (event.type === "ERROR") {
    if (typeof event.message !== "string" || event.message.length === 0) {
      throw new Error("River ERROR event requires a message");
    }
    return scene;
  }

  throw new Error(`Unknown river action '${event.action ?? event.type}'`);
}
