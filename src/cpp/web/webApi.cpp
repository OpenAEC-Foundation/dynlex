#include "textEncoding.h"
#ifdef DYNLEX_WEB

#include "codegen/codegen.h"
#include "codegen/spirv.h"
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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
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
	std::string entryPath = kMainSourcePath;
	std::optional<std::string> mainSource;
	std::vector<std::filesystem::path> importSourcePaths;
	std::vector<uint8_t> outputWasm;
	std::string outputWasmBase64;
	std::vector<uint8_t> outputShaderSpirv;
	std::string outputShaderSpirvBase64;
	std::string shaderReflectionJson = R"({"uniforms":[],"vertexInputs":[],"resources":[]})";
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

std::filesystem::path relativeSourcePath(const std::string &name) {
	const std::filesystem::path path(name);
	if (name.empty() || name.find('\0') != std::string::npos || path.is_absolute() || path.filename().empty() || path == "." ||
		*path.begin() == ".." || path.lexically_normal().generic_string() != name)
		throw std::invalid_argument("A source needs a normalized relative filename");
	return path;
}

void clearImportSources(WebCompilerState &state) {
	std::set<std::filesystem::path> directories;
	for (const auto &path : state.importSourcePaths) {
		std::filesystem::remove(path);
		for (auto directory = path.parent_path(); directory != "/workspace"; directory = directory.parent_path())
			directories.insert(directory);
	}
	for (auto directory = directories.rbegin(); directory != directories.rend(); ++directory) {
		if (std::filesystem::is_empty(*directory))
			std::filesystem::remove(*directory);
	}
	state.importSourcePaths.clear();
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
	nlohmann::json result = {{"file", nullptr}, {"line", nullptr}, {"column", nullptr}};
	if (!location.sourceFile)
		return result;
	result["file"] = location.sourceFile->uri;
	result["line"] = location.sourceFileLineIndex + 1;
	result["column"] = lsp::utf16Column(location.sourceFile->getLine(location.sourceFileLineIndex), location.column) + 1;
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
		nlohmann::json entry = sourceLocationToJson(diagnostic.range.sourceStart());
		entry["severity"] = diagnosticSeverity(diagnostic.level);
		entry["message"] = diagnostic.message;
		entry["range"] = rangeToJson(diagnostic.range);

		nlohmann::json related = nlohmann::json::array();
		for (const RelatedInfo &relatedInfo : diagnostic.relatedInfo) {
			nlohmann::json relatedEntry = sourceLocationToJson(relatedInfo.range.sourceStart());
			relatedEntry["message"] = relatedInfo.message;
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

#include "webShaderResources.inl"

bool reflectShaderSpirv(
	const std::vector<uint8_t> &spirvBytes, const ParseContext &context, std::string &reflectionJson, std::string &errorMessage
) {
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
			const auto &block = compiler.get_type(uniform.base_type_id);
			requireCompilerInvariant(block.member_types.size() == 1, "uniform block has an unexpected member count");
			const auto &value = compiler.get_type(block.member_types.front());
			const char *scalar = nullptr;
			uint32_t rows = 1, columns = value.vecsize;
			if (value.basetype == spirv_cross::SPIRType::Struct) {
				requireCompilerInvariant(!value.member_types.empty(), "matrix uniform storage has no rows");
				const auto &row = compiler.get_type(value.member_types.front());
				columns = row.vecsize;
				const uint32_t step = columns < 4 ? 2 : 1;
				rows = value.member_types.size() / step;
				requireCompilerInvariant(
					row.basetype == spirv_cross::SPIRType::Float && row.width == 32 && row.columns == 1 && row.array.empty() &&
						columns >= 2 && columns <= 4 && rows >= 2 && rows <= 4 && value.member_types.size() == rows * step &&
						compiler.get_declared_struct_size(value) == rows * 16,
					"matrix uniform has an unsupported physical layout"
				);
				for (uint32_t index = 0; index < rows; ++index) {
					requireCompilerInvariant(
						value.member_types[index * step] == value.member_types.front() &&
							compiler.type_struct_member_offset(value, index * step) == index * 16,
						"matrix uniform rows have inconsistent storage"
					);
					if (step == 2) {
						const auto &padding = compiler.get_type(value.member_types[index * step + 1]);
						requireCompilerInvariant(
							padding.basetype == spirv_cross::SPIRType::Float && padding.width == 32 && padding.columns == 1 &&
								padding.vecsize == 4 - columns && padding.array.empty() &&
								compiler.type_struct_member_offset(value, index * step + 1) == index * 16 + columns * 4,
							"matrix uniform padding has inconsistent storage"
						);
					}
				}
				scalar = "float32";
			} else {
				scalar = value.basetype == spirv_cross::SPIRType::Float	 ? "float32"
						 : value.basetype == spirv_cross::SPIRType::Int	 ? "sint32"
						 : value.basetype == spirv_cross::SPIRType::UInt ? "uint32"
																		 : nullptr;
				requireCompilerInvariant(
					scalar && value.width == 32 && value.columns == 1 && value.array.empty(),
					"uniform block has an unsupported reflected type"
				);
			}
			reflectedUniform["type"] = {{"scalar", scalar}, {"rows", rows}, {"columns", columns}};
			uniforms.push_back(std::move(reflectedUniform));
		}
		std::sort(uniforms.begin(), uniforms.end(), [](const nlohmann::json &left, const nlohmann::json &right) {
			const auto leftKey = std::pair(left.at("group").get<uint32_t>(), left.at("binding").get<uint32_t>());
			const auto rightKey = std::pair(right.at("group").get<uint32_t>(), right.at("binding").get<uint32_t>());
			return leftKey < rightKey;
		});
		nlohmann::json reflection;
		reflection["uniforms"] = std::move(uniforms);
		nlohmann::json inputs = nlohmann::json::array();
		if (context.options.shaderStage == ParseContext::ShaderStage::Vertex) {
			for (const auto &input : resources.stage_inputs) {
				const std::string symbol = compiler.get_name(input.id);
				auto declared =
					std::find_if(context.shaderVertexInputs.begin(), context.shaderVertexInputs.end(), [&](const auto &entry) {
					return shaderVertexInputGlobalName(entry.name, entry.location, entry.type) == symbol;
				});
				if (declared == context.shaderVertexInputs.end() ||
					!compiler.has_decoration(input.id, spv::DecorationLocation)) {
					errorMessage = "Emitted vertex input is missing its declared interface";
					return false;
				}
				const auto &type = compiler.get_type(input.base_type_id);
				std::string format;
				if (type.basetype == spirv_cross::SPIRType::Float)
					format = "float32";
				else if (type.basetype == spirv_cross::SPIRType::Int)
					format = "sint32";
				else if (type.basetype == spirv_cross::SPIRType::UInt)
					format = "uint32";
				if (format.empty() || type.width != 32 || type.columns != 1 || type.vecsize < 1 || type.vecsize > 4 ||
					!type.array.empty()) {
					errorMessage = "Emitted vertex input has an unsupported GPU format";
					return false;
				}
				if (type.vecsize > 1)
					format += "x" + std::to_string(type.vecsize);
				inputs.push_back(
					{{"name", declared->name},
					 {"location", compiler.get_decoration(input.id, spv::DecorationLocation)},
					 {"format", format}}
				);
			}
			std::sort(inputs.begin(), inputs.end(), [](const auto &left, const auto &right) {
				return left.at("location") < right.at("location");
			});
		}
		reflection["vertexInputs"] = std::move(inputs);
		reflection["resources"] = reflectedShaderResources(compiler, resources, context);
		reflectionJson = reflection.dump();
		return true;
	} catch (const std::exception &error) {
		errorMessage = error.what();
		return false;
	}
}

