#ifdef DYNLEX_WEB

#include "codegen/codegen.h"
#include "compiler/compiler.h"
#include "lsp/dynlexServer.h"
#include "lsp/fileSystem.h"
#include "parseContext.h"
#include "pathUtils.h"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <spirv_cross.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if __has_include(<emscripten/emscripten.h>)
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

namespace {

constexpr const char *kMainSourcePath = "/workspace/main.dl";
constexpr const char *kOutputWasmPath = "/tmp/main.wasm";
constexpr const char *kOutputSpirvPath = "/tmp/main.spv";

enum WebCompileStatus {
	WebCompileStatusOk = 0,
	WebCompileStatusCompileFailed = 1,
	WebCompileStatusCodegenFailed = 2,
	WebCompileStatusInternalError = 3,
	WebCompileStatusNotInitialized = 4,
};

enum class WebOutputKind {
	ProgramWasm,
	ShaderSpirv,
};

struct CompilerLogEntry {
	std::string level;
	std::string message;
};

class WebMessageTransport final : public lsp::Transport {
  public:
	lsp::TransferSize read(char * /*buffer*/, std::size_t /*count*/) override { return 0; }

	lsp::TransferSize write(const char *buffer, std::size_t count) override {
		if (!connected)
			return -1;
		pending.append(buffer, count);
		extractMessages();
		return static_cast<lsp::TransferSize>(count);
	}

	bool isConnected() const override { return connected; }

	void close() override { connected = false; }

	Json takeMessages() {
		Json result = Json::array();
		for (Json &message : messages)
			result.push_back(std::move(message));
		messages.clear();
		return result;
	}

  private:
	bool connected = true;
	std::string pending;
	std::vector<Json> messages;

	void extractMessages() {
		while (true) {
			size_t headerEnd = pending.find("\r\n\r\n");
			size_t delimiterLength = 4;
			if (headerEnd == std::string::npos) {
				headerEnd = pending.find("\n\n");
				delimiterLength = 2;
			}
			if (headerEnd == std::string::npos)
				return;

			constexpr std::string_view contentLengthHeader = "Content-Length:";
			std::string_view headers(pending.data(), headerEnd);
			size_t lengthPosition = headers.find(contentLengthHeader);
			if (lengthPosition == std::string_view::npos)
				throw std::logic_error("web LSP transport received a message without Content-Length");
			lengthPosition += contentLengthHeader.size();
			while (lengthPosition < headers.size() && headers[lengthPosition] == ' ')
				lengthPosition++;
			size_t lengthEnd = headers.find_first_of("\r\n", lengthPosition);
			if (lengthEnd == std::string_view::npos)
				lengthEnd = headers.size();

			size_t contentLength = 0;
			const char *lengthBegin = headers.data() + lengthPosition;
			const char *lengthFinish = headers.data() + lengthEnd;
			auto [parsedEnd, error] = std::from_chars(lengthBegin, lengthFinish, contentLength);
			if (error != std::errc{} || parsedEnd != lengthFinish || contentLength == 0)
				throw std::logic_error("web LSP transport received an invalid Content-Length");

			size_t bodyStart = headerEnd + delimiterLength;
			if (pending.size() - bodyStart < contentLength)
				return;
			messages.push_back(Json::parse(pending.substr(bodyStart, contentLength)));
			pending.erase(0, bodyStart + contentLength);
		}
	}
};

class WebLspServer final : public lsp::DynLexServer {
  public:
	WebLspServer() : WebLspServer(new WebMessageTransport()) {}

	Json exchange(const Json &message) {
		processMessage(message);
		return webTransport->takeMessages();
	}

  private:
	explicit WebLspServer(WebMessageTransport *transport)
		: lsp::DynLexServer(std::unique_ptr<lsp::Transport>(transport)), webTransport(transport) {}

