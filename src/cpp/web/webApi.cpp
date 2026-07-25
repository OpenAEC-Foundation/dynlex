#ifdef DYNLEX_WEB

#include "codegen/codegen.h"
#include "compiler/compiler.h"
#include "lsp/dynlexServer.h"
#include "lsp/fileSystem.h"
#include "parseContext.h"
#include "pathUtils.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <spirv_glsl.hpp>
#include <string>
#include <string_view>
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
	ShaderGlsl,
};

struct CompilerLogEntry {
	std::string level;
	std::string message;
};

class WebLspServer final : public lsp::DynLexServer {
  public:
	WebLspServer() : lsp::DynLexServer(0) {
		lsp::InitializeParams params;
		params.rootUri = pathutil::toAbsoluteUri("/workspace");
		(void)onInitialize(params);
	}

	void resetMainDocument() {
		if (!documentOpen)
			return;
		lsp::DidCloseTextDocumentParams closeParams;
		closeParams.textDocument.uri = documentUri;
		onDidClose(closeParams);
		documentOpen = false;
		documentVersion = 0;
		syncedSource.clear();
		documentUri.clear();
	}

	void syncMainDocument(const std::string &uri, const std::string &source) {
		if (!documentOpen || documentUri != uri) {
			resetMainDocument();
			lsp::DidOpenTextDocumentParams openParams;
			openParams.textDocument.uri = uri;
			openParams.textDocument.languageId = "dynlex";
			openParams.textDocument.version = 1;
			openParams.textDocument.text = source;
			onDidOpen(openParams);
			documentOpen = true;
			documentUri = uri;
			documentVersion = 1;
			syncedSource = source;
			return;
		}

		if (syncedSource == source)
			return;

		lsp::DidChangeTextDocumentParams changeParams;
		changeParams.textDocument.uri = uri;
		changeParams.textDocument.version = documentVersion + 1;
		lsp::TextDocumentContentChangeEvent change;
		change.text = source;
		changeParams.contentChanges.push_back(std::move(change));
		onDidChange(changeParams);

		documentVersion = changeParams.textDocument.version;
		syncedSource = source;
	}

	std::optional<lsp::Location> definitionAt(const std::string &uri, int zeroBasedLine, int zeroBasedCharacter) {
		lsp::TextDocumentPositionParams params;
		params.textDocument.uri = uri;
		params.position.line = std::max(0, zeroBasedLine);
		params.position.character = std::max(0, zeroBasedCharacter);
		return onDefinition(params);
	}

	std::optional<lsp::Hover> hoverAt(const std::string &uri, int zeroBasedLine, int zeroBasedCharacter) {
		lsp::TextDocumentPositionParams params;
		params.textDocument.uri = uri;
		params.position.line = std::max(0, zeroBasedLine);
		params.position.character = std::max(0, zeroBasedCharacter);
		return onHover(params);
	}

	lsp::SemanticTokens semanticTokensFor(const std::string &uri) {
		lsp::SemanticTokensParams params;
		params.textDocument.uri = uri;
		return onSemanticTokensFull(params);
	}

  private:
	bool documentOpen = false;
	std::string documentUri;
	int documentVersion = 0;
	std::string syncedSource;
};

