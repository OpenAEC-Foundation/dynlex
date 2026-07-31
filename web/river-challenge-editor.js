import {
  applySemanticTextEdit,
  rebaseLspDiagnosticsAfterLines,
  rebaseSemanticTokensAfterLines,
  renderSemanticTokenLine,
  renderSemanticTokens
} from "./semantic-highlighting.js";
import { semanticTokenLegend } from "./semantic-token-legend.js";

export const RIVER_PROGRAM_PREFIX = "import lib/river_challenge.dl\n\n";
export const RIVER_PROGRAM_PREFIX_LINES = 2;
export const RIVER_STARTER_SOURCE = "# The official names are: sheep, wolf, and hay.\n"
  + "get the hay in the boat\n"
  + "row to the other side";

function sourceLineStarts(source) {
  const starts = [0];
  for (let index = 0; index < source.length; index += 1) {
    if (source[index] === "\n") {
      starts.push(index + 1);
    }
  }
  return starts;
}

function sourceOffset(source, lineStarts, position) {
  if (
    !position
    || !Number.isInteger(position.line)
    || position.line < 0
    || position.line >= lineStarts.length
    || !Number.isInteger(position.character)
    || position.character < 0
  ) {
    throw new Error("LSP range position points outside the river source");
  }
  const start = lineStarts[position.line];
  const end = position.line + 1 < lineStarts.length
    ? lineStarts[position.line + 1] - 1
    : source.length;
  if (position.character > end - start) {
    throw new Error("LSP range column points outside the river source");
  }
  return start + position.character;
}

export function riverSourcePosition(source, offset) {
  if (!Number.isInteger(offset) || offset < 0 || offset > source.length) {
    throw new Error("River source cursor offset is invalid");
  }
  const starts = sourceLineStarts(source);
  let line = starts.length - 1;
  while (line > 0 && starts[line] > offset) {
    line -= 1;
  }
  return {
    line,
    character: offset - starts[line]
  };
}

export function prefixedRiverPosition(source, offset) {
  const position = riverSourcePosition(source, offset);
  return {
    line: position.line + RIVER_PROGRAM_PREFIX_LINES,
    character: position.character
  };
}

export function riverSingleChangedLine(previousSource, source) {
  const previousStarts = sourceLineStarts(previousSource);
  const starts = sourceLineStarts(source);
  if (previousStarts.length !== starts.length) return null;
  let changedLine = null;
  for (let line = 0; line < starts.length; line += 1) {
    const previousEnd = line + 1 < previousStarts.length
      ? previousStarts[line + 1] - 1
      : previousSource.length;
    const end = line + 1 < starts.length ? starts[line + 1] - 1 : source.length;
    if (
      previousSource.slice(previousStarts[line], previousEnd)
      === source.slice(starts[line], end)
    ) {
      continue;
    }
    if (changedLine !== null) return null;
    changedLine = line;
  }
  return changedLine;
}

function sourceRangeOffsets(source, range) {
  const starts = sourceLineStarts(source);
  const start = sourceOffset(source, starts, range.start);
  const end = sourceOffset(source, starts, range.end);
  if (end < start) {
    throw new Error("LSP range ends before it starts");
  }
  return { start, end };
}

function markedSourceFragment(code, source, ranges, dataName, state) {
  const fragment = code.ownerDocument.createDocumentFragment();
  let offset = 0;
  for (const range of ranges) {
    if (range.start > offset) {
      fragment.append(code.ownerDocument.createTextNode(source.slice(offset, range.start)));
    }
    const marker = code.ownerDocument.createElement("span");
    marker.dataset[dataName] = "";
    if (state !== undefined) {
      marker.dataset.riverCallState = state;
    }
    marker.textContent = source.slice(range.start, range.end);
    if (range.start === range.end) {
      marker.classList.add("is-empty");
      marker.textContent = "\u00a0";
    }
    fragment.append(marker);
    offset = range.end;
  }
  if (offset < source.length) {
    fragment.append(code.ownerDocument.createTextNode(source.slice(offset)));
  }
  code.replaceChildren(fragment);
}

