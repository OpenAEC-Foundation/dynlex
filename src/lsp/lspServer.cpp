#include "lsp/lspServer.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "pattern/pattern_registry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <stdexcept>

namespace tbx {

// ============================================================================
// JsonValue implementation
// ============================================================================

JsonValue::JsonValue(std::initializer_list<std::pair<std::string, JsonValue>> obj)
    : type_(Type::Object) {
    for (const auto& pair : obj) {
        objectVal_[pair.first] = pair.second;
    }
}

JsonValue JsonValue::array() {
    JsonValue val;
    val.type_ = Type::Array;
    return val;
}

JsonValue JsonValue::object() {
    JsonValue val;
    val.type_ = Type::Object;
    return val;
}

void JsonValue::push(const JsonValue& val) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        arrayVal_.clear();
    }
    arrayVal_.push_back(val);
}

void JsonValue::set(const std::string& key, const JsonValue& val) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        objectVal_.clear();
    }
    objectVal_[key] = val;
}

bool JsonValue::has(const std::string& key) const {
    return type_ == Type::Object && objectVal_.count(key) > 0;
}

JsonValue& JsonValue::operator[](const std::string& key) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        objectVal_.clear();
    }
    return objectVal_[key];
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
    static JsonValue null;
    if (type_ != Type::Object) return null;
    auto it = objectVal_.find(key);
    return it != objectVal_.end() ? it->second : null;
}

std::string JsonValue::serialize() const {
    std::ostringstream out;
    switch (type_) {
        case Type::Null:
            out << "null";
            break;
        case Type::Bool:
            out << (boolVal_ ? "true" : "false");
            break;
        case Type::Number:
            if (numberVal_ == (int)numberVal_) {
                out << (int)numberVal_;
            } else {
                out << numberVal_;
            }
            break;
        case Type::String: {
            out << '"';
            for (char c : stringVal_) {
                switch (c) {
                    case '"': out << "\\\""; break;
                    case '\\': out << "\\\\"; break;
                    case '\n': out << "\\n"; break;
                    case '\r': out << "\\r"; break;
                    case '\t': out << "\\t"; break;
                    default:
                        if (c >= 0 && c < 32) {
                            out << "\\u" << std::hex << std::setfill('0')
                                << std::setw(4) << (int)(unsigned char)c;
                        } else {
                            out << c;
                        }
                }
            }
            out << '"';
            break;
        }
        case Type::Array: {
            out << '[';
            bool first = true;
            for (const auto& val : arrayVal_) {
                if (!first) out << ',';
                first = false;
                out << val.serialize();
            }
            out << ']';
            break;
        }
        case Type::Object: {
            out << '{';
            bool first = true;
            for (const auto& pair : objectVal_) {
                if (!first) out << ',';
                first = false;
                out << '"' << pair.first << "\":" << pair.second.serialize();
            }
            out << '}';
            break;
        }
    }
    return out.str();
}

void JsonValue::skipWhitespace(const std::string& json, size_t& pos) {
    while (pos < json.size() && std::isspace((unsigned char)json[pos])) {
        pos++;
    }
}

std::string JsonValue::parseString(const std::string& json, size_t& pos) {
    if (json[pos] != '"') {
        throw std::runtime_error("Expected string");
    }
    pos++; // skip opening quote

    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
            switch (json[pos]) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u': {
                    // Parse unicode escape (simplified - just skip)
                    pos += 4;
                    result += '?';
                    break;
                }
                default: result += json[pos];
            }
        } else {
            result += json[pos];
        }
        pos++;
    }

    if (pos >= json.size()) {
        throw std::runtime_error("Unterminated string");
    }
    pos++; // skip closing quote
    return result;
}

