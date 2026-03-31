import gdb


_PATTERN_ELEMENT_TYPE_NAMES = {
    0: "Other",
    1: "VariableLike",
    2: "Variable",
    3: "Word",
    4: "Choice",
    5: "Count",
}

_MATCHED_ARGUMENT_KIND_NAMES = {
    0: "Expression",
    1: "SubMatch",
    2: "Variable",
    3: "Word",
}


def _strip_type(type_):
    if type_ is None:
        return None
    try:
        type_ = type_.strip_typedefs()
    except gdb.error:
        pass
    if hasattr(type_, "unqualified"):
        try:
            type_ = type_.unqualified()
        except gdb.error:
            pass
    ref_codes = [gdb.TYPE_CODE_REF]
    rvalue_ref_code = getattr(gdb, "TYPE_CODE_RVALUE_REF", None)
    if rvalue_ref_code is not None:
        ref_codes.append(rvalue_ref_code)
    while type_.code in ref_codes:
        type_ = type_.target().strip_typedefs()
        if hasattr(type_, "unqualified"):
            try:
                type_ = type_.unqualified()
            except gdb.error:
                pass
    return type_


def _pointer_is_null(value):
    try:
        return int(value) == 0
    except (TypeError, ValueError, gdb.error):
        return False


def _enum_int(value):
    return int(value)


def _shared_ptr_target(shared_ptr_value):
    ptr = shared_ptr_value["_M_ptr"]
    if _pointer_is_null(ptr):
        return None
    return ptr.dereference()


def _vector_size(vector_value):
    impl = vector_value["_M_impl"]
    return int(impl["_M_finish"] - impl["_M_start"])


def _vector_item(vector_value, index):
    start = vector_value["_M_impl"]["_M_start"]
    return (start + index).dereference()


def _normalize_rendered_string(rendered):
    if len(rendered) >= 2 and rendered[0] == '"' and rendered[-1] == '"':
        return rendered[1:-1]
    return rendered


def _read_std_string(string_value):
    try:
        visualizer = gdb.default_visualizer(string_value)
    except gdb.error:
        visualizer = None
    if visualizer is not None and hasattr(visualizer, "to_string"):
        try:
            rendered = visualizer.to_string()
            if rendered is not None:
                return _normalize_rendered_string(str(rendered))
        except Exception:
            pass
    try:
        length = int(string_value["_M_string_length"])
        return string_value["_M_dataplus"]["_M_p"].string(length=length)
    except gdb.error:
        return _normalize_rendered_string(str(string_value))


def _pattern_element_type_name(type_value):
    return _PATTERN_ELEMENT_TYPE_NAMES.get(_enum_int(type_value), str(type_value))


def _node_text(node_ptr):
    if _pointer_is_null(node_ptr):
        return None
    return _read_std_string(node_ptr.dereference()["text"])


def _pattern_match_signature(match_value):
    nodes = match_value["nodesPassed"]
    node_count = _vector_size(nodes)
    if node_count == 0:
        return "<empty>"
    result = []
    for index in range(node_count):
        node = _vector_item(nodes, index)
        if _enum_int(node["type"]) == 2:
            result.append("$")
        else:
            result.append(_read_std_string(node["text"]))
    return "".join(result)


def _consumed_source_prefix(reference_ptr, element_index, char_index):
    if _pointer_is_null(reference_ptr):
        return ""
    reference = reference_ptr.dereference()
    pattern_elements = reference["patternElements"]
    total_elements = _vector_size(pattern_elements)
    bounded_element_index = min(int(element_index), total_elements)
    result = []
    for index in range(bounded_element_index):
        element = _vector_item(pattern_elements, index)
        result.append(_read_std_string(element["text"]))
    if bounded_element_index < total_elements and int(char_index) > 0:
        current_text = _read_std_string(_vector_item(pattern_elements, bounded_element_index)["text"])
        result.append(current_text[: min(int(char_index), len(current_text))])
    return "".join(result)


def _match_progress_path(progress_value, depth=0):
    if depth > 64:
        return "<recursion limit>"
    current_consumed = _consumed_source_prefix(
        progress_value["patternReference"], progress_value["sourceElementIndex"], progress_value["sourceCharIndex"]
    )
    parent = _shared_ptr_target(progress_value["parent"])
    if parent is None:
        return current_consumed
    parent_rendered = _match_progress_path(parent, depth + 1)
    parent_consumed = _consumed_source_prefix(
        parent["patternReference"], parent["sourceElementIndex"], parent["sourceCharIndex"]
    )
    submatch_consumed = current_consumed
    if current_consumed.startswith(parent_consumed):
        submatch_consumed = current_consumed[len(parent_consumed) :]
    return parent_rendered + "(" + submatch_consumed


class PatternElementPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        return f'PatternElement {_pattern_element_type_name(self.value["type"])} "{_read_std_string(self.value["text"])}"'

    def children(self):
        yield ("type", self.value["type"])
        yield ("text", self.value["text"])
        yield ("startPos", self.value["startPos"])


class PatternTreeNodePrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        type_value = _enum_int(self.value["type"])
        text = _read_std_string(self.value["text"])
        if type_value == 2:
            return 'PatternTreeNode Argument "$"'
        if type_value == 3:
            return "PatternTreeNode WordCapture"
        if type_value == 1:
            return f'PatternTreeNode LiteralWord "{text}"'
        if type_value == 0:
            return f'PatternTreeNode Literal "{text}"'
        return f"PatternTreeNode {_pattern_element_type_name(self.value['type'])}"

    def children(self):
        yield ("kind", self.value["type"])
        yield ("text", self.value["text"])
        yield ("startPos", self.value["startPos"])
        yield ("matchingDefinitions", self.value["matchingDefinitions"])
        yield ("literalChildren", self.value["literalChildren"])
        if not _pointer_is_null(self.value["argumentChild"]):
            yield ("argumentChild", self.value["argumentChild"])
        if not _pointer_is_null(self.value["wordChild"]):
            yield ("wordChild", self.value["wordChild"])
        yield ("parameterNames", self.value["parameterNames"])
        yield ("definitionStartPositions", self.value["definitionStartPositions"])


class VariableMatchPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        state = "bound" if not _pointer_is_null(self.value["variableReference"]) else "unbound"
        return (
            f'VariableMatch "{_read_std_string(self.value["name"])}" '
            f'[{int(self.value["lineStartPos"])},{int(self.value["lineEndPos"])}) {state}'
        )

    def children(self):
        yield ("name", self.value["name"])
        yield ("lineStartPos", self.value["lineStartPos"])
        yield ("lineEndPos", self.value["lineEndPos"])
        if not _pointer_is_null(self.value["variableReference"]):
            yield ("variableReference", self.value["variableReference"])


class WordMatchPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        return (
            f'WordMatch "{_read_std_string(self.value["text"])}" '
            f'[{int(self.value["lineStartPos"])},{int(self.value["lineEndPos"])})'
        )

    def children(self):
        yield ("text", self.value["text"])
        yield ("lineStartPos", self.value["lineStartPos"])
        yield ("lineEndPos", self.value["lineEndPos"])


class AcceptedLiteralMatchPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        text = _node_text(self.value["node"])
        if text is None:
            return "AcceptedLiteral (null)"
        return f'AcceptedLiteral "{text}"'

    def children(self):
        if not _pointer_is_null(self.value["node"]):
            yield ("node", self.value["node"])


class MatchedArgumentPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        argument_index = int(self.value["argumentIndex"])
        kind = _enum_int(self.value["kind"])
        item_index = int(self.value["itemIndex"])
        if kind == 0:
            return f"arg#{argument_index} Expression expr={self.value['expression']}"
        if kind == 1:
            return f"arg#{argument_index} SubMatch subMatches[{item_index}]"
        if kind == 2:
            return f"arg#{argument_index} Variable discoveredVariables[{item_index}]"
        if kind == 3:
            return f"arg#{argument_index} Word discoveredWords[{item_index}]"
        return f"arg#{argument_index} kind={_MATCHED_ARGUMENT_KIND_NAMES.get(kind, kind)} item={item_index}"

    def children(self):
        yield ("argumentIndex", self.value["argumentIndex"])
        yield ("kind", self.value["kind"])
        yield ("itemIndex", self.value["itemIndex"])
        if not _pointer_is_null(self.value["expression"]):
            yield ("expression", self.value["expression"])


class PatternMatchPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        return (
            f'PatternMatch "{_pattern_match_signature(self.value)}" '
            f'line=[{int(self.value["lineStartPos"])},{int(self.value["lineEndPos"])}) '
            f"vars={_vector_size(self.value['discoveredVariables'])} "
            f"words={_vector_size(self.value['discoveredWords'])} "
            f"sub={_vector_size(self.value['subMatches'])} "
            f"args={_vector_size(self.value['orderedArguments'])}"
        )

    def children(self):
        yield ("signature", _pattern_match_signature(self.value))
        if not _pointer_is_null(self.value["matchedEndNode"]):
            yield ("endNode", self.value["matchedEndNode"])
        yield ("lineStartPos", self.value["lineStartPos"])
        yield ("lineEndPos", self.value["lineEndPos"])
        yield ("captures.variables.count", _vector_size(self.value["discoveredVariables"]))
        yield ("captures.words.count", _vector_size(self.value["discoveredWords"]))
        yield ("captures.acceptedLiterals.count", _vector_size(self.value["acceptedLiterals"]))
        yield ("captures.subMatches.count", _vector_size(self.value["subMatches"]))
        yield ("captures.orderedArguments.count", _vector_size(self.value["orderedArguments"]))
        yield ("nodesPassed", self.value["nodesPassed"])
        yield ("discoveredVariables", self.value["discoveredVariables"])
        yield ("discoveredWords", self.value["discoveredWords"])
        yield ("acceptedLiterals", self.value["acceptedLiterals"])
        yield ("orderedArguments", self.value["orderedArguments"])
        yield ("subMatches", self.value["subMatches"])


