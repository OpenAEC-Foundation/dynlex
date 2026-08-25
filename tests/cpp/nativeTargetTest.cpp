#include "compiler/codegen/nativeTarget.h"
#include <cstdlib>
#include <iostream>
#include <string_view>

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
	return 0;
}
