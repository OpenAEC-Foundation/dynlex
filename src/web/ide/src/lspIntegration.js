import { LspSession } from "../../../../web/lsp-client.js";
import {
  completionKindFromLsp,
  diagnosticSeverityFromLsp,
  positionToLsp,
  rangeFromLsp,
  rangeToLsp,
  symbolKindFromLsp
} from "./lspProtocol.js";

const languageSelector = { language: "dynlex", scheme: "file" };
const selectInstantiationCommand = "dynlex.selectInstantiation";

function textDocument(model) {
  return { uri: model.uri.toString() };
}

function markdownContents(contents) {
  if (typeof contents === "string") {
    return [{ value: contents }];
  }
  if (contents && typeof contents.value === "string") {
    return [{ value: contents.value }];
  }
  if (Array.isArray(contents)) {
    return contents.flatMap((content) => markdownContents(content));
  }
  return [];
}

function escapeMarkdown(text) {
  return String(text).replace(/[\\[\]()`]/g, "\\$&");
}

function comparePositions(left, right) {
  if (left.line !== right.line) {
    return left.line - right.line;
  }
  return left.character - right.character;
}

function rangesOverlap(left, right) {
  return comparePositions(left.start, right.end) <= 0 && comparePositions(right.start, left.end) <= 0;
}

function rangeContainsPosition(range, position) {
  return comparePositions(range.start, position) <= 0 && comparePositions(position, range.end) <= 0;
}

function rangeAtPosition(model, position) {
  const word = model.getWordAtPosition(position);
  return {
    startLineNumber: position.lineNumber,
    startColumn: word?.startColumn ?? position.column,
    endLineNumber: position.lineNumber,
    endColumn: word?.endColumn ?? position.column
  };
}

function requireCapabilities(initializeResult) {
  const capabilities = initializeResult?.capabilities;
  if (!capabilities || typeof capabilities !== "object") {
    throw new Error("DynLex language server returned no capabilities");
  }
  return capabilities;
}

export class DynLexLanguageFeatures {
  constructor({
    monaco,
    editor,
    mainModel,
    exchange,
    analysisProfiles,
    onDiagnostics,
    onModelChanged,
    onDocumentsChanged
  }) {
    this.monaco = monaco;
    this.editor = editor;
    this.mainModel = mainModel;
    this.session = new LspSession(exchange);
    this.analysisProfiles = analysisProfiles;
    this.onDiagnostics = onDiagnostics;
    this.onModelChanged = onModelChanged;
    this.onDocumentsChanged = onDocumentsChanged;
    this.capabilities = null;
    this.diagnosticsByUri = new Map();
    this.openDocuments = new Map();
    this.viewStates = new Map();
    this.disposables = [];
    this.semanticTokensChanged = new monaco.Emitter();
    this.disposables.push(this.semanticTokensChanged);
  }

  async start() {
    this.disposables.push(
      this.session.onNotification("textDocument/publishDiagnostics", (params) => {
        this.#publishDiagnostics(params);
      }),
      this.session.onRequest("workspace/semanticTokens/refresh", () => {
        this.semanticTokensChanged.fire();
        return null;
      })
    );

    const initializeResult = await this.session.start({
      capabilities: {
        textDocument: {
          semanticTokens: {
            requests: { full: true }
          }
        },
        workspace: {
          semanticTokens: { refreshSupport: true }
        }
      },
      initializationOptions: {
        dynlex: {
          analysisProfiles: this.analysisProfiles
        }
      }
    });
    this.capabilities = requireCapabilities(initializeResult);
    await this.#openDocument(this.mainModel);
    this.#registerLanguageProviders();
    if (this.capabilities.semanticTokensProvider) {
      this.semanticTokensChanged.fire();
    }
    this.#registerEditorIntegration();
    await this.#sendActiveCursor();
    return this.capabilities;
  }

  async stop() {
    for (const { contentListener } of this.openDocuments.values()) {
      contentListener.dispose();
    }
    await this.session.stop();
    this.openDocuments.clear();
    for (const disposable of this.disposables.splice(0)) {
      disposable.dispose();
    }
  }

  #runDocumentOperation(operation) {
    void operation.catch((error) => {
      console.error("DynLex document synchronization failed", error);
    });
  }

  #request(method, params) {
    return this.session.request(method, params);
  }

  async #openDocument(model) {
    const uri = model.uri.toString();
    if (this.openDocuments.has(uri)) {
      return;
    }
    const document = await this.session.openDocument({
      uri,
      languageId: "dynlex",
      version: model.getVersionId(),
      text: model.getValue()
    });
    const contentListener = model.onDidChangeContent((event) => {
      this.#runDocumentOperation(
        document.applyChanges(
          event.changes.map((change) => ({
            range: rangeToLsp(change.range),
            rangeLength: change.rangeLength,
            text: change.text
          })),
          {
            version: model.getVersionId(),
            text: model.getValue()
          }
        )
      );
    });
    this.openDocuments.set(uri, { model, document, contentListener });
    const diagnostics = this.diagnosticsByUri.get(uri);
    if (diagnostics) {
      this.#setModelMarkers(model, diagnostics);
    }
    this.onDocumentsChanged?.([...this.openDocuments.values()].map((entry) => entry.model));
  }

  async showDocument(uri, selection) {
    const entry = this.openDocuments.get(uri);
    if (!entry) {
      throw new Error(`DynLex document is not open: ${uri}`);
    }
    await this.#activateModel(entry.model, selection);
  }

  async #activateModel(targetModel, selection) {
    const currentModel = this.editor.getModel();
    if (currentModel && currentModel !== targetModel) {
      this.viewStates.set(currentModel.uri.toString(), this.editor.saveViewState());
    }
    if (currentModel !== targetModel) {
      this.editor.setModel(targetModel);
    }

    if (selection?.endLineNumber) {
      this.editor.setSelection(selection);
      this.editor.revealRangeInCenter(selection);
    } else if (selection?.lineNumber) {
      this.editor.setPosition(selection);
      this.editor.revealPositionInCenter(selection);
    } else {
      const viewState = this.viewStates.get(targetModel.uri.toString());
      if (viewState) {
        this.editor.restoreViewState(viewState);
      }
    }
    this.editor.focus();
  }

  #registerLanguageProviders() {
    const capabilities = this.capabilities;
    if (capabilities.completionProvider) {
      this.disposables.push(this.monaco.languages.registerCompletionItemProvider(languageSelector, {
        triggerCharacters: capabilities.completionProvider.triggerCharacters ?? [],
        provideCompletionItems: async (model, position, _context, token) => {
          const response = await this.#request("textDocument/completion", {
            textDocument: textDocument(model),
            position: positionToLsp(position)
          });
          if (token.isCancellationRequested) {
            return { suggestions: [] };
          }
          const items = Array.isArray(response?.items) ? response.items : [];
          const word = model.getWordUntilPosition(position);
          const defaultRange = {
            startLineNumber: position.lineNumber,
            startColumn: word.startColumn,
            endLineNumber: position.lineNumber,
            endColumn: position.column
          };
          return {
            incomplete: response?.isIncomplete === true,
            suggestions: items.map((item) => ({
              label: item.label,
              kind: completionKindFromLsp(item.kind, this.monaco.languages.CompletionItemKind),
              detail: item.detail,
              sortText: item.sortText,
              insertText: item.textEdit?.newText ?? item.insertText ?? item.label,
              range: item.textEdit?.range ? rangeFromLsp(item.textEdit.range) : defaultRange
            }))
          };
        }
      }));
    }

    if (capabilities.definitionProvider) {
      this.disposables.push(this.monaco.languages.registerDefinitionProvider(languageSelector, {
        provideDefinition: async (model, position, token) => {
          const location = await this.#request("textDocument/definition", {
            textDocument: textDocument(model),
            position: positionToLsp(position)
          });
          if (token.isCancellationRequested || !location) {
            return null;
          }
          return {
            uri: this.monaco.Uri.parse(location.uri),
            range: rangeFromLsp(location.range)
          };
        }
      }));
    }

    if (capabilities.hoverProvider) {
      this.disposables.push(
        this.monaco.editor.registerCommand(
          selectInstantiationCommand,
          async (_accessor, selectionKey, instantiationKey) => {
            await this.session.notify("dynlex/selectInstantiation", {
              selectionKey,
              instantiationKey
            });
            this.semanticTokensChanged.fire();
            this.editor.trigger("dynlex", "editor.action.showHover", {});
          }
        ),
        this.monaco.languages.registerHoverProvider(languageSelector, {
          provideHover: async (model, position, token) => {
            const hover = await this.#request("textDocument/hover", {
              textDocument: textDocument(model),
              position: positionToLsp(position)
            });
            if (token.isCancellationRequested) {
              return null;
            }
            const contents = markdownContents(hover?.contents);
            if (contents.length === 0) {
              return null;
            }
            return {
              contents,
              range: hover?.range ? rangeFromLsp(hover.range) : rangeAtPosition(model, position)
            };
          }
        }),
        this.monaco.languages.registerHoverProvider(languageSelector, {
          provideHover: async (model, position, token) => {
            const lspPosition = positionToLsp(position);
            const instantiations = await this.#request(
              "dynlex/instantiationsInDocument",
              textDocument(model)
            );
            if (token.isCancellationRequested) {
              return null;
            }
            const entry = Array.isArray(instantiations)
              ? instantiations.find((candidate) => (
                  candidate?.range && rangeContainsPosition(candidate.range, lspPosition)
                ))
              : undefined;
            if (!entry) {
              return null;
            }
            const options = entry.options.map((option) => {
              const commandArguments = encodeURIComponent(JSON.stringify([
                entry.selectionKey,
                option.key
              ]));
              const prefix = option.key === entry.currentKey ? "current: " : "";
              return `[${escapeMarkdown(prefix + option.label)}]`
                + `(command:${selectInstantiationCommand}?${commandArguments})`;
            });
            return {
              contents: [{
                value: `Choose inferred instance:\n\n${options.join("  \n")}`,
                isTrusted: { enabledCommands: [selectInstantiationCommand] }
              }],
              range: rangeFromLsp(entry.range)
            };
          }
        })
      );
    }

    const semanticTokens = capabilities.semanticTokensProvider;
    if (semanticTokens) {
      const legend = semanticTokens.legend;
      if (!Array.isArray(legend?.tokenTypes) || !Array.isArray(legend?.tokenModifiers)) {
        throw new Error("DynLex language server returned an invalid semantic-token legend");
      }
      this.disposables.push(this.monaco.languages.registerDocumentSemanticTokensProvider(languageSelector, {
        onDidChange: this.semanticTokensChanged.event,
        getLegend: () => legend,
        provideDocumentSemanticTokens: async (model, _lastResultId, token) => {
          const response = await this.#request("textDocument/semanticTokens/full", {
            textDocument: textDocument(model)
          });
          if (token.isCancellationRequested) {
            return { data: new Uint32Array() };
          }
          if (!Array.isArray(response?.data) || response.data.some((value) => !Number.isInteger(value) || value < 0)) {
            throw new Error("DynLex language server returned invalid semantic tokens");
          }
          return {
            data: Uint32Array.from(response.data),
            resultId: String(model.getVersionId())
          };
        },
        releaseDocumentSemanticTokens() {}
      }));
    }

    if (capabilities.documentSymbolProvider) {
      this.disposables.push(this.monaco.languages.registerDocumentSymbolProvider(languageSelector, {
        displayName: "DynLex",
        provideDocumentSymbols: async (model, token) => {
          const symbols = await this.#request("textDocument/documentSymbol", {
            textDocument: textDocument(model)
          });
          if (token.isCancellationRequested) {
            return [];
          }
          return symbols.map((symbol) => this.#documentSymbol(symbol));
        }
      }));
    }

    if (capabilities.codeActionProvider) {
      this.disposables.push(this.monaco.languages.registerCodeActionProvider(languageSelector, {
        provideCodeActions: async (model, range, context, token) => {
          const uri = model.uri.toString();
          const requestedRange = rangeToLsp(range);
          const diagnostics = (this.diagnosticsByUri.get(uri) ?? [])
            .filter((diagnostic) => rangesOverlap(diagnostic.range, requestedRange));
          const actions = await this.#request("textDocument/codeAction", {
            textDocument: textDocument(model),
            range: requestedRange,
            context: {
              diagnostics,
              ...(context.only ? { only: [context.only] } : {})
            }
          });
          if (token.isCancellationRequested) {
            return { actions: [], dispose() {} };
          }
          return {
            actions: await Promise.all(actions.map(async (action) => ({
              title: action.title,
              kind: action.kind,
              diagnostics: context.markers,
              edit: action.edit ? await this.#workspaceEdit(action.edit) : undefined
            }))),
            dispose() {}
          };
        }
      }, {
        providedCodeActionKinds: ["quickfix"]
      }));
    }
  }

  #registerEditorIntegration() {
    this.disposables.push(
      this.monaco.editor.registerEditorOpener({
        openCodeEditor: async (sourceEditor, resource, selection) => {
          let targetModel = this.monaco.editor.getModel(resource);
          let needsDidOpen = false;
          if (!targetModel) {
            const source = await this.#request("dynlex/readDocument", {
              uri: resource.toString()
            });
            if (typeof source !== "string") {
              return false;
            }
            targetModel = this.monaco.editor.createModel(source, "dynlex", resource);
            needsDidOpen = true;
          }
          if (needsDidOpen) {
            await this.#openDocument(targetModel);
          }
          await this.#activateModel(targetModel, selection);
          return true;
        }
      }),
      this.editor.onDidChangeModel(() => {
        const model = this.editor.getModel();
        this.editor.updateOptions({ readOnly: model !== this.mainModel });
        this.onModelChanged?.(model);
        this.#runDocumentOperation(this.#sendActiveCursor());
      }),
      this.editor.onDidChangeCursorPosition(() => {
        this.#runDocumentOperation(this.#sendActiveCursor());
      })
    );
  }

  async #sendActiveCursor() {
    const model = this.editor.getModel();
    const position = this.editor.getPosition();
    if (!model || !position || model.getLanguageId() !== "dynlex") {
      await this.session.clearActiveCursor();
      return;
    }
    const entry = this.openDocuments.get(model.uri.toString());
    if (!entry) {
      throw new Error(`Active DynLex document is not open: ${model.uri.toString()}`);
    }
    await entry.document.setActiveCursor(positionToLsp(position));
  }

  #publishDiagnostics(params) {
    if (typeof params?.uri !== "string" || !Array.isArray(params.diagnostics)) {
      throw new Error("DynLex language server published invalid diagnostics");
    }
    this.diagnosticsByUri.set(params.uri, params.diagnostics);
    const model = this.monaco.editor.getModel(this.monaco.Uri.parse(params.uri));
    if (model) {
      this.#setModelMarkers(model, params.diagnostics);
    }
    this.onDiagnostics?.(params.uri, params.diagnostics);
  }

  #setModelMarkers(model, diagnostics) {
    const markers = diagnostics.map((diagnostic) => ({
      ...rangeFromLsp(diagnostic.range),
      severity: diagnosticSeverityFromLsp(diagnostic.severity, this.monaco.MarkerSeverity),
      message: diagnostic.message,
      source: diagnostic.source ?? "dynlex"
    }));
    this.monaco.editor.setModelMarkers(model, "dynlex-lsp", markers);
  }

  #documentSymbol(symbol) {
    return {
      name: symbol.name,
      detail: symbol.detail ?? "",
      kind: symbolKindFromLsp(symbol.kind, this.monaco.languages.SymbolKind),
      tags: [],
      range: rangeFromLsp(symbol.range),
      selectionRange: rangeFromLsp(symbol.selectionRange),
      children: Array.isArray(symbol.children)
        ? symbol.children.map((child) => this.#documentSymbol(child))
        : undefined
    };
  }

  async #workspaceEdit(edit) {
    const edits = [];
    for (const [uri, textEdits] of Object.entries(edit.changes ?? {})) {
      const resource = this.monaco.Uri.parse(uri);
      let model = this.monaco.editor.getModel(resource);
      if (!model) {
        const source = await this.#request("dynlex/readDocument", { uri });
        if (typeof source !== "string") {
          throw new Error(`DynLex language server returned an edit for unavailable document ${uri}`);
        }
        model = this.monaco.editor.createModel(source, "dynlex", resource);
        await this.#openDocument(model);
      }
      for (const textEdit of textEdits) {
        edits.push({
          resource,
          versionId: model.getVersionId(),
          textEdit: {
            range: rangeFromLsp(textEdit.range),
            text: textEdit.newText
          }
        });
      }
    }
    return { edits };
  }
}
