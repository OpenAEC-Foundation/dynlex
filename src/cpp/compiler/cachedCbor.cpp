#include "cachedCbor.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
class CacheCborReader {
	using Json = nlohmann::json;
	struct Frame {
		Json *value;
		size_t remaining;
		bool object;
	};
	std::string_view input;
	size_t cursor = 0;
	std::stack<Frame> pending;
	[[noreturn]] void invalid() const { throw std::runtime_error("invalid cache CBOR at byte " + std::to_string(cursor)); }
	unsigned byte() {
		if (cursor == input.size())
			invalid();
		return static_cast<unsigned char>(input[cursor++]);
	}
	uint64_t integer(size_t bytes) {
		uint64_t result = 0;
		for (size_t i = 0; i < bytes; i++)
			result = (result << 8) | byte();
		return result;
	}
	uint64_t argument(unsigned extra) {
		if (extra < 24)
			return extra;
		if (extra < 28)
			return integer(size_t{1} << (extra - 24));
		invalid();
	}
	std::string_view span(uint64_t size) {
		if (size > input.size() - cursor)
			invalid();
		auto result = input.substr(cursor, static_cast<size_t>(size));
		cursor += static_cast<size_t>(size);
		return result;
	}
	static double half(uint16_t bits) {
		unsigned exponent = (bits >> 10) & 31, fraction = bits & 1023;
		double value = exponent == 31
						   ? (fraction ? std::numeric_limits<double>::quiet_NaN() : std::numeric_limits<double>::infinity())
					   : exponent == 0 ? std::ldexp(static_cast<double>(fraction), -24)
									   : std::ldexp(static_cast<double>(1024 + fraction), static_cast<int>(exponent) - 25);
		return (bits & 0x8000) ? -value : value;
	}
	std::string key() {
		unsigned token = byte();
		if ((token >> 5) != 3)
			invalid();
		return std::string(span(argument(token & 31)));
	}
	void binary(Json &target, uint64_t size, bool subtype = false, uint64_t tag = 0) {
		auto bytes = span(size);
		std::vector<uint8_t> value(bytes.begin(), bytes.end());
		target = subtype ? Json::binary(std::move(value), tag) : Json::binary(std::move(value));
	}
	void value(Json &target) {
		unsigned token = byte(), major = token >> 5, extra = token & 31;
		if (major == 7) {
			switch (extra) {
			case 20:
				target = false;
				return;
			case 21:
				target = true;
				return;
			case 22:
				target = nullptr;
				return;
			case 25:
				target = half(static_cast<uint16_t>(integer(2)));
				return;
			case 26:
				target = static_cast<double>(std::bit_cast<float>(static_cast<uint32_t>(integer(4))));
				return;
			case 27:
				target = std::bit_cast<double>(integer(8));
				return;
			default:
				invalid();
			}
		}
		uint64_t size = argument(extra);
		switch (major) {
		case 0:
			target = size;
			return;
		case 1:
			if (size > static_cast<uint64_t>(INT64_MAX))
				invalid();
			target = int64_t{-1} - static_cast<int64_t>(size);
			return;
		case 2:
			binary(target, size);
			return;
		case 3:
			target = std::string(span(size));
			return;
		case 4:
		case 5: {
			bool object = major == 5;
			if (size > (input.size() - cursor) / (object ? 2 : 1))
				invalid();
			target = object ? Json::object() : Json::array();
			if (!object)
				target.get_ref<Json::array_t &>().reserve(static_cast<size_t>(size));
			pending.push({&target, static_cast<size_t>(size), object});
			return;
		}
		case 6: {
			unsigned binaryToken = byte();
			if ((binaryToken >> 5) != 2)
				invalid();
			binary(target, argument(binaryToken & 31), true, size);
			return;
		}
		default:
			invalid();
		}
	}

  public:
	explicit CacheCborReader(std::string_view input) : input(input) {}
	Json read() {
		Json result;
		value(result);
		while (!pending.empty()) {
			Frame &frame = pending.top();
			if (!frame.remaining) {
				pending.pop();
				continue;
			}
			--frame.remaining;
			Json *next;
			if (frame.object) {
				std::string name = key();
				auto &object = frame.value->get_ref<Json::object_t &>();
				size_t before = object.size();
				auto entry = object.emplace_hint(object.end(), std::move(name), Json{});
				if (object.size() != before + 1)
					invalid();
				next = &entry->second;
			} else {
				frame.value->push_back(nullptr);
				next = &frame.value->back();
			}
			value(*next);
		}
		if (cursor != input.size())
			invalid();
		return result;
	}
};

} // namespace

nlohmann::json parseCachedCbor(std::string_view input) { return CacheCborReader(input).read(); }
