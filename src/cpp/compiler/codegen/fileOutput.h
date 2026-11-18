#pragma once
#include "parseContext.h"
#include "llvm/Support/raw_ostream.h"
#include <string_view>

inline bool writeFileOutput(
	ParseContext &context, llvm::StringRef path, std::string_view bytes, const char *openError, const char *writeError
) {
	std::error_code error;
	llvm::raw_fd_ostream output(path, error);
	const char *message = openError;
	if (!error) {
		output.write(bytes.data(), bytes.size());
		if (path == "-")
			output.flush();
		else
			output.close();
		error = output.error();
		message = writeError;
	}
	if (error) {
		output.clear_error();
		context.addDiagnostic(Diagnostic(context, Diagnostic::Level::Error, message, Range(), "error", error.message()));
		return false;
	}
	return true;
}
