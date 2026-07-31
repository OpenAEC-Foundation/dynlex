function requirePosition(position, lineKey, characterKey) {
  if (
    !position
    || !Number.isInteger(position[lineKey])
    || !Number.isInteger(position[characterKey])
    || position[lineKey] < 0
    || position[characterKey] < 0
  ) {
    throw new TypeError("Expected a valid non-negative position");
  }
}

export function positionToLsp(position) {
  requirePosition(position, "lineNumber", "column");
  if (position.lineNumber === 0 || position.column === 0) {
    throw new TypeError("Monaco positions must be one-based");
  }
  return {
    line: position.lineNumber - 1,
    character: position.column - 1
  };
}

export function positionFromLsp(position) {
  requirePosition(position, "line", "character");
  return {
    lineNumber: position.line + 1,
    column: position.character + 1
  };
}

export function rangeToLsp(range) {
  if (!range) {
    throw new TypeError("Expected a valid Monaco range");
  }
  return {
    start: positionToLsp({
      lineNumber: range.startLineNumber,
      column: range.startColumn
    }),
    end: positionToLsp({
      lineNumber: range.endLineNumber,
      column: range.endColumn
    })
  };
}

export function rangeFromLsp(range) {
  if (!range?.start || !range?.end) {
    throw new TypeError("Expected a valid LSP range");
  }
  const start = positionFromLsp(range.start);
  const end = positionFromLsp(range.end);
  return {
    startLineNumber: start.lineNumber,
    startColumn: start.column,
    endLineNumber: end.lineNumber,
    endColumn: end.column
  };
}

export function completionKindFromLsp(kind, completionKinds) {
  const names = new Map([
    [1, "Text"],
    [2, "Method"],
    [3, "Function"],
    [6, "Variable"],
    [9, "Module"],
    [14, "Keyword"],
    [15, "Snippet"],
    [17, "File"]
  ]);
  return completionKinds[names.get(kind) ?? "Text"];
}

export function symbolKindFromLsp(kind, symbolKinds) {
  const names = new Map([
    [1, "File"],
    [2, "Module"],
    [3, "Namespace"],
    [5, "Class"],
    [12, "Function"],
    [13, "Variable"],
    [25, "Operator"]
  ]);
  return symbolKinds[names.get(kind) ?? "Namespace"];
}

export function diagnosticSeverityFromLsp(severity, markerSeverities) {
  const names = new Map([
    [1, "Error"],
    [2, "Warning"],
    [3, "Info"],
    [4, "Hint"]
  ]);
  return markerSeverities[names.get(severity) ?? "Info"];
}
