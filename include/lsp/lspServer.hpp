#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <iostream>
#include <sstream>
#include <vector>
#include <memory>

namespace tbx {

// Forward declarations
class Lexer;
class Parser;
class PatternRegistry;

// Represents a position in a document (0-indexed)
struct LspPosition {
    int line{};
    int character{};
};

// Represents a range in a document
struct LspRange {
    LspPosition start;
    LspPosition end;
};

// Represents a diagnostic (error/warning)
struct LspDiagnostic {
    LspRange range;
    int severity{1}; // 1=Error, 2=Warning, 3=Info, 4=Hint
    std::string message;
    std::string source{"3bx"};
};

// Represents an open document
struct TextDocument {
    std::string uri;
    std::string content;
    int version{};
};

// Simple JSON value type for LSP communication
class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    JsonValue() : type_(Type::Null) {}
    JsonValue(bool b) : type_(Type::Bool), boolVal_(b) {}
    JsonValue(int n) : type_(Type::Number), numberVal_(n) {}
    JsonValue(double n) : type_(Type::Number), numberVal_(n) {}
    JsonValue(const char* s) : type_(Type::String), stringVal_(s) {}
    JsonValue(const std::string& s) : type_(Type::String), stringVal_(s) {}
    JsonValue(std::initializer_list<std::pair<std::string, JsonValue>> obj);

    static JsonValue array();
    static JsonValue object();

    // Array operations
    void push(const JsonValue& val);

    // Object operations
    void set(const std::string& key, const JsonValue& val);
    bool has(const std::string& key) const;
    JsonValue& operator[](const std::string& key);
    const JsonValue& operator[](const std::string& key) const;

    // Type queries
    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Value accessors
    bool asBool() const { return boolVal_; }
    double asNumber() const { return numberVal_; }
    int asInt() const { return (int)numberVal_; }
    const std::string& asString() const { return stringVal_; }
    const std::vector<JsonValue>& asArray() const { return arrayVal_; }
    const std::unordered_map<std::string, JsonValue>& asObject() const { return objectVal_; }

    // Serialize to JSON string
    std::string serialize() const;

    // Parse from JSON string
    static JsonValue parse(const std::string& json);

private:
    Type type_;
    bool boolVal_{};
    double numberVal_{};
    std::string stringVal_;
    std::vector<JsonValue> arrayVal_;
    std::unordered_map<std::string, JsonValue> objectVal_;

    static JsonValue parseValue(const std::string& json, size_t& pos);
    static void skipWhitespace(const std::string& json, size_t& pos);
    static std::string parseString(const std::string& json, size_t& pos);
    static double parseNumber(const std::string& json, size_t& pos);
};

// LSP Server implementation
class LspServer {
public:
    LspServer();
    ~LspServer();

    // Run the server (main loop reading from stdin, writing to stdout)
    void run();

    // Process a single message (for testing)
    std::string processMessage(const std::string& message);

    // Enable debug mode (writes logs to stderr)
    void setDebug(bool debug) { debug_ = debug; }

private:
    bool debug_{};
    bool initialized_{};
    bool shutdown_{};
    std::unordered_map<std::string, TextDocument> documents_;
    std::unique_ptr<PatternRegistry> registry_;

    // Message handling
    JsonValue handleRequest(const std::string& method, const JsonValue& params, const JsonValue& id);
    void handleNotification(const std::string& method, const JsonValue& params);

    // LSP method handlers
    JsonValue handleInitialize(const JsonValue& params);
    void handleInitialized(const JsonValue& params);
    void handleShutdown();
    void handleExit();

    // Document sync
    void handleDidOpen(const JsonValue& params);
    void handleDidChange(const JsonValue& params);
    void handleDidClose(const JsonValue& params);

    // Language features
    JsonValue handleCompletion(const JsonValue& params);
    JsonValue handleHover(const JsonValue& params);

    // Diagnostics
    void publishDiagnostics(const std::string& uri, const std::string& content);
    std::vector<LspDiagnostic> getDiagnostics(const std::string& content, const std::string& filename);

    // IO helpers
    std::string readMessage();
    void writeMessage(const std::string& content);
    void sendResponse(const JsonValue& id, const JsonValue& result);
    void sendError(const JsonValue& id, int code, const std::string& message);
    void sendNotification(const std::string& method, const JsonValue& params);

    // Logging
    void log(const std::string& message);

    // Utility
    std::string uriToPath(const std::string& uri);
    std::string pathToUri(const std::string& path);
};

} // namespace tbx
