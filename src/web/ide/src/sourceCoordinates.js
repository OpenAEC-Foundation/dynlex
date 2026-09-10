import { rebaseSemanticTokensAfterLines } from "../../../../web/semantic-highlighting.js";

// A game supplies imports before the editable document. Keep all LSP coordinates
// in one place; library locations retain their own, unshifted coordinates.
export class SourceCoordinates {
  constructor(uri, prefix = "") {
    if (prefix && !prefix.endsWith("\n")) throw new Error("An editor prefix must end at a line boundary");
    this.uri = uri;
    this.prefix = prefix;
    this.lines = prefix.split("\n").length - 1;
  }
  text(model) { return (model.uri.toString() === this.uri ? this.prefix : "") + model.getValue(); }
  map(value, direction, uri = this.uri) {
    if (Array.isArray(value)) return value.map(item => this.map(item, direction, uri));
    if (value === null || typeof value !== "object") return value;
    uri = value.uri ?? value.textDocument?.uri ?? uri;
    if (Number.isInteger(value.line) && Number.isInteger(value.character)) {
      return { ...value, line: Math.max(0, value.line + (uri === this.uri ? direction * this.lines : 0)) };
    }
    return Object.fromEntries(Object.entries(value).map(([key, item]) => [key,
      this.map(item, direction, key.startsWith("file:") ? key : uri)]));
  }
  tokens(data) { return rebaseSemanticTokensAfterLines(data, this.lines); }
}
