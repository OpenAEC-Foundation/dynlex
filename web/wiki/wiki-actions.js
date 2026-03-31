(() => {
  "use strict";

  function toBase64Url(text) {
    const bytes = new TextEncoder().encode(text);
    let binary = "";
    for (const byte of bytes) {
      binary += String.fromCharCode(byte);
    }
    return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
  }

  function normalizeSnippet(source) {
    return source.replace(/\u00a0/g, " ").replace(/\s+$/g, "");
  }

  function resolveIdeBase() {
    const path = (window.location.pathname || "").replace(/\\/g, "/");
    if (path.includes("/wiki/sections/")) {
      return "../../ide/index.html";
    }
    if (path.includes("/wiki/")) {
      return "../ide/index.html";
    }
    return "ide/index.html";
  }

  function buildIdeUrl(code, autorun) {
    const url = new URL(resolveIdeBase(), window.location.href);
    url.searchParams.set("code64", toBase64Url(code));
    if (autorun) {
      url.searchParams.set("autorun", "1");
    }
    return url.toString();
  }

  function createActionButton(label, href, runVariant) {
    const link = document.createElement("a");
    link.className = runVariant ? "wiki-code-btn wiki-code-btn-run" : "wiki-code-btn";
    link.href = href;
    link.target = "_blank";
    link.rel = "noopener";
    link.textContent = label;
    return link;
  }

  function attachCodeActions() {
    const codeElements = document.querySelectorAll(".code-block pre code");
    for (const codeElement of codeElements) {
      const snippet = normalizeSnippet(codeElement.textContent || "");
      if (snippet.trim().length === 0) {
        continue;
      }

      const container = codeElement.closest(".code-block");
      if (!container || container.dataset.ideActionsAttached === "1") {
        continue;
      }
      container.dataset.ideActionsAttached = "1";

      const actions = document.createElement("div");
      actions.className = "wiki-code-actions";

      actions.appendChild(createActionButton("Run", buildIdeUrl(snippet, true), true));
      actions.appendChild(createActionButton("Open in Editor", buildIdeUrl(snippet, false), false));

      container.prepend(actions);
    }
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", attachCodeActions, { once: true });
  } else {
    attachCodeActions();
  }
})();