	WebMessageTransport *webTransport;
};

struct WebCompilerState {
	bool initialized = false;
	std::string mainSource;
	std::vector<uint8_t> outputWasm;
	std::string outputWasmBase64;
	std::vector<uint8_t> outputShaderSpirv;
	std::string outputShaderSpirvBase64;
	std::string shaderUniformsJson = R"({"uniforms":[]})";
	std::vector<CompilerLogEntry> compilerLog;
	std::string diagnosticsJson = R"({"diagnostics":[]})";
	std::string compilerLogJson = R"({"messages":[]})";
	std::string lspExchangeJson = "[]";
	std::unique_ptr<WebLspServer> lspServer;
};

WebCompilerState &webState() {
	static WebCompilerState state;
	return state;
}

void appendCompilerLog(std::string level, std::string message) {
	webState().compilerLog.push_back({std::move(level), std::move(message)});
}

void flushCompilerLogJson() {
	nlohmann::json messages = nlohmann::json::array();
	for (const CompilerLogEntry &entry : webState().compilerLog) {
		nlohmann::json message;
		message["level"] = entry.level;
		message["message"] = entry.message;
		messages.push_back(std::move(message));
	}
	nlohmann::json root;
	root["messages"] = std::move(messages);
	webState().compilerLogJson = root.dump();
}

std::string diagnosticSeverity(Diagnostic::Level level) {
	switch (level) {
	case Diagnostic::Level::Info:
		return "info";
	case Diagnostic::Level::Warning:
		return "warning";
	case Diagnostic::Level::Error:
		return "error";
	}
	return "error";
}

nlohmann::json sourceLocationToJson(const SourceLocation &location) {
	if (!location.sourceFile)
		return nullptr;
	nlohmann::json result;
	result["file"] = location.sourceFile->uri;
	result["line"] = location.sourceFileLineIndex + 1;
	result["column"] = location.column + 1;
	return result;
}

nlohmann::json rangeToJson(Range range) {
	if (!range.line)
		return nullptr;
	SourceLocation start = range.sourceStart();
	SourceLocation end = range.sourceEnd();
	if (!start.sourceFile || !end.sourceFile)
		return nullptr;
	nlohmann::json result;
	result["start"] = sourceLocationToJson(start);
	result["end"] = sourceLocationToJson(end);
	return result;
}

void flushDiagnosticsJson(const std::vector<Diagnostic> &diagnostics) {
	nlohmann::json entries = nlohmann::json::array();
	for (const Diagnostic &diagnostic : diagnostics) {
		SourceLocation start = diagnostic.range.sourceStart();
		nlohmann::json entry;
		entry["severity"] = diagnosticSeverity(diagnostic.level);
		entry["message"] = diagnostic.message;
		entry["file"] = start.sourceFile ? nlohmann::json(start.sourceFile->uri) : nlohmann::json(nullptr);
		entry["line"] = start.sourceFile ? nlohmann::json(start.sourceFileLineIndex + 1) : nlohmann::json(nullptr);
		entry["column"] = start.sourceFile ? nlohmann::json(start.column + 1) : nlohmann::json(nullptr);
		entry["range"] = rangeToJson(diagnostic.range);

		nlohmann::json related = nlohmann::json::array();
		for (const RelatedInfo &relatedInfo : diagnostic.relatedInfo) {
			SourceLocation relatedStart = relatedInfo.range.sourceStart();
			nlohmann::json relatedEntry;
			relatedEntry["message"] = relatedInfo.message;
			relatedEntry["file"] =
				relatedStart.sourceFile ? nlohmann::json(relatedStart.sourceFile->uri) : nlohmann::json(nullptr);
			relatedEntry["line"] =
				relatedStart.sourceFile ? nlohmann::json(relatedStart.sourceFileLineIndex + 1) : nlohmann::json(nullptr);
			relatedEntry["column"] =
				relatedStart.sourceFile ? nlohmann::json(relatedStart.column + 1) : nlohmann::json(nullptr);
			relatedEntry["range"] = rangeToJson(relatedInfo.range);
			related.push_back(std::move(relatedEntry));
		}
		entry["related"] = std::move(related);
		entries.push_back(std::move(entry));
	}
	nlohmann::json root;
	root["diagnostics"] = std::move(entries);
	webState().diagnosticsJson = root.dump();
}

bool readBinaryFile(const std::string &path, std::vector<uint8_t> &bytes, std::string &errorMessage) {
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input) {
		errorMessage = "cannot open file '" + path + "'";
		return false;
	}
	std::ifstream::pos_type size = input.tellg();
	if (size < 0) {
		errorMessage = "cannot determine file size for '" + path + "'";
		return false;
	}
	input.seekg(0, std::ios::beg);
	bytes.resize(static_cast<size_t>(size));
	if (!bytes.empty())
		input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!input.good()) {
		errorMessage = "cannot read file '" + path + "'";
		bytes.clear();
		return false;
	}
	return true;
}

