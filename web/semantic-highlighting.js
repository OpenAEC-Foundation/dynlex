export function semanticLegendsMatch(left, right) {
  if (!left || !right) return false;
  return ["tokenTypes", "tokenModifiers"].every((key) => (
    Array.isArray(left[key])
    && Array.isArray(right[key])
    && left[key].length === right[key].length
    && left[key].every((value, index) => value === right[key][index])
  ));
}

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

export function rebaseLspDiagnosticsAfterLines(diagnostics, removedLineCount) {
  if (!Array.isArray(diagnostics)) {
    throw new TypeError("LSP diagnostics must be an array");
  }
  if (!Number.isInteger(removedLineCount) || removedLineCount < 0) {
    throw new TypeError("Removed diagnostic line count must be a non-negative integer");
  }

  return diagnostics.map((diagnostic) => {
    const start = diagnostic?.range?.start;
    const end = diagnostic?.range?.end;
    const positions = [start?.line, start?.character, end?.line, end?.character];
    if (!positions.every((value) => Number.isInteger(value) && value >= 0)) {
      throw new Error("LSP diagnostic has an invalid range");
    }
    if (
      end.line < start.line
      || (end.line === start.line && end.character < start.character)
    ) {
      throw new Error("LSP diagnostic range ends before it starts");
    }
    if (start.line < removedLineCount || end.line < removedLineCount) {
      throw new Error("LSP diagnostic points into the removed prefix");
    }
    if (typeof diagnostic.message !== "string" || diagnostic.message.length === 0) {
      throw new Error("LSP diagnostic has no message");
    }

    return {
      ...diagnostic,
      range: {
        start: {
          line: start.line - removedLineCount,
          character: start.character
        },
        end: {
          line: end.line - removedLineCount,
          character: end.character
        }
      }
    };
  });
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
    token.className = [baseClass, semanticTokenClassName(range.tokenType, classPrefix)]
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

function sourceLineRanges(sourceText) {
  const starts = sourceLineStarts(sourceText);
  return starts.map((start, line) => ({
    start,
    end: line + 1 < starts.length ? starts[line + 1] - 1 : sourceText.length
  }));
}

function topLevelTextBoundary(target, offset, splitToken = false) {
  let traversed = 0;
  for (const [index, child] of [...target.childNodes].entries()) {
    const length = child.textContent.length;
    if (offset === traversed) return index;
    if (offset === traversed + length) return index + 1;
    if (offset < traversed + length) {
      if (child.nodeType !== 3) {
        if (!splitToken) {
          throw new Error("Semantic token crosses a source-line boundary");
        }
        const tokenOffset = offset - traversed;
        const before = child.cloneNode(false);
        before.textContent = child.textContent.slice(0, tokenOffset);
        const after = child.cloneNode(false);
        after.textContent = child.textContent.slice(tokenOffset);
        child.replaceWith(before, after);
        return index + 1;
      }
      child.splitText(offset - traversed);
      return index + 1;
    }
    traversed += length;
  }
  if (offset !== traversed) {
    throw new Error("Semantic source-line boundary is outside the rendered text");
  }
  return target.childNodes.length;
}

export function applySemanticTextEdit(target, previousSourceText, sourceText) {
  if (!target?.ownerDocument || typeof target.replaceChildren !== "function") {
    throw new Error("Semantic-token target must be a DOM element");
  }
  if (target.textContent !== previousSourceText) {
    throw new Error("Rendered semantic text differs from its previous source");
  }

  let start = 0;
  while (
    start < previousSourceText.length
    && start < sourceText.length
    && previousSourceText[start] === sourceText[start]
  ) {
    start += 1;
  }
  let previousEnd = previousSourceText.length;
  let sourceEnd = sourceText.length;
  while (
    previousEnd > start
    && sourceEnd > start
    && previousSourceText[previousEnd - 1] === sourceText[sourceEnd - 1]
  ) {
    previousEnd -= 1;
    sourceEnd -= 1;
  }
  if (start === previousEnd && start === sourceEnd) return;

  const startBoundary = topLevelTextBoundary(target, start, true);
  const endBoundary = topLevelTextBoundary(target, previousEnd, true);
  const afterEdit = target.childNodes[endBoundary] ?? null;
  for (let index = endBoundary - 1; index >= startBoundary; index -= 1) {
    target.childNodes[index].remove();
  }
  if (sourceEnd > start) {
    target.insertBefore(
      target.ownerDocument.createTextNode(sourceText.slice(start, sourceEnd)),
      afterEdit
    );
  }
  target.normalize();
  if (target.textContent !== sourceText) {
    throw new Error("Incremental semantic edit produced incorrect source text");
  }
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

export function renderSemanticTokenLine(
  target,
  previousSourceText,
  sourceText,
  tokenData,
  legend,
  line,
  options = {}
) {
  if (!target?.ownerDocument || typeof target.replaceChildren !== "function") {
    throw new Error("Semantic-token target must be a DOM element");
  }
  if (target.textContent !== previousSourceText) {
    throw new Error("Rendered semantic text differs from its previous source");
  }
  const previousLines = sourceLineRanges(previousSourceText);
  const sourceLines = sourceLineRanges(sourceText);
  if (!Number.isInteger(line) || line < 0 || line >= sourceLines.length) {
    throw new Error("Semantic source-line index is invalid");
  }
  if (previousLines.length !== sourceLines.length) {
    throw new Error("Incremental semantic rendering cannot change the source line count");
  }
  for (let index = 0; index < sourceLines.length; index += 1) {
    if (index === line) continue;
    const previous = previousLines[index];
    const current = sourceLines[index];
    if (
      previousSourceText.slice(previous.start, previous.end)
      !== sourceText.slice(current.start, current.end)
    ) {
      throw new Error("Incremental semantic rendering changed more than one source line");
    }
  }

  const previousLine = previousLines[line];
  const sourceLine = sourceLines[line];
  const startBoundary = topLevelTextBoundary(target, previousLine.start);
  const endBoundary = topLevelTextBoundary(target, previousLine.end);
  const afterLine = target.childNodes[endBoundary] ?? null;
  for (let index = endBoundary - 1; index >= startBoundary; index -= 1) {
    target.childNodes[index].remove();
  }
  const ranges = decodeSemanticTokenRanges(sourceText, tokenData, legend);
  target.insertBefore(semanticTokenFragment(
    target.ownerDocument,
    sourceText,
    ranges,
    sourceLine.start,
    sourceLine.end,
    options
  ), afterLine);
  if (target.textContent !== sourceText) {
    throw new Error("Incremental semantic rendering produced incorrect source text");
  }
}
