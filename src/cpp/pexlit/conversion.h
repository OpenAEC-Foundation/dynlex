#include <string>
#include <vector>

enum class ByteStringFormat {
	Raw,
	// binary
	Bin,
	// hexadecimal
	Hex
};

// returns false if dest is not a hexadecimal digit
constexpr bool hexCharToByte(char in, uint8_t &dest) {
	if (in >= '0' && in <= '9') {
		dest = in - '0';
	} else {
		// 97('a') - 65('A') = 32. we OR it in to make it lowercase so we don't have to check for both lower and upper case.
		// non characters will be modified, but will still fall outside of the range.
		char lowerCase = in | (char)('a' - 'A');
		if (lowerCase >= 'a' && lowerCase <= 'f')
			dest = lowerCase + (10 - 'a');
		else
			return false;
	}
	return true;
}

constexpr bool hex2Bytes(char *inStart, char *inEnd, std::vector<uint8_t> &bytes) {
	// amount of characters doesn't match
	size_t amount = inEnd - inStart;
	if (amount % 2)
		return false;
	bytes = std::vector<uint8_t>(amount / 2);

	char *inPtr = inStart;
	for (uint8_t &currentByte : bytes) {
		uint8_t higherByte{};
		if (!hexCharToByte(*inPtr++, higherByte))
			return false;
		if (!hexCharToByte(*inPtr++, currentByte))
			return false;
		currentByte |= higherByte << 4;
	}
	return true;
}
constexpr bool bin2Bytes(char *inStart, char *inEnd, std::vector<uint8_t> &bytes) {
	// amount of characters doesn't match
	size_t amount = inEnd - inStart;
	if (amount % 8)
		return false;
	bytes = std::vector<uint8_t>(amount / 8);

	char *inPtr = inStart;
	for (uint8_t &currentByte : bytes) {
		for (int i = 7; i >= 0; i--) {
			char currentChar = *inPtr++;
			int rest = currentChar - '0';
			if (rest & (~1)) {
				return false;
			} else {
				currentByte |= rest << i;
			}
		}
	}
	return true;
}

inline bool string2Bytes(std::string in, std::vector<uint8_t> &bytes) {
	auto start = in.data(), end = start + in.size();
	if (in[0] == '#') {
		return hex2Bytes(++start, end, bytes);
	} else if (in[0] == 'B') {
		return bin2Bytes(++start, end, bytes);
	} else
		return false;
}

inline bool bytes2String(std::vector<uint8_t> in, std::string &str, ByteStringFormat format) {
	switch (format) {
	case ByteStringFormat::Bin: {
		str = std::string(in.size() * 8 + 1, 'B');
		auto charPtr = str.data();
		for (uint8_t currentByte : in) {
			for (int i = 7; i >= 0; i--) {
				// increment first, because we start at [1]
				*++charPtr = '0' + ((currentByte >> i) & 1);
			}
		}
	} break;
	case ByteStringFormat::Hex: {
		str = std::string(in.size() * 2 + 1, '#');
		auto charPtr = str.data();
		for (uint8_t currentByte : in) {
			for (int i = 4; i >= 0; i -= 4) {
				uint8_t val = ((currentByte >> i) & 0xf);
				*++charPtr = val < 10 ? '0' + val : 'A' + (val - 10);
			}
		}
	} break;
	default:
		return false;
	}
	return true;
}