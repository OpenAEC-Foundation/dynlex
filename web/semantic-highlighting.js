function sourceLineStarts(sourceText) {
  const starts = [0];
  for (let index = 0; index < sourceText.length; index += 1) {
    if (sourceText[index] === "\n") starts.push(index + 1);
  }
  return starts;
}

export function decodeSemanticTokenRanges(sourceText, tokenData, legend) {
  if (!Array.isArray(tokenData) || tokenData.length % 5 !== 0) {
    throw new Error("Semantic-token data must contain groups of five integers");
  }
  if (!legend || !Array.isArray(legend.tokenTypes)) {
    throw new Error("Semantic-token legend is missing token types");
  }

  const lineStarts = sourceLineStarts(sourceText);

  const ranges = [];
  let line = 0;
  let column = 0;
  let previousEnd = 0;
  for (let index = 0; index < tokenData.length; index += 5) {
    const tuple = tokenData.slice(index, index + 5);
    if (!tuple.every((value) => Number.isInteger(value) && value >= 0)) {
      throw new Error("Semantic-token data contains an invalid integer");
    }
    const [deltaLine, deltaColumn, length, typeIndex, modifierBits] = tuple;
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
    const modifiers = legend.tokenModifiers.filter((_, bit) => modifierBits & (1 << bit));
    ranges.push({ start, end, tokenType, modifiers });
    previousEnd = end;
  }
  return ranges;
}

export function rebaseSemanticTokensAfterLines(tokenData, removedLineCount) {
  if (!Array.isArray(tokenData) || tokenData.length % 5 !== 0) {
    throw new Error("Semantic-token data must contain groups of five integers");
  }
  if (!Number.isInteger(removedLineCount) || removedLineCount < 0) {
    throw new Error("Removed semantic-token line count must be a non-negative integer");
  }

  const rebased = [];
  let sourceLine = 0;
  let sourceColumn = 0;
  let outputLine = 0;
  let outputColumn = 0;
  let hasOutput = false;
  for (let index = 0; index < tokenData.length; index += 5) {
    const tuple = tokenData.slice(index, index + 5);
    if (!tuple.every((value) => Number.isInteger(value) && value >= 0)) {
      throw new Error("Semantic-token data contains an invalid integer");
    }
    const [deltaLine, deltaColumn, length, typeIndex, modifiers] = tuple;
    if (deltaLine === 0) {
      sourceColumn += deltaColumn;
    } else {
      sourceLine += deltaLine;
      sourceColumn = deltaColumn;
    }
    if (sourceLine < removedLineCount) {
      continue;
    }

    const line = sourceLine - removedLineCount;
    const outputDeltaLine = hasOutput ? line - outputLine : line;
    const outputDeltaColumn = outputDeltaLine === 0
      ? sourceColumn - outputColumn
      : sourceColumn;
    if (outputDeltaLine < 0 || outputDeltaColumn < 0) {
      throw new Error("Semantic tokens are not in source order");
    }
    rebased.push(outputDeltaLine, outputDeltaColumn, length, typeIndex, modifiers);
    outputLine = line;
    outputColumn = sourceColumn;
    hasOutput = true;
  }
  return rebased;
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

function semanticTokenFragment(document, sourceText, ranges, start, end, options) {
  const { baseClass = "", classPrefix = "semantic-token-" } = options;
  const fragment = document.createDocumentFragment();
  let offset = start;
  for (const range of ranges) {
    if (range.end <= start) continue;
    if (range.start >= end) break;
    if (range.start < start || range.end > end) {
      throw new Error("Semantic token crosses the rendered source range");
    }
    if (range.start > offset) {
      fragment.append(document.createTextNode(sourceText.slice(offset, range.start)));
    }
    const token = document.createElement("span");
    token.className = [baseClass, semanticTokenClassName(range.tokenType, classPrefix), ...range.modifiers.map(name => name.toLowerCase())]
      .filter(Boolean)
      .join(" ");
    token.textContent = sourceText.slice(range.start, range.end);
    fragment.append(token);
    offset = range.end;
  }
  if (offset < end) {
    fragment.append(document.createTextNode(sourceText.slice(offset, end)));
  }
  return fragment;
}

export function renderSemanticTokens(target, sourceText, tokenData, legend, options = {}) {
  if (!target?.ownerDocument || typeof target.replaceChildren !== "function") {
    throw new Error("Semantic-token target must be a DOM element");
  }
  const ranges = decodeSemanticTokenRanges(sourceText, tokenData, legend);
  target.replaceChildren(semanticTokenFragment(
    target.ownerDocument,
    sourceText,
    ranges,
    0,
    sourceText.length,
    options
  ));
}
