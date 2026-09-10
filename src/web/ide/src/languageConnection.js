import { LspSession } from "../../../../web/lsp-client.js";

// A page has one compiler and one language session, shared by all its editors.
export class DynLexConnection {
  constructor(exchange, analysisProfiles = [{ target: "cpu" }]) {
    this.session = new LspSession(exchange);
    this.diagnostics = new Set();
    this.refresh = new Set();
    this.session.onNotification("textDocument/publishDiagnostics", params => {
      for (const listener of this.diagnostics) listener(params);
    });
    this.session.onRequest("workspace/semanticTokens/refresh", () => {
      for (const listener of this.refresh) listener();
      return null;
    });
    this.ready = this.session.start({
      capabilities: {
        textDocument: { semanticTokens: { requests: { full: true } } },
        workspace: { semanticTokens: { refreshSupport: true } }
      },
      initializationOptions: { dynlex: { analysisProfiles } }
    });
  }
  subscribe(diagnostics, refresh) {
    this.diagnostics.add(diagnostics); this.refresh.add(refresh);
    return { dispose: () => { this.diagnostics.delete(diagnostics); this.refresh.delete(refresh); } };
  }
  stop() { return this.session.stop(); }
}
