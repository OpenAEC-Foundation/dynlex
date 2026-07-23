#include "runtimeText.h"

#include <stdint.h>

bool dynlex_runtime_is_valid_utf8(const char *text, size_t length) {
	if (text == NULL)
		return length == 0;

	size_t index = 0;
	while (index < length) {
		uint8_t first = (uint8_t)text[index++];
		if (first <= 0x7f)
			continue;

		uint32_t code_point;
		size_t continuation_count;
		uint32_t minimum;
		if (first >= 0xc2 && first <= 0xdf) {
			code_point = first & 0x1fU;
			continuation_count = 1;
			minimum = 0x80;
		} else if (first >= 0xe0 && first <= 0xef) {
			code_point = first & 0x0fU;
			continuation_count = 2;
			minimum = 0x800;
		} else if (first >= 0xf0 && first <= 0xf4) {
			code_point = first & 0x07U;
			continuation_count = 3;
			minimum = 0x10000;
		} else {
			return false;
		}

		if (continuation_count > length - index)
			return false;
		for (size_t continuation = 0; continuation < continuation_count; ++continuation) {
			uint8_t byte = (uint8_t)text[index++];
			if ((byte & 0xc0U) != 0x80U)
				return false;
			code_point = (code_point << 6U) | (byte & 0x3fU);
		}
		if (code_point < minimum || code_point > 0x10ffffU || (code_point >= 0xd800U && code_point <= 0xdfffU))
			return false;
	}
	return true;
}
