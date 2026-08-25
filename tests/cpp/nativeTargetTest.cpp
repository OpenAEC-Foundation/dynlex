#include "compiler/codegen/nativeTarget.h"
#include "compiler/codegen/nativeLibraries.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
	if (condition)
		return;
	std::cerr << message << '\n';
	std::exit(1);
}

void expectWindowsObjectTarget(llvm::StringRef input, llvm::StringRef expected) {
	const llvm::Triple triple = normalizedNativeTargetTriple(input);
	expect(triple.str() == expected, "Windows target triple was not normalized");
	expect(triple.isOSBinFormatCOFF(), "Windows target did not select the COFF object format");
	expect(!triple.isOSBinFormatELF(), "Windows target incorrectly selected the ELF object format");
}

} // namespace

int main() {
	expectWindowsObjectTarget("x86_64-w64-mingw32", "x86_64-w64-windows-gnu");
	expectWindowsObjectTarget("aarch64-w64-mingw32", "aarch64-w64-windows-gnu");
	expectWindowsObjectTarget("x86_64-w64-windows-gnu", "x86_64-w64-windows-gnu");

	const llvm::Triple windows = normalizedNativeTargetTriple("x86_64-w64-mingw32");
	expect(
		nativeLibraryArguments(windows, "dynlex_runtime", "runtime.a") ==
			std::vector<std::string>({"runtime.a", "-lshell32", "-lole32", "-luuid"}),
		"Windows runtime system libraries were not linked after the runtime archive"
	);
	const llvm::Triple linuxTarget("x86_64-unknown-linux-gnu");
	expect(
		nativeLibraryArguments(linuxTarget, "dynlex_runtime", "runtime.a") ==
			std::vector<std::string>({"runtime.a", "-pthread"}),
		"POSIX runtime thread support was not linked"
	);
	return 0;
}