function diagnosticSourceRanges(source, diagnostics) {
  const ranges = diagnostics.map((diagnostic) => {
    const offsets = sourceRangeOffsets(source, diagnostic.range);
    let { start, end } = offsets;
    if (start === end) {
      if (end < source.length && source[end] !== "\n") {
        end += 1;
      } else if (start > 0 && source[start - 1] !== "\n") {
        start -= 1;
      }
    }
    return { start, end };
  }).sort((left, right) => left.start - right.start || left.end - right.end);

  const merged = [];
  for (const range of ranges) {
    const previous = merged.at(-1);
    if (previous && range.start <= previous.end) {
      previous.end = Math.max(previous.end, range.end);
    } else {
      merged.push(range);
    }
  }
  return merged;
}

export function renderRiverSource(code, source, tokenData) {
  renderSemanticTokens(code, source, tokenData, semanticTokenLegend, {
    baseClass: "river-token",
    classPrefix: "river-token-"
  });
}

export function applyRiverSourceEdit(code, previousSource, source) {
  applySemanticTextEdit(code, previousSource, source);
}

export function renderRiverSourceLine(code, previousSource, source, tokenData, line) {
  renderSemanticTokenLine(
    code,
    previousSource,
    source,
    tokenData,
    semanticTokenLegend,
    line,
    {
      baseClass: "river-token",
      classPrefix: "river-token-"
    }
  );
}

export function renderRiverDiagnosticRanges(code, source, diagnostics) {
  markedSourceFragment(
    code,
    source,
    diagnosticSourceRanges(source, diagnostics),
    "riverDiagnosticRange"
  );
}

export function renderRiverCallRange(code, source, range, state) {
  const ranges = range ? [sourceRangeOffsets(source, range)] : [];
  markedSourceFragment(code, source, ranges, "riverCallRange", state);
}

export function setRiverLineState(indicator, lineNumber, state) {
  if (!Number.isInteger(lineNumber) || lineNumber < 1) {
    throw new Error("River source line must be a positive integer");
  }
  indicator.hidden = false;
  indicator.dataset.riverSourceLine = String(lineNumber);
  indicator.dataset.riverLineState = state;
  indicator.style.setProperty("--river-source-line-index", String(lineNumber - 1));
}

export function clearRiverLineStates(indicator) {
  indicator.hidden = true;
  delete indicator.dataset.riverSourceLine;
  delete indicator.dataset.riverLineState;
  indicator.style.removeProperty("--river-source-line-index");
}

export function renderRiverCompilerDiagnostics(panel, lineIndicator, diagnostics) {
  panel.replaceChildren();
  clearRiverLineStates(lineIndicator);
  if (diagnostics.length === 0) {
    panel.hidden = true;
    return;
  }
  panel.hidden = false;
  const messages = [];
  for (const diagnostic of diagnostics) {
    const compilerLine = Number.isInteger(diagnostic.line) ? diagnostic.line : 1;
    const sourceLine = compilerLine - RIVER_PROGRAM_PREFIX_LINES;
    if (sourceLine < 1) {
      throw new Error(`River challenge library diagnostic: ${diagnostic.message}`);
    }
    setRiverLineState(lineIndicator, sourceLine, "error");
    const column = Number.isInteger(diagnostic.column) ? diagnostic.column : 1;
    messages.push(`ERROR ${sourceLine}:${column}  ${diagnostic.message}`);
  }
  panel.textContent = messages.join("\n");
}

