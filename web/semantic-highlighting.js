export function semanticLegendsMatch(left, right) {
  if (!left || !right) return false;
  return ["tokenTypes", "tokenModifiers"].every((key) => (
    Array.isArray(left[key])
    && Array.isArray(right[key])
    && left[key].length === right[key].length
    && left[key].every((value, index) => value === right[key][index])
  ));
}

export function decodeSemanticTokenRanges(sourceText, tokenData, legend) {
  if (!Array.isArray(tokenData) || tokenData.length % 5 !== 0) {
    throw new Error("Semantic-token data must contain groups of five integers");
  }
  if (!legend || !Array.isArray(legend.tokenTypes)) {
    throw new Error("Semantic-token legend is missing token types");
  }

  const lineStarts = [0];
  for (let index = 0; index < sourceText.length; index += 1) {
    if (sourceText[index] === "\n") lineStarts.push(index + 1);
  }

  const ranges = [];
  let line = 0;
  let column = 0;
  let previousEnd = 0;
  for (let index = 0; index < tokenData.length; index += 5) {
    const tuple = tokenData.slice(index, index + 5);
    if (!tuple.every((value) => Number.isInteger(value) && value >= 0)) {
      throw new Error("Semantic-token data contains an invalid integer");
    }
    const [deltaLine, deltaColumn, length, typeIndex] = tuple;
    if (deltaLine === 0) {
      column += deltaColumn;
    } else {
      line += deltaLine;
      column = deltaColumn;
    }
    if (line >= lineStarts.length || length === 0) {
      throw new Error("Semantic token points outside the source");
    }

    const lineEnd = line + 1 < lineStarts.length ? lineStarts[line + 1] - 1 : sourceText.length;
    const start = lineStarts[line] + column;
    const end = start + length;
    const tokenType = legend.tokenTypes[typeIndex];
    if (typeof tokenType !== "string" || tokenType.length === 0 || end > lineEnd || start < previousEnd) {
      throw new Error("Semantic token has an invalid range or type");
    }
    ranges.push({ start, end, tokenType });
    previousEnd = end;
  }
  return ranges;
}

export function semanticTokenClassName(tokenType, prefix) {
  if (typeof prefix !== "string" || prefix.length === 0) {
    throw new Error("Semantic token class prefix is required");
  }
  const suffix = tokenType.replace(/[^a-z0-9_-]/gi, "-").toLowerCase();
  if (suffix.length === 0) {
    throw new Error("Semantic token type cannot produce a CSS class");
  }
  return `${prefix}${suffix}`;
}

export function renderSemanticTokens(target, sourceText, tokenData, legend, options = {}) {
  if (!target?.ownerDocument || typeof target.replaceChildren !== "function") {
    throw new Error("Semantic-token target must be a DOM element");
  }
  const { baseClass = "", classPrefix = "semantic-token-" } = options;
  const fragment = target.ownerDocument.createDocumentFragment();
  let offset = 0;
  for (const range of decodeSemanticTokenRanges(sourceText, tokenData, legend)) {
    if (range.start > offset) {
      fragment.append(target.ownerDocument.createTextNode(sourceText.slice(offset, range.start)));
    }
    const token = target.ownerDocument.createElement("span");
    token.className = [baseClass, semanticTokenClassName(range.tokenType, classPrefix)]
      .filter(Boolean)
      .join(" ");
    token.textContent = sourceText.slice(range.start, range.end);
    fragment.append(token);
    offset = range.end;
  }
  if (offset < sourceText.length) {
    fragment.append(target.ownerDocument.createTextNode(sourceText.slice(offset)));
  }
  target.replaceChildren(fragment);
}