class PatternReferencePrinter:
    def __init__(self, value):
        self.value = value

    def _pattern_text(self):
        return _read_std_string(self.value["pattern"]["text"])

    def to_string(self):
        match_ptr = self.value["match"]
        pattern_text = self._pattern_text()
        if not _pointer_is_null(match_ptr) and not _pointer_is_null(match_ptr.dereference()["matchedEndNode"]):
            return f'PatternReference matched type={self.value["patternType"]} pattern="{pattern_text}"'
        if int(self.value["resolved"]) != 0:
            return f'PatternReference resolved-no-match type={self.value["patternType"]} pattern="{pattern_text}"'
        return f'PatternReference unresolved type={self.value["patternType"]} pattern="{pattern_text}"'

    def children(self):
        yield ("resolved", self.value["resolved"])
        yield ("patternType", self.value["patternType"])
        yield ("sourceRange", self.value["sourceRange"])
        yield ("patternText", self._pattern_text())
        yield ("patternElements", self.value["patternElements"])
        if not _pointer_is_null(self.value["expression"]):
            yield ("expression", self.value["expression"])
        if not _pointer_is_null(self.value["match"]):
            yield ("match", self.value["match"])


class MatchOptionsPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        return (
            f'MatchOptions acceptLiterals={int(self.value["acceptLiterals"]) != 0} '
            f'maxSteps={int(self.value["maxSteps"])}'
        )

    def children(self):
        yield ("acceptLiterals", self.value["acceptLiterals"])
        yield ("maxSteps", self.value["maxSteps"])


class MatchProgressPrinter:
    def __init__(self, value):
        self.value = value

    def _path(self):
        path = _match_progress_path(self.value)
        return path if path else "<root>"

    def to_string(self):
        if _pointer_is_null(self.value["currentNode"]):
            return "MatchProgress invalid-node"
        status = "complete" if not _pointer_is_null(self.value["match"]["matchedEndNode"]) else "in-progress"
        return f"MatchProgress {status}: {self._path()}"

    def children(self):
        yield ("renderedPath", self._path())
        yield ("type", self.value["type"])
        yield ("currentNode", self.value["currentNode"])
        yield ("rootNode", self.value["rootNode"])
        yield ("patternReference", self.value["patternReference"])
        yield ("match", self.value["match"])
        yield ("parent", self.value["parent"])
        yield ("sourceElementIndex", self.value["sourceElementIndex"])
        yield ("sourceCharIndex", self.value["sourceCharIndex"])
        yield ("patternStartPos", self.value["patternStartPos"])
        yield ("patternPos", self.value["patternPos"])
        yield ("sourceArgumentIndex", self.value["sourceArgumentIndex"])
        yield ("matchedArgumentIndex", self.value["matchedArgumentIndex"])
        yield ("options", self.value["options"])


class DynLexPrinterLookup:
    name = "dynlex"
    enabled = True

    _PRINTERS = {
        "PatternElement": PatternElementPrinter,
        "PatternTreeNode": PatternTreeNodePrinter,
        "VariableMatch": VariableMatchPrinter,
        "WordMatch": WordMatchPrinter,
        "AcceptedLiteralMatch": AcceptedLiteralMatchPrinter,
        "MatchedArgument": MatchedArgumentPrinter,
        "PatternMatch": PatternMatchPrinter,
        "PatternReference": PatternReferencePrinter,
        "MatchOptions": MatchOptionsPrinter,
        "MatchProgress": MatchProgressPrinter,
    }

    def __call__(self, value):
        type_ = _strip_type(value.type)
        if type_ is None:
            return None
        tag = type_.tag or type_.name
        printer = self._PRINTERS.get(tag)
        if printer is None:
            return None
        return printer(value)


_PRINTER_LOOKUP = DynLexPrinterLookup()


def register(objfile=None):
    printers = objfile.pretty_printers if objfile is not None else gdb.pretty_printers
    for printer in printers:
        if getattr(printer, "name", None) == _PRINTER_LOOKUP.name:
            return
    printers.insert(0, _PRINTER_LOOKUP)