std::string encodeBase64(const std::vector<uint8_t> &bytes) {
	static constexpr char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	if (bytes.empty())
		return {};

	std::string encoded;
	encoded.reserve(((bytes.size() + 2) / 3) * 4);

	size_t index = 0;
	while (index + 2 < bytes.size()) {
		uint32_t chunk = (static_cast<uint32_t>(bytes[index]) << 16) | (static_cast<uint32_t>(bytes[index + 1]) << 8) |
						 static_cast<uint32_t>(bytes[index + 2]);
		encoded.push_back(kBase64Alphabet[(chunk >> 18) & 0x3f]);
		encoded.push_back(kBase64Alphabet[(chunk >> 12) & 0x3f]);
		encoded.push_back(kBase64Alphabet[(chunk >> 6) & 0x3f]);
		encoded.push_back(kBase64Alphabet[chunk & 0x3f]);
		index += 3;
	}

	size_t remaining = bytes.size() - index;
	if (remaining == 1) {
		uint32_t chunk = static_cast<uint32_t>(bytes[index]) << 16;
		encoded.push_back(kBase64Alphabet[(chunk >> 18) & 0x3f]);
		encoded.push_back(kBase64Alphabet[(chunk >> 12) & 0x3f]);
		encoded.push_back('=');
		encoded.push_back('=');
	} else if (remaining == 2) {
		uint32_t chunk = (static_cast<uint32_t>(bytes[index]) << 16) | (static_cast<uint32_t>(bytes[index + 1]) << 8);
		encoded.push_back(kBase64Alphabet[(chunk >> 18) & 0x3f]);
		encoded.push_back(kBase64Alphabet[(chunk >> 12) & 0x3f]);
		encoded.push_back(kBase64Alphabet[(chunk >> 6) & 0x3f]);
		encoded.push_back('=');
	}

	return encoded;
}

bool reflectShaderSpirv(const std::vector<uint8_t> &spirvBytes, std::string &uniformsJson, std::string &errorMessage) {
	if (spirvBytes.size() < 20 || spirvBytes.size() % sizeof(uint32_t) != 0) {
		errorMessage = "emitted SPIR-V is not a complete word stream";
		return false;
	}

	std::vector<uint32_t> spirvWords(spirvBytes.size() / sizeof(uint32_t));
	std::memcpy(spirvWords.data(), spirvBytes.data(), spirvBytes.size());

	try {
		spirv_cross::Compiler compiler(std::move(spirvWords));
		spirv_cross::ShaderResources resources = compiler.get_shader_resources();

		nlohmann::json uniforms = nlohmann::json::array();
		for (const spirv_cross::Resource &uniform : resources.uniform_buffers) {
			std::string spirvName = compiler.get_name(uniform.id);
			constexpr std::string_view prefix = "ubo_";
			if (!spirvName.starts_with(prefix)) {
				errorMessage = "shader uniform has an unexpected SPIR-V resource name";
				return false;
			}
			if (!compiler.has_decoration(uniform.id, spv::DecorationBinding) ||
				!compiler.has_decoration(uniform.id, spv::DecorationDescriptorSet)) {
				errorMessage = "shader uniform is missing its WebGPU resource binding";
				return false;
			}

			nlohmann::json reflectedUniform;
			reflectedUniform["name"] = spirvName.substr(prefix.size());
			reflectedUniform["group"] = compiler.get_decoration(uniform.id, spv::DecorationDescriptorSet);
			reflectedUniform["binding"] = compiler.get_decoration(uniform.id, spv::DecorationBinding);
			uniforms.push_back(std::move(reflectedUniform));
		}
		std::sort(uniforms.begin(), uniforms.end(), [](const nlohmann::json &left, const nlohmann::json &right) {
			const auto leftKey = std::pair(left.at("group").get<uint32_t>(), left.at("binding").get<uint32_t>());
			const auto rightKey = std::pair(right.at("group").get<uint32_t>(), right.at("binding").get<uint32_t>());
			return leftKey < rightKey;
		});
		nlohmann::json reflection;
		reflection["uniforms"] = std::move(uniforms);
		uniformsJson = reflection.dump();
		return true;
	} catch (const std::exception &error) {
		errorMessage = error.what();
		return false;
	}
}