double JsonValue::parseNumber(const std::string& json, size_t& pos) {
    size_t start = pos;
    if (json[pos] == '-') pos++;
    while (pos < json.size() && std::isdigit((unsigned char)json[pos])) pos++;
    if (pos < json.size() && json[pos] == '.') {
        pos++;
        while (pos < json.size() && std::isdigit((unsigned char)json[pos])) pos++;
    }
    if (pos < json.size() && (json[pos] == 'e' || json[pos] == 'E')) {
        pos++;
        if (json[pos] == '+' || json[pos] == '-') pos++;
        while (pos < json.size() && std::isdigit((unsigned char)json[pos])) pos++;
    }
    return std::stod(json.substr(start, pos - start));
}

JsonValue JsonValue::parseValue(const std::string& json, size_t& pos) {
    skipWhitespace(json, pos);

    if (pos >= json.size()) {
        return JsonValue();
    }

    char c = json[pos];

    if (c == 'n') {
        pos += 4; // null
        return JsonValue();
    }
    if (c == 't') {
        pos += 4; // true
        return JsonValue(true);
    }
    if (c == 'f') {
        pos += 5; // false
        return JsonValue(false);
    }
    if (c == '"') {
        return JsonValue(parseString(json, pos));
    }
    if (c == '-' || std::isdigit((unsigned char)c)) {
        return JsonValue(parseNumber(json, pos));
    }
    if (c == '[') {
        pos++; // skip [
        JsonValue arr = JsonValue::array();
        skipWhitespace(json, pos);
        if (json[pos] != ']') {
            while (true) {
                arr.push(parseValue(json, pos));
                skipWhitespace(json, pos);
                if (json[pos] == ']') break;
                if (json[pos] != ',') {
                    throw std::runtime_error("Expected ',' or ']' in array");
                }
                pos++; // skip ,
            }
        }
        pos++; // skip ]
        return arr;
    }
    if (c == '{') {
        pos++; // skip {
        JsonValue obj = JsonValue::object();
        skipWhitespace(json, pos);
        if (json[pos] != '}') {
            while (true) {
                skipWhitespace(json, pos);
                std::string key = parseString(json, pos);
                skipWhitespace(json, pos);
                if (json[pos] != ':') {
                    throw std::runtime_error("Expected ':' in object");
                }
                pos++; // skip :
                obj.set(key, parseValue(json, pos));
                skipWhitespace(json, pos);
                if (json[pos] == '}') break;
                if (json[pos] != ',') {
                    throw std::runtime_error("Expected ',' or '}' in object");
                }
                pos++; // skip ,
            }
        }
        pos++; // skip }
        return obj;
    }

    throw std::runtime_error(std::string("Unexpected character: ") + c);
}

JsonValue JsonValue::parse(const std::string& json) {
    size_t pos = 0;
    return parseValue(json, pos);
}

// ============================================================================
// LspServer implementation
// ============================================================================

LspServer::LspServer() : registry_(std::make_unique<PatternRegistry>()) {
    registry_->loadPrimitives();
}

LspServer::~LspServer() = default;

void LspServer::run() {
    log("3BX Language Server starting...");

    while (!shutdown_) {
        try {
            std::string message = readMessage();
            if (message.empty()) {
                break;
            }

            std::string response = processMessage(message);
            if (!response.empty()) {
                writeMessage(response);
            }
        } catch (const std::exception& e) {
            log("Error: " + std::string(e.what()));
        }
    }

    log("3BX Language Server shutting down.");
}

std::string LspServer::readMessage() {
    // Read headers until empty line
    std::string headers;
    int contentLength = -1;

    while (true) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            return ""; // EOF
        }

        // Remove \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            break; // End of headers
        }

        // Parse Content-Length header
        if (line.find("Content-Length:") == 0) {
            contentLength = std::stoi(line.substr(15));
        }
    }

    if (contentLength <= 0) {
        log("Invalid Content-Length");
        return "";
    }

    // Read content
    std::string content(contentLength, '\0');
    std::cin.read(&content[0], contentLength);

    if (std::cin.gcount() != contentLength) {
        log("Failed to read full message content");
        return "";
    }

    log("Received: " + content);
    return content;
}