export function renderRiverLspFeedback({
  diagnosticCode,
  diagnostics,
  lineIndicator,
  panel,
  source,
  sourceCode,
  semanticTokens
}) {
  const sourceTokens = rebaseSemanticTokensAfterLines(
    semanticTokens,
    RIVER_PROGRAM_PREFIX_LINES
  );
  const sourceDiagnostics = rebaseLspDiagnosticsAfterLines(
    diagnostics,
    RIVER_PROGRAM_PREFIX_LINES
  );
  renderRiverSource(sourceCode, source, sourceTokens);
  renderRiverDiagnosticRanges(diagnosticCode, source, sourceDiagnostics);
  panel.replaceChildren();
  if (sourceDiagnostics.length === 0) {
    panel.hidden = true;
    return sourceDiagnostics;
  }

  clearRiverLineStates(lineIndicator);
  const severityLabels = new Map([
    [1, "ERROR"],
    [2, "WARNING"],
    [3, "INFO"],
    [4, "HINT"]
  ]);
  panel.hidden = false;
  panel.textContent = sourceDiagnostics.map((diagnostic) => {
    const line = diagnostic.range.start.line + 1;
    const column = diagnostic.range.start.character + 1;
    setRiverLineState(lineIndicator, line, "error");
    return `${severityLabels.get(diagnostic.severity) ?? "ERROR"} ${line}:${column}  ${diagnostic.message}`;
  }).join("\n");
  return sourceDiagnostics;
}

export function renderRiverLspLineFeedback({
  line,
  previousSource,
  source,
  sourceCode,
  semanticTokens
}) {
  const sourceTokens = rebaseSemanticTokensAfterLines(
    semanticTokens,
    RIVER_PROGRAM_PREFIX_LINES
  );
  renderRiverSourceLine(sourceCode, previousSource, source, sourceTokens, line);
}

function rebaseRiverRange(range) {
  if (
    !range?.start
    || !range?.end
    || range.start.line < RIVER_PROGRAM_PREFIX_LINES
    || range.end.line < RIVER_PROGRAM_PREFIX_LINES
  ) {
    throw new Error("River call expression range is outside the challenge source");
  }
  return {
    start: {
      line: range.start.line - RIVER_PROGRAM_PREFIX_LINES,
      character: range.start.character
    },
    end: {
      line: range.end.line - RIVER_PROGRAM_PREFIX_LINES,
      character: range.end.character
    }
  };
}

export function riverCommandCallRanges(callExpressions, commandCount) {
  if (!Array.isArray(callExpressions) || !Number.isInteger(commandCount) || commandCount < 0) {
    throw new TypeError("River command ranges require compiler call expressions and a command count");
  }
  const sourceCalls = callExpressions
    .filter((call) => (
      call?.returnType === "nothing"
      && typeof call.definition?.uri === "string"
      && new URL(call.definition.uri).pathname.endsWith("/lib/river_challenge.dl")
    ))
    .map((call) => rebaseRiverRange(call.range));
  if (commandCount > 0 && sourceCalls.length === 0) {
    throw new Error("Compiled river program returned no command call expressions");
  }
  return Array.from(
    { length: commandCount },
    (_, index) => sourceCalls[index % sourceCalls.length]
  );
}

function defaultCompletionRange(source, cursorOffset) {
  let start = cursorOffset;
  while (start > 0 && /[\p{L}\p{N}_]/u.test(source[start - 1])) {
    start -= 1;
  }
  return { start, end: cursorOffset };
}

function completionRange(source, cursorOffset, item) {
  if (!item.textEdit?.range) {
    return defaultCompletionRange(source, cursorOffset);
  }
  const range = rebaseRiverRange(item.textEdit.range);
  return sourceRangeOffsets(source, range);
}

const COMPLETION_EDGE = 8;
const COMPLETION_GAP = 8;

function completionGeometryValues({ caret, container, list }) {
  return [
    caret.top,
    caret.right,
    caret.bottom,
    caret.left,
    container.width,
    container.height,
    list.width,
    list.height
  ];
}