int compileAndEmit(WebOutputKind outputKind, ParseContext::ShaderStage shaderStage) {
	WebCompilerState &state = webState();
	if (!state.initialized) {
		appendCompilerLog("error", "compiler state is not initialized");
		flushCompilerLogJson();
		return WebCompileStatusNotInitialized;
	}

	state.outputWasm.clear();
	state.outputWasmBase64.clear();
	state.outputShaderSpirv.clear();
	state.outputShaderSpirvBase64.clear();
	state.shaderUniformsJson = R"({"uniforms":[]})";
	state.compilerLog.clear();
	appendCompilerLog("info", "compile request started");

	ParseContext context;
	context.options.inputPath = kMainSourcePath;
	context.options.outputPath = outputKind == WebOutputKind::ShaderSpirv ? kOutputSpirvPath : kOutputWasmPath;
	context.options.emitWASM = outputKind == WebOutputKind::ProgramWasm;
	context.options.emitLLVM = false;
	context.options.emitSPIRV = outputKind == WebOutputKind::ShaderSpirv;
	context.options.shaderStage = shaderStage;
	auto memoryFileSystem = std::make_unique<lsp::MemoryFileSystem>(std::make_unique<lsp::LocalFileSystem>());
	memoryFileSystem->setFile(kMainSourcePath, state.mainSource);
	context.fileSystem = std::move(memoryFileSystem);

	bool frontEndSuccess = compile(kMainSourcePath, context);
	bool codegenSuccess = false;
	if (frontEndSuccess) {
		appendCompilerLog("info", "front-end compile finished");
		codegenSuccess = generateCode(context);
	} else {
		appendCompilerLog("error", "front-end compile failed");
	}

	flushDiagnosticsJson(context.diagnostics);
	bool hasErrorDiagnostics = std::any_of(context.diagnostics.begin(), context.diagnostics.end(), [](const Diagnostic &d) {
		return d.level == Diagnostic::Level::Error;
	});

	if (!frontEndSuccess) {
		appendCompilerLog("error", "front-end compile failed with diagnostics");
		flushCompilerLogJson();
		return WebCompileStatusCompileFailed;
	}

	if (!codegenSuccess) {
		appendCompilerLog("error", "code generation failed");
		flushCompilerLogJson();
		return WebCompileStatusCodegenFailed;
	}

	if (hasErrorDiagnostics) {
		appendCompilerLog("error", "compile finished with error diagnostics");
		flushCompilerLogJson();
		return WebCompileStatusCompileFailed;
	}

	if (outputKind == WebOutputKind::ProgramWasm) {
		std::string readError;
		if (!readBinaryFile(kOutputWasmPath, state.outputWasm, readError)) {
			appendCompilerLog("error", "failed to read emitted wasm: " + readError);
			flushCompilerLogJson();
			return WebCompileStatusInternalError;
		}
		state.outputWasmBase64 = encodeBase64(state.outputWasm);
		appendCompilerLog("info", "emitted wasm bytes: " + std::to_string(state.outputWasm.size()));
	} else {
		std::string readError;
		if (!readBinaryFile(kOutputSpirvPath, state.outputShaderSpirv, readError)) {
			std::cerr << "Shader artifact read failed: " << readError << '\n';
			appendCompilerLog("error", "An error occurred. Check the browser log.");
			flushCompilerLogJson();
			return WebCompileStatusInternalError;
		}
		std::string reflectionError;
		if (!reflectShaderSpirv(state.outputShaderSpirv, state.shaderUniformsJson, reflectionError)) {
			std::cerr << "Shader reflection failed: " << reflectionError << '\n';
			appendCompilerLog("error", "An error occurred. Check the browser log.");
			flushCompilerLogJson();
			return WebCompileStatusInternalError;
		}
		state.outputShaderSpirvBase64 = encodeBase64(state.outputShaderSpirv);
		appendCompilerLog("info", "emitted SPIR-V shader bytes: " + std::to_string(state.outputShaderSpirv.size()));
	}

	appendCompilerLog("info", "compile request succeeded");
	flushCompilerLogJson();
	return WebCompileStatusOk;
}