void LspServer::writeMessage(const std::string& content) {
    log("Sending: " + content);
    std::cout << "Content-Length: " << content.size() << "\r\n\r\n" << content;
    std::cout.flush();
}

std::string LspServer::processMessage(const std::string& message) {
    JsonValue json = JsonValue::parse(message);

    std::string method = json["method"].asString();
    JsonValue params = json["params"];
    JsonValue id = json["id"];

    if (json.has("id")) {
        // This is a request
        JsonValue result = handleRequest(method, params, id);

        JsonValue response = JsonValue::object();
        response.set("jsonrpc", "2.0");
        response.set("id", id);
        response.set("result", result);
        return response.serialize();
    } else {
        // This is a notification
        handleNotification(method, params);
        return "";
    }
}

JsonValue LspServer::handleRequest(const std::string& method, const JsonValue& params, const JsonValue& id) {
    log("Request: " + method);

    if (method == "initialize") {
        return handleInitialize(params);
    }
    if (method == "shutdown") {
        handleShutdown();
        return JsonValue();
    }
    if (method == "textDocument/completion") {
        return handleCompletion(params);
    }
    if (method == "textDocument/hover") {
        return handleHover(params);
    }

    // Unknown method
    log("Unknown request method: " + method);
    return JsonValue();
}

void LspServer::handleNotification(const std::string& method, const JsonValue& params) {
    log("Notification: " + method);

    if (method == "initialized") {
        handleInitialized(params);
    } else if (method == "exit") {
        handleExit();
    } else if (method == "textDocument/didOpen") {
        handleDidOpen(params);
    } else if (method == "textDocument/didChange") {
        handleDidChange(params);
    } else if (method == "textDocument/didClose") {
        handleDidClose(params);
    } else {
        log("Unknown notification method: " + method);
    }
}

JsonValue LspServer::handleInitialize(const JsonValue& params) {
    initialized_ = true;

    // Build capabilities
    JsonValue capabilities = JsonValue::object();

    // Text document sync - incremental updates
    JsonValue textDocSync = JsonValue::object();
    textDocSync.set("openClose", true);
    textDocSync.set("change", 1); // 1 = Full sync, 2 = Incremental
    capabilities.set("textDocumentSync", textDocSync);

    // Completion support
    JsonValue completionProvider = JsonValue::object();
    completionProvider.set("resolveProvider", false);
    JsonValue triggerChars = JsonValue::array();
    triggerChars.push(" ");
    triggerChars.push("@");
    completionProvider.set("triggerCharacters", triggerChars);
    capabilities.set("completionProvider", completionProvider);

    // Hover support
    capabilities.set("hoverProvider", true);

    // Build response
    JsonValue result = JsonValue::object();
    result.set("capabilities", capabilities);

    JsonValue serverInfo = JsonValue::object();
    serverInfo.set("name", "3BX Language Server");
    serverInfo.set("version", "0.1.0");
    result.set("serverInfo", serverInfo);

    return result;
}

void LspServer::handleInitialized(const JsonValue& params) {
    log("Client initialized");
}

void LspServer::handleShutdown() {
    shutdown_ = true;
    log("Shutdown requested");
}

void LspServer::handleExit() {
    std::exit(shutdown_ ? 0 : 1);
}

void LspServer::handleDidOpen(const JsonValue& params) {
    const JsonValue& textDoc = params["textDocument"];
    std::string uri = textDoc["uri"].asString();
    std::string text = textDoc["text"].asString();
    int version = textDoc["version"].asInt();

    TextDocument doc;
    doc.uri = uri;
    doc.content = text;
    doc.version = version;
    documents_[uri] = doc;

    log("Document opened: " + uri);
    publishDiagnostics(uri, text);
}