struct WebCompilerState {
	bool initialized = false;
	std::string mainSource;
	std::vector<uint8_t> outputWasm;
	std::string outputWasmBase64;
	std::string outputShaderGlsl;
	std::string shaderUniformsJson = R"({"uniforms":[]})";
	std::vector<CompilerLogEntry> compilerLog;
	std::string diagnosticsJson = R"({"diagnostics":[]})";
	std::string compilerLogJson = R"({"messages":[]})";
	std::string lspMainUri;
	std::string lspHoverJson = "null";
	std::string lspDefinitionJson = "null";
	std::string lspSemanticTokensJson = R"({"data":[],"legend":{"tokenTypes":[],"tokenModifiers":[]}})";
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

nlohmann::json lspLegendJson() {
	nlohmann::json legend;
	legend["tokenTypes"] = lsp::getSemanticTokenTypes();
	legend["tokenModifiers"] = lsp::getSemanticTokenModifiers();
	return legend;
}

std::string defaultLspSemanticTokensJson() {
	nlohmann::json root;
	root["data"] = nlohmann::json::array();
	root["legend"] = lspLegendJson();
	return root.dump();
}

std::string makeLspErrorJson(std::string message) {
	nlohmann::json error;
	error["error"] = std::move(message);
	return error.dump();
}

bool ensureLspDocumentReady(std::string &errorMessage) {
	WebCompilerState &state = webState();
	if (!state.initialized) {
		errorMessage = "compiler state is not initialized";
		return false;
	}
	if (!state.lspServer)
		state.lspServer = std::make_unique<WebLspServer>();
	try {
		state.lspServer->syncMainDocument(state.lspMainUri, state.mainSource);
		return true;
	} catch (const std::exception &error) {
		errorMessage = error.what();
		return false;
	}
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

bool translateSpirvToWebGlsl(
	const std::string &spirvPath, ParseContext::ShaderStage shaderStage, std::string &glslSource, std::string &uniformsJson,
	std::string &errorMessage
) {
	std::vector<uint8_t> spirvBytes;
	if (!readBinaryFile(spirvPath, spirvBytes, errorMessage))
		return false;
	if (spirvBytes.size() < 20 || spirvBytes.size() % sizeof(uint32_t) != 0) {
		errorMessage = "emitted SPIR-V is not a complete word stream";
		return false;
	}

	std::vector<uint32_t> spirvWords(spirvBytes.size() / sizeof(uint32_t));
	std::memcpy(spirvWords.data(), spirvBytes.data(), spirvBytes.size());

	try {
		spirv_cross::CompilerGLSL compiler(std::move(spirvWords));
		spirv_cross::ShaderResources resources = compiler.get_shader_resources();

		if (shaderStage == ParseContext::ShaderStage::Fragment) {
			for (const spirv_cross::Resource &output : resources.stage_outputs)
				compiler.set_name(output.id, "dynlexColor");
		}

		nlohmann::json uniforms = nlohmann::json::array();
		for (const spirv_cross::Resource &uniform : resources.uniform_buffers) {
			std::string spirvName = compiler.get_name(uniform.id);
			constexpr std::string_view prefix = "ubo_";
			if (!spirvName.starts_with(prefix)) {
				errorMessage = "shader uniform has an unexpected SPIR-V resource name";
				return false;
			}
			uint32_t binding = compiler.get_decoration(uniform.id, spv::DecorationBinding);
			std::string blockName = "DynlexUniformBlock" + std::to_string(binding);
			compiler.set_name(uniform.base_type_id, blockName);
			compiler.set_name(uniform.id, "dynlexUniform" + std::to_string(binding));
			compiler.set_member_name(uniform.base_type_id, 0, "value");

			nlohmann::json reflectedUniform;
			reflectedUniform["name"] = spirvName.substr(prefix.size());
			reflectedUniform["block"] = blockName;
			reflectedUniform["binding"] = binding;
			uniforms.push_back(std::move(reflectedUniform));
		}
		std::sort(uniforms.begin(), uniforms.end(), [](const nlohmann::json &left, const nlohmann::json &right) {
			return left.at("binding").get<uint32_t>() < right.at("binding").get<uint32_t>();
		});

		spirv_cross::CompilerGLSL::Options options;
		options.version = 300;
		options.es = true;
		options.vulkan_semantics = false;
		options.separate_shader_objects = false;
		options.enable_420pack_extension = false;
		options.force_zero_initialized_variables = true;
		options.fragment.default_float_precision = spirv_cross::CompilerGLSL::Options::Highp;
		compiler.set_common_options(options);

		glslSource = compiler.compile();
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
	state.outputShaderGlsl.clear();
	state.shaderUniformsJson = R"({"uniforms":[]})";
	state.compilerLog.clear();
	appendCompilerLog("info", "compile request started");

	ParseContext context;
	context.options.inputPath = kMainSourcePath;
	context.options.outputPath = outputKind == WebOutputKind::ShaderGlsl ? kOutputSpirvPath : kOutputWasmPath;
	context.options.emitWASM = outputKind == WebOutputKind::ProgramWasm;
	context.options.emitLLVM = false;
	context.options.emitSPIRV = outputKind == WebOutputKind::ShaderGlsl;
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
		std::string translationError;
		if (!translateSpirvToWebGlsl(
				kOutputSpirvPath, shaderStage, state.outputShaderGlsl, state.shaderUniformsJson, translationError
			)) {
			std::cerr << "Shader translation failed: " << translationError << '\n';
			appendCompilerLog("error", "An error occurred. Check the browser log.");
			flushCompilerLogJson();
			return WebCompileStatusInternalError;
		}
		appendCompilerLog("info", "emitted WebGL shader source");
	}

	appendCompilerLog("info", "compile request succeeded");
	flushCompilerLogJson();
	return WebCompileStatusOk;
}

const char *lspHoverJson(int zeroBasedLine, int zeroBasedColumn) {
	WebCompilerState &state = webState();
	std::string errorMessage;
	if (!ensureLspDocumentReady(errorMessage)) {
		state.lspHoverJson = makeLspErrorJson("hover request failed: " + errorMessage);
		return state.lspHoverJson.c_str();
	}

	try {
		std::optional<lsp::Hover> hover = state.lspServer->hoverAt(state.lspMainUri, zeroBasedLine, zeroBasedColumn);
		state.lspHoverJson = hover ? nlohmann::json(*hover).dump() : "null";
	} catch (const std::exception &error) {
		state.lspHoverJson = makeLspErrorJson("hover request failed: " + std::string(error.what()));
	}
	return state.lspHoverJson.c_str();
}

const char *lspDefinitionJson(int zeroBasedLine, int zeroBasedColumn) {
	WebCompilerState &state = webState();
	std::string errorMessage;
	if (!ensureLspDocumentReady(errorMessage)) {
		state.lspDefinitionJson = makeLspErrorJson("definition request failed: " + errorMessage);
		return state.lspDefinitionJson.c_str();
	}

	try {
		std::optional<lsp::Location> location = state.lspServer->definitionAt(state.lspMainUri, zeroBasedLine, zeroBasedColumn);
		state.lspDefinitionJson = location ? nlohmann::json(*location).dump() : "null";
	} catch (const std::exception &error) {
		state.lspDefinitionJson = makeLspErrorJson("definition request failed: " + std::string(error.what()));
	}
	return state.lspDefinitionJson.c_str();
}

const char *lspSemanticTokensJson() {
	WebCompilerState &state = webState();
	std::string errorMessage;
	if (!ensureLspDocumentReady(errorMessage)) {
		state.lspSemanticTokensJson = makeLspErrorJson("semantic token request failed: " + errorMessage);
		return state.lspSemanticTokensJson.c_str();
	}

	try {
		lsp::SemanticTokens tokens = state.lspServer->semanticTokensFor(state.lspMainUri);
		nlohmann::json root;
		root["data"] = std::move(tokens.data);
		root["legend"] = lspLegendJson();
		state.lspSemanticTokensJson = root.dump();
	} catch (const std::exception &error) {
		state.lspSemanticTokensJson = makeLspErrorJson("semantic token request failed: " + std::string(error.what()));
	}
	return state.lspSemanticTokensJson.c_str();
}

} // namespace

extern "C" {

// NOLINTBEGIN(readability-identifier-naming)
EMSCRIPTEN_KEEPALIVE void dynlex_web_init() {
	WebCompilerState &state = webState();
	state = {};
	state.initialized = true;
	state.mainSource = "print \"\"";
	state.lspMainUri = pathutil::toAbsoluteUri(kMainSourcePath);
	state.diagnosticsJson = R"({"diagnostics":[]})";
	state.compilerLogJson = R"({"messages":[{"level":"info","message":"compiler initialized"}]})";
	state.lspHoverJson = "null";
	state.lspDefinitionJson = "null";
	state.lspSemanticTokensJson = defaultLspSemanticTokensJson();
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

EMSCRIPTEN_KEEPALIVE int dynlex_web_compile_and_emit_shader_glsl(const char *shaderStage) {
	if (!shaderStage) {
		std::cerr << "Shader compilation requested without a stage\n";
		return WebCompileStatusInternalError;
	}
	const std::string_view stage(shaderStage);
	if (stage == "fragment")
		return compileAndEmit(WebOutputKind::ShaderGlsl, ParseContext::ShaderStage::Fragment);
	if (stage == "vertex")
		return compileAndEmit(WebOutputKind::ShaderGlsl, ParseContext::ShaderStage::Vertex);
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

EMSCRIPTEN_KEEPALIVE const char *dynlex_web_get_output_shader_glsl() { return webState().outputShaderGlsl.c_str(); }

EMSCRIPTEN_KEEPALIVE const char *dynlex_web_get_shader_uniforms_json() { return webState().shaderUniformsJson.c_str(); }

EMSCRIPTEN_KEEPALIVE const char *dynlex_web_get_compiler_log_json() { return webState().compilerLogJson.c_str(); }

EMSCRIPTEN_KEEPALIVE const char *dynlex_web_get_lsp_hover_json(int zeroBasedLine, int zeroBasedColumn) {
	return lspHoverJson(zeroBasedLine, zeroBasedColumn);
}

EMSCRIPTEN_KEEPALIVE const char *dynlex_web_get_lsp_definition_json(int zeroBasedLine, int zeroBasedColumn) {
	return lspDefinitionJson(zeroBasedLine, zeroBasedColumn);
}

EMSCRIPTEN_KEEPALIVE const char *dynlex_web_get_lsp_semantic_tokens_json() { return lspSemanticTokensJson(); }
// NOLINTEND(readability-identifier-naming)

} // extern "C"

#endif
