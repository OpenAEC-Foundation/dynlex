#include "textEncoding.h"
#include "llvm/Support/ConvertUTF.h"
#include <algorithm>

namespace lsp {

static int convertColumn(std::string_view text, int column, bool fromUtf16) {
	int bytes = 0, units = 0;
	while (bytes < static_cast<int>(text.size())) {
		const auto *start = reinterpret_cast<const llvm::UTF8 *>(text.data() + bytes);
		unsigned width = llvm::getUTF8SequenceSize(start, reinterpret_cast<const llvm::UTF8 *>(text.data() + text.size()));
		// Malformed file bytes occupy one replacement character each.
		int byteWidth = static_cast<int>(std::max(1u, width));
		int unitWidth = byteWidth == 4 ? 2 : 1;
		if ((fromUtf16 ? units + unitWidth : bytes + byteWidth) > column)
			break;
		bytes += byteWidth;
		units += unitWidth;
	}
	return fromUtf16 ? bytes : units;
}

int utf16Column(std::string_view text, int column) { return convertColumn(text, column, false); }
int byteColumn(std::string_view text, int column) { return convertColumn(text, column, true); }

} // namespace lsp