void LspServer::handleDidChange(const JsonValue& params) {
    const JsonValue& textDoc = params["textDocument"];
    std::string uri = textDoc["uri"].asString();
    int version = textDoc["version"].asInt();

    // For full sync, take the complete new content
    const JsonValue& changes = params["contentChanges"];
    if (changes.isArray() && !changes.asArray().empty()) {
        std::string text = changes.asArray()[0]["text"].asString();

        documents_[uri].content = text;
        documents_[uri].version = version;

        log("Document changed: " + uri);
        publishDiagnostics(uri, text);
    }
}

void LspServer::handleDidClose(const JsonValue& params) {
    const JsonValue& textDoc = params["textDocument"];
    std::string uri = textDoc["uri"].asString();

    documents_.erase(uri);
    log("Document closed: " + uri);

    // Clear diagnostics
    JsonValue diagParams = JsonValue::object();
    diagParams.set("uri", uri);
    diagParams.set("diagnostics", JsonValue::array());
    sendNotification("textDocument/publishDiagnostics", diagParams);
}

JsonValue LspServer::handleCompletion(const JsonValue& params) {
    const JsonValue& textDoc = params["textDocument"];
    std::string uri = textDoc["uri"].asString();
    const JsonValue& position = params["position"];
    int line = position["line"].asInt();
    int character = position["character"].asInt();

    JsonValue items = JsonValue::array();

    // Add reserved words
    std::vector<std::string> keywords = {
        "set", "to", "if", "then", "else", "while", "loop",
        "function", "return", "is", "the", "a", "an", "and", "or", "not",
        "pattern", "syntax", "when", "parsed", "triggered", "priority",
        "import", "use", "from", "class", "expression", "members",
        "created", "new", "of", "with", "by", "each", "member",
        "print", "effect", "get", "patterns", "result"
    };

    for (const auto& kw : keywords) {
        JsonValue item = JsonValue::object();
        item.set("label", kw);
        item.set("kind", 14); // Keyword
        item.set("detail", "keyword");
        items.push(item);
    }

    // Add intrinsics
    std::vector<std::pair<std::string, std::string>> intrinsics = {
        {"@intrinsic(\"store\", var, val)", "Store value in variable"},
        {"@intrinsic(\"load\", var)", "Load value from variable"},
        {"@intrinsic(\"add\", a, b)", "Addition"},
        {"@intrinsic(\"sub\", a, b)", "Subtraction"},
        {"@intrinsic(\"mul\", a, b)", "Multiplication"},
        {"@intrinsic(\"div\", a, b)", "Division"},
        {"@intrinsic(\"print\", val)", "Print to console"}
    };

    for (const auto& intr : intrinsics) {
        JsonValue item = JsonValue::object();
        item.set("label", intr.first);
        item.set("kind", 3); // Function
        item.set("detail", intr.second);
        item.set("insertText", intr.first);
        items.push(item);
    }

    // Add patterns from registry
    for (const auto* pattern : registry_->allPatterns()) {
        std::string label;
        for (const auto& elem : pattern->elements) {
            if (!label.empty()) label += " ";
            if (elem.is_param) {
                label += "<" + elem.value + ">";
            } else {
                label += elem.value;
            }
        }

        JsonValue item = JsonValue::object();
        item.set("label", label);
        item.set("kind", 15); // Snippet
        item.set("detail", "pattern");
        items.push(item);
    }

    return items;
}