export function riverCompletionPlacement({ caret, container, list }) {
  if (
    completionGeometryValues({ caret, container, list })
      .some((value) => !Number.isFinite(value))
    || caret.right < caret.left
    || caret.bottom < caret.top
    || container.width <= COMPLETION_EDGE * 2
    || container.height <= COMPLETION_EDGE * 2
    || list.width <= 0
    || list.height <= 0
    || list.width > container.width - COMPLETION_EDGE * 2
  ) {
    throw new Error("River completion geometry is invalid");
  }

  const rightLeft = caret.right + COMPLETION_GAP;
  const fullHeight = Math.min(list.height, container.height - COMPLETION_EDGE * 2);
  const clampedTop = Math.min(
    Math.max(caret.top, COMPLETION_EDGE),
    container.height - COMPLETION_EDGE - fullHeight
  );
  if (rightLeft + list.width <= container.width - COMPLETION_EDGE) {
    const placement = {
      placement: "right",
      left: rightLeft,
      top: clampedTop
    };
    if (fullHeight < list.height) {
      placement.maxHeight = fullHeight;
    }
    return placement;
  }

  const left = Math.min(
    Math.max(caret.left, COMPLETION_EDGE),
    container.width - COMPLETION_EDGE - list.width
  );
  const belowTop = caret.bottom + COMPLETION_GAP;
  const belowSpace = container.height - COMPLETION_EDGE - belowTop;
  if (list.height <= belowSpace) {
    return {
      placement: "below",
      left,
      top: belowTop
    };
  }

  const aboveSpace = caret.top - COMPLETION_GAP - COMPLETION_EDGE;
  if (list.height <= aboveSpace) {
    return {
      placement: "above",
      left,
      top: caret.top - COMPLETION_GAP - list.height
    };
  }

  const placement = belowSpace >= aboveSpace ? "below" : "above";
  const availableHeight = placement === "below" ? belowSpace : aboveSpace;
  if (availableHeight <= 0) {
    throw new Error("River completion list has no room around the caret");
  }
  return {
    placement,
    left,
    top: placement === "below" ? belowTop : COMPLETION_EDGE,
    maxHeight: availableHeight
  };
}

function riverCaretRect(source, container) {
  const cursorOffset = source.selectionEnd;
  const beforeCursor = source.value.slice(0, cursorOffset);
  const lineStart = beforeCursor.lastIndexOf("\n") + 1;
  let line = 0;
  for (const character of beforeCursor) {
    if (character === "\n") {
      line += 1;
    }
  }

  const style = source.ownerDocument.defaultView.getComputedStyle(source);
  const measure = source.ownerDocument.createElement("span");
  measure.style.position = "absolute";
  measure.style.visibility = "hidden";
  measure.style.whiteSpace = "pre";
  measure.style.font = style.font;
  measure.style.fontVariantLigatures = style.fontVariantLigatures;
  measure.style.letterSpacing = style.letterSpacing;
  measure.style.tabSize = style.tabSize;
  measure.textContent = source.value.slice(lineStart, cursorOffset);
  container.append(measure);
  const lineWidth = measure.getBoundingClientRect().width;
  measure.remove();

  const sourceRect = source.getBoundingClientRect();
  const containerRect = container.getBoundingClientRect();
  const left = sourceRect.left - containerRect.left - container.clientLeft
    + source.clientLeft
    + Number.parseFloat(style.paddingLeft)
    + lineWidth
    - source.scrollLeft;
  const top = sourceRect.top - containerRect.top - container.clientTop
    + source.clientTop
    + Number.parseFloat(style.paddingTop)
    + line * Number.parseFloat(style.lineHeight)
    - source.scrollTop;
  const lineHeight = Number.parseFloat(style.lineHeight);
  return {
    top,
    right: left + 1,
    bottom: top + lineHeight,
    left
  };
}

function positionRiverCompletions(source, list) {
  list.style.removeProperty("max-height");
  list.style.visibility = "hidden";
  list.hidden = false;
  const container = list.offsetParent;
  if (!(container instanceof source.ownerDocument.defaultView.HTMLElement)) {
    throw new Error("River completion list has no HTML positioning container");
  }
  const placement = riverCompletionPlacement({
    caret: riverCaretRect(source, container),
    container: {
      width: container.clientWidth,
      height: container.clientHeight
    },
    list: {
      width: list.getBoundingClientRect().width,
      height: list.getBoundingClientRect().height
    }
  });
  list.style.left = `${placement.left}px`;
  list.style.top = `${placement.top}px`;
  if (placement.maxHeight !== undefined) {
    list.style.maxHeight = `${placement.maxHeight}px`;
  }
  list.dataset.riverCompletionPlacement = placement.placement;
  list.style.removeProperty("visibility");
}