const char *exchangeLspJson(const char *messageJson) {
	WebCompilerState &state = webState();
	if (!state.initialized) {
		std::cerr << "Web LSP exchange requested before compiler initialization\n";
		state.lspExchangeJson = R"([{"jsonrpc":"2.0","id":null,"error":{"code":-32603,"message":"Internal error"}}])";
		return state.lspExchangeJson.c_str();
	}
	if (!messageJson) {
		std::cerr << "Web LSP exchange received no JSON message\n";
		state.lspExchangeJson = R"([{"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Invalid request"}}])";
		return state.lspExchangeJson.c_str();
	}

	try {
		if (!state.lspServer)
			state.lspServer = std::make_unique<WebLspServer>();
		state.lspExchangeJson = state.lspServer->exchange(Json::parse(messageJson)).dump();
	} catch (const Json::parse_error &error) {
		std::cerr << "Web LSP JSON parse failed: " << error.what() << '\n';
		state.lspExchangeJson = R"([{"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error"}}])";
	} catch (const std::exception &error) {
		std::cerr << "Web LSP exchange failed: " << error.what() << '\n';
		state.lspExchangeJson = R"([{"jsonrpc":"2.0","id":null,"error":{"code":-32603,"message":"Internal error"}}])";
	}
	return state.lspExchangeJson.c_str();
}

} // namespace

extern "C" {

// NOLINTBEGIN(readability-identifier-naming)
EMSCRIPTEN_KEEPALIVE void dynlex_web_init() {
	WebCompilerState &state = webState();
	state = {};
	state.initialized = true;
	state.mainSource = "print \"\"";
	state.diagnosticsJson = R"({"diagnostics":[]})";
	state.compilerLogJson = R"({"messages":[{"level":"info","message":"compiler initialized"}]})";
	state.lspExchangeJson = "[]";
	state.lspServer = std::make_unique<WebLspServer>();
}

EMSCRIPTEN_KEEPALIVE void dynlex_web_set_main_source(const char *utf8Source) {
	WebCompilerState &state = webState();
	if (!state.initialized)
		dynlex_web_init();
	state.mainSource = utf8Source ? std::string(utf8Source) : std::string{};
}

EMSCRIPTEN_KEEPALIVE int dynlex_web_compile_and_emit_wasm() {
	return compileAndEmit(WebOutputKind::ProgramWasm, ParseContext::ShaderStage::Fragment);
}

EMSCRIPTEN_KEEPALIVE int dynlex_web_compile_and_emit_shader_spirv(const char *shaderStage) {
	if (!shaderStage) {
		std::cerr << "Shader compilation requested without a stage\n";
		return WebCompileStatusInternalError;
	}
	const std::string_view stage(shaderStage);
	if (stage == "fragment")
		return compileAndEmit(WebOutputKind::ShaderSpirv, ParseContext::ShaderStage::Fragment);
	if (stage == "vertex")
		return compileAndEmit(WebOutputKind::ShaderSpirv, ParseContext::ShaderStage::Vertex);
	std::cerr << "Shader compilation requested with invalid stage: " << stage << '\n';
	return WebCompileStatusInternalError;
}

EMSCRIPTEN_KEEPALIVE const char *dynlex_web_get_diagnostics_json() { return webState().diagnosticsJson.c_str(); }

EMSCRIPTEN_KEEPALIVE const uint8_t *dynlex_web_get_output_wasm_ptr() {
	if (webState().outputWasm.empty())
		return nullptr;
	return webState().outputWasm.data();
}

EMSCRIPTEN_KEEPALIVE int dynlex_web_get_output_wasm_len() {
	const size_t size = webState().outputWasm.size();
	if (size > static_cast<size_t>(std::numeric_limits<int>::max()))
		return std::numeric_limits<int>::max();
	return static_cast<int>(size);
}

EMSCRIPTEN_KEEPALIVE const char *dynlex_web_get_output_wasm_base64() { return webState().outputWasmBase64.c_str(); }

EMSCRIPTEN_KEEPALIVE const char *dynlex_web_get_output_shader_spirv_base64() {
	return webState().outputShaderSpirvBase64.c_str();
}

EMSCRIPTEN_KEEPALIVE const char *dynlex_web_get_shader_uniforms_json() { return webState().shaderUniformsJson.c_str(); }

EMSCRIPTEN_KEEPALIVE const char *dynlex_web_get_compiler_log_json() { return webState().compilerLogJson.c_str(); }

EMSCRIPTEN_KEEPALIVE const char *dynlex_web_lsp_exchange_json(const char *messageJson) { return exchangeLspJson(messageJson); }
// NOLINTEND(readability-identifier-naming)

} // extern "C"

#endif