JsonValue LspServer::handleHover(const JsonValue& params) {
    const JsonValue& textDoc = params["textDocument"];
    std::string uri = textDoc["uri"].asString();
    const JsonValue& position = params["position"];
    int line = position["line"].asInt();
    int character = position["character"].asInt();

    auto it = documents_.find(uri);
    if (it == documents_.end()) {
        return JsonValue();
    }

    const std::string& content = it->second.content;

    // Find the word at the cursor position
    std::vector<std::string> lines;
    std::istringstream stream(content);
    std::string lineStr;
    while (std::getline(stream, lineStr)) {
        lines.push_back(lineStr);
    }

    if (line < 0 || line >= (int)lines.size()) {
        return JsonValue();
    }

    const std::string& currentLine = lines[line];
    if (character < 0 || character >= (int)currentLine.size()) {
        return JsonValue();
    }

    // Find word boundaries
    int start = character;
    int end = character;
    while (start > 0 && (std::isalnum(currentLine[start - 1]) || currentLine[start - 1] == '_' || currentLine[start - 1] == '@')) {
        start--;
    }
    while (end < (int)currentLine.size() && (std::isalnum(currentLine[end]) || currentLine[end] == '_')) {
        end++;
    }

    std::string word = currentLine.substr(start, end - start);

    if (word.empty()) {
        return JsonValue();
    }

    // Check for intrinsics
    if (word == "@intrinsic" || word.find("@") == 0) {
        JsonValue contents = JsonValue::object();
        contents.set("kind", "markdown");
        contents.set("value",
            "**@intrinsic(name, args...)**\n\n"
            "Calls a built-in operation.\n\n"
            "Available intrinsics:\n"
            "- `store(var, val)` - Store value in variable\n"
            "- `load(var)` - Load value from variable\n"
            "- `add(a, b)` - Addition\n"
            "- `sub(a, b)` - Subtraction\n"
            "- `mul(a, b)` - Multiplication\n"
            "- `div(a, b)` - Division\n"
            "- `print(val)` - Print to console"
        );

        JsonValue result = JsonValue::object();
        result.set("contents", contents);
        return result;
    }

    // Check for keywords
    std::unordered_map<std::string, std::string> keywordDocs = {
        {"pattern", "**pattern:**\n\nDefines a new syntax pattern that can be used in code."},
        {"syntax", "**syntax:**\n\nSpecifies the pattern's syntax template. Reserved words become literals, others become parameters."},
        {"when", "**when triggered/parsed:**\n\nDefines behavior when pattern is triggered (runtime) or parsed (compile-time)."},
        {"triggered", "**when triggered:**\n\nRuntime behavior using intrinsics."},
        {"parsed", "**when parsed:**\n\nCompile-time behavior (optional)."},
        {"set", "**set variable to value**\n\nAssigns a value to a variable."},
        {"if", "**if condition then**\n\nConditional statement."},
        {"function", "**function name(params):**\n\nDefines a function."},
        {"import", "**import module.3bx**\n\nImports patterns from another file."},
        {"class", "**class:**\n\nDefines a class with members and patterns."},
    };

    auto docIt = keywordDocs.find(word);
    if (docIt != keywordDocs.end()) {
        JsonValue contents = JsonValue::object();
        contents.set("kind", "markdown");
        contents.set("value", docIt->second);

        JsonValue result = JsonValue::object();
        result.set("contents", contents);
        return result;
    }

    return JsonValue();
}

void LspServer::publishDiagnostics(const std::string& uri, const std::string& content) {
    std::string path = uriToPath(uri);
    std::vector<LspDiagnostic> diags = getDiagnostics(content, path);

    JsonValue diagnostics = JsonValue::array();
    for (const auto& diag : diags) {
        JsonValue d = JsonValue::object();

        JsonValue range = JsonValue::object();
        JsonValue start = JsonValue::object();
        start.set("line", diag.range.start.line);
        start.set("character", diag.range.start.character);
        JsonValue end = JsonValue::object();
        end.set("line", diag.range.end.line);
        end.set("character", diag.range.end.character);
        range.set("start", start);
        range.set("end", end);

        d.set("range", range);
        d.set("severity", diag.severity);
        d.set("source", diag.source);
        d.set("message", diag.message);

        diagnostics.push(d);
    }

    JsonValue params = JsonValue::object();
    params.set("uri", uri);
    params.set("diagnostics", diagnostics);

    sendNotification("textDocument/publishDiagnostics", params);
}