export function createRiverCompletions({ completeDynLex, list, source }) {
  if (typeof completeDynLex !== "function") {
    throw new TypeError("River editor requires DynLex completion");
  }
  let generation = 0;
  let items = [];
  let selectedIndex = 0;

  function close() {
    generation += 1;
    items = [];
    selectedIndex = 0;
    list.hidden = true;
    list.replaceChildren();
    list.style.removeProperty("left");
    list.style.removeProperty("max-height");
    list.style.removeProperty("top");
    list.style.removeProperty("visibility");
    delete list.dataset.riverCompletionPlacement;
    source.removeAttribute("aria-activedescendant");
  }

  function select(index) {
    if (items.length === 0) {
      throw new Error("Cannot select an empty river completion list");
    }
    selectedIndex = (index + items.length) % items.length;
    for (const [itemIndex, button] of [...list.children].entries()) {
      const selected = itemIndex === selectedIndex;
      button.setAttribute("aria-selected", String(selected));
      if (selected) {
        source.setAttribute("aria-activedescendant", button.id);
        button.scrollIntoView({ block: "nearest" });
      }
    }
  }

  function accept(index = selectedIndex) {
    const item = items[index];
    if (!item) {
      throw new Error("River completion selection is missing");
    }
    const range = completionRange(source.value, source.selectionEnd, item);
    const text = item.textEdit?.newText ?? item.insertText ?? item.label;
    if (typeof text !== "string") {
      throw new Error("DynLex completion has no insertion text");
    }
    const previousValue = source.value;
    source.setRangeText(text, range.start, range.end, "end");
    close();
    if (source.value !== previousValue) {
      source.dispatchEvent(new Event("input", { bubbles: true }));
    }
  }

  async function request() {
    if (source.ownerDocument.activeElement !== source || source.selectionStart !== source.selectionEnd) {
      close();
      return;
    }
    const requestGeneration = ++generation;
    const cursorOffset = source.selectionEnd;
    const position = prefixedRiverPosition(source.value, cursorOffset);
    const response = await completeDynLex(
      RIVER_PROGRAM_PREFIX + source.value,
      position
    );
    if (
      requestGeneration !== generation
      || source.ownerDocument.activeElement !== source
      || cursorOffset !== source.selectionEnd
    ) {
      return;
    }
    const responseItems = Array.isArray(response?.items) ? response.items : [];
    items = responseItems;
    list.replaceChildren(...items.map((item, index) => {
      const button = list.ownerDocument.createElement("button");
      button.type = "button";
      button.id = `river-completion-${requestGeneration}-${index}`;
      button.dataset.riverCompletion = "";
      button.setAttribute("role", "option");
      button.setAttribute("aria-selected", "false");
      const label = list.ownerDocument.createElement("strong");
      label.textContent = item.label;
      const detail = list.ownerDocument.createElement("small");
      detail.textContent = item.detail ?? "";
      button.append(label, detail);
      button.addEventListener("mousedown", (event) => {
        event.preventDefault();
        accept(index);
      });
      return button;
    }));
    if (items.length === 0) {
      close();
      return;
    }
    select(0);
    positionRiverCompletions(source, list);
  }

  function handleKeydown(event) {
    if (list.hidden) {
      return false;
    }
    if (event.key === "ArrowDown" || event.key === "ArrowUp") {
      event.preventDefault();
      select(selectedIndex + (event.key === "ArrowDown" ? 1 : -1));
      return true;
    }
    if (event.key === "Tab") {
      event.preventDefault();
      accept();
      return true;
    }
    if (event.key === "Escape") {
      event.preventDefault();
      close();
      return true;
    }
    return false;
  }

  return Object.freeze({ close, handleKeydown, request });
}