int compileAndEmit(WebOutputKind outputKind, ParseContext::ShaderStage shaderStage, bool traceExecution = false) {
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
	state.shaderReflectionJson = R"({"uniforms":[],"vertexInputs":[],"resources":[]})";
	state.compilerLog.clear();
	appendCompilerLog("info", "compile request started");

	ParseContext context;
	context.options.inputPath = state.entryPath;
	context.options.outputPath = outputKind == WebOutputKind::ShaderSpirv ? kOutputSpirvPath : kOutputWasmPath;
	context.options.emitWASM = outputKind == WebOutputKind::ProgramWasm;
	context.options.emitLLVM = false;
	context.options.emitSPIRV = outputKind == WebOutputKind::ShaderSpirv;
	context.options.shaderStage = shaderStage;
	context.options.traceExecution = traceExecution;
	auto memoryFileSystem = std::make_unique<lsp::MemoryFileSystem>(std::make_unique<lsp::LocalFileSystem>());
	if (state.mainSource)
		memoryFileSystem->setFile(state.entryPath, *state.mainSource);
	context.fileSystem = std::move(memoryFileSystem);

	bool frontEndSuccess = compile(state.entryPath, context);
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
		if (!reflectShaderSpirv(state.outputShaderSpirv, context, state.shaderReflectionJson, reflectionError)) {
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
	clearImportSources(state);
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
	state.entryPath = kMainSourcePath;
	state.mainSource = utf8Source ? std::string(utf8Source) : std::string{};
}

EMSCRIPTEN_KEEPALIVE int dynlex_web_set_entry_file(const char *relativeFilename) {
	WebCompilerState &state = webState();
	if (!state.initialized)
		dynlex_web_init();
	try {
		if (!relativeFilename)
			throw std::invalid_argument("The entry file needs a relative filename");
		state.entryPath = (std::filesystem::path("/workspace") / relativeSourcePath(relativeFilename)).generic_string();
		state.mainSource.reset();
		return WebCompileStatusOk;
	} catch (const std::invalid_argument &error) {
		appendCompilerLog("error", error.what());
		flushCompilerLogJson();
		return WebCompileStatusCompileFailed;
	}
}

// Replace the browser workspace's project modules. No application library is
// embedded in the compiler; the caller explicitly selects an inline or file entry.
EMSCRIPTEN_KEEPALIVE int dynlex_web_set_import_sources(const char *sourcesJson) {
	WebCompilerState &state = webState();
	if (!state.initialized)
		dynlex_web_init();
	std::map<std::string, std::string> sources;
	try {
		if (!sourcesJson)
			throw std::invalid_argument("Imported sources need a JSON object");
		const Json input = Json::parse(sourcesJson);
		if (!input.is_object())
			throw std::invalid_argument("Imported sources must be a filename-to-source object");
		for (const auto &[name, value] : input.items()) {
			relativeSourcePath(name);
			sources.emplace(name, value.get<std::string>());
		}
		for (const auto &[name, source] : sources) {
			for (auto parent = std::filesystem::path(name).parent_path(); !parent.empty(); parent = parent.parent_path()) {
				if (sources.contains(parent.generic_string()))
					throw std::invalid_argument("An imported source filename cannot also be a directory");
			}
		}
	} catch (const std::exception &error) {
		appendCompilerLog("error", std::string("Imported sources rejected: ") + error.what());
		flushCompilerLogJson();
		return WebCompileStatusCompileFailed;
	}

	clearImportSources(state);
	for (const auto &[name, source] : sources) {
		const auto path = std::filesystem::path("/workspace") / name;
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary);
		output.exceptions(std::ios::badbit | std::ios::failbit);
		output.write(source.data(), static_cast<std::streamsize>(source.size()));
		output.close();
		state.importSourcePaths.push_back(path);
	}
	return WebCompileStatusOk;
}

EMSCRIPTEN_KEEPALIVE int dynlex_web_compile_and_emit_wasm() {
	return compileAndEmit(WebOutputKind::ProgramWasm, ParseContext::ShaderStage::Fragment);
}

EMSCRIPTEN_KEEPALIVE int dynlex_web_compile_and_emit_wasm_traced() {
	return compileAndEmit(WebOutputKind::ProgramWasm, ParseContext::ShaderStage::Fragment, true);
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

EMSCRIPTEN_KEEPALIVE const char *dynlex_web_get_shader_reflection_json() { return webState().shaderReflectionJson.c_str(); }

EMSCRIPTEN_KEEPALIVE const char *dynlex_web_get_compiler_log_json() { return webState().compilerLogJson.c_str(); }

EMSCRIPTEN_KEEPALIVE const char *dynlex_web_lsp_exchange_json(const char *messageJson) { return exchangeLspJson(messageJson); }
// NOLINTEND(readability-identifier-naming)

} // extern "C"

#endif