std::vector<LspDiagnostic> LspServer::getDiagnostics(const std::string& content, const std::string& filename) {
    std::vector<LspDiagnostic> diagnostics;

    try {
        // Use the lexer to tokenize and find errors
        Lexer lexer(content, filename);
        auto tokens = lexer.tokenize();

        // Check for lexer errors
        for (const auto& token : tokens) {
            if (token.type == TokenType::ERROR) {
                LspDiagnostic diag;
                diag.range.start.line = (int)token.location.line - 1;
                diag.range.start.character = (int)token.location.column - 1;
                diag.range.end.line = (int)token.location.line - 1;
                diag.range.end.character = (int)token.location.column + (int)token.lexeme.size() - 1;
                diag.severity = 1; // Error
                diag.message = "Unexpected token: " + token.lexeme;
                diagnostics.push_back(diag);
            }
        }

        // Try to parse and catch syntax errors
        Lexer parserLexer(content, filename);
        Parser parser(parserLexer);

        try {
            auto program = parser.parse();
        } catch (const std::exception& e) {
            // Parse error - try to extract location from message
            std::string msg = e.what();
            LspDiagnostic diag;
            diag.range.start.line = 0;
            diag.range.start.character = 0;
            diag.range.end.line = 0;
            diag.range.end.character = 0;
            diag.severity = 1;
            diag.message = msg;

            // Try to parse line/column from error message
            size_t linePos = msg.find("line ");
            if (linePos != std::string::npos) {
                size_t numStart = linePos + 5;
                size_t numEnd = numStart;
                while (numEnd < msg.size() && std::isdigit(msg[numEnd])) numEnd++;
                if (numEnd > numStart) {
                    diag.range.start.line = std::stoi(msg.substr(numStart, numEnd - numStart)) - 1;
                    diag.range.end.line = diag.range.start.line;
                }
            }

            diagnostics.push_back(diag);
        }
    } catch (const std::exception& e) {
        // General error during analysis
        LspDiagnostic diag;
        diag.range.start.line = 0;
        diag.range.start.character = 0;
        diag.range.end.line = 0;
        diag.range.end.character = 0;
        diag.severity = 1;
        diag.message = std::string("Analysis error: ") + e.what();
        diagnostics.push_back(diag);
    }

    return diagnostics;
}

void LspServer::sendResponse(const JsonValue& id, const JsonValue& result) {
    JsonValue response = JsonValue::object();
    response.set("jsonrpc", "2.0");
    response.set("id", id);
    response.set("result", result);
    writeMessage(response.serialize());
}

void LspServer::sendError(const JsonValue& id, int code, const std::string& message) {
    JsonValue error = JsonValue::object();
    error.set("code", code);
    error.set("message", message);

    JsonValue response = JsonValue::object();
    response.set("jsonrpc", "2.0");
    response.set("id", id);
    response.set("error", error);
    writeMessage(response.serialize());
}

void LspServer::sendNotification(const std::string& method, const JsonValue& params) {
    JsonValue notification = JsonValue::object();
    notification.set("jsonrpc", "2.0");
    notification.set("method", method);
    notification.set("params", params);
    writeMessage(notification.serialize());
}

void LspServer::log(const std::string& message) {
    if (debug_) {
        std::cerr << "[3BX-LSP] " << message << std::endl;
    }
}

std::string LspServer::uriToPath(const std::string& uri) {
    // Simple file:// URI to path conversion
    if (uri.find("file://") == 0) {
        std::string path = uri.substr(7);
        // URL decode (simplified - just handle %20 for spaces)
        std::string decoded;
        for (size_t i = 0; i < path.size(); i++) {
            if (path[i] == '%' && i + 2 < path.size()) {
                int val = std::stoi(path.substr(i + 1, 2), nullptr, 16);
                decoded += (char)val;
                i += 2;
            } else {
                decoded += path[i];
            }
        }
        return decoded;
    }
    return uri;
}

std::string LspServer::pathToUri(const std::string& path) {
    return "file://" + path;
}

} // namespace tbx
