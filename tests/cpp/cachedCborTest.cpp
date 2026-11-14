#include "compiler/cachedCbor.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>
#include <string>

using Json = nlohmann::json;
using namespace std::string_literals;

static void require(bool condition) {
	if (!condition)
		throw std::runtime_error("cache CBOR value mismatch");
}

static bool equal(const Json &left, const Json &right) {
	if (left.type() != right.type())
		return false;
	if (left.is_number_float()) {
		double a = left.get<double>(), b = right.get<double>();
		if (std::isnan(a) || std::isnan(b))
			return std::isnan(a) && std::isnan(b);
		return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b);
	}
	if (left.is_object()) {
		if (left.size() != right.size())
			return false;
		for (auto it = left.begin(); it != left.end(); ++it)
			if (!right.contains(it.key()) || !equal(it.value(), right.at(it.key())))
				return false;
		return true;
	}
	if (left.is_array()) {
		if (left.size() != right.size())
			return false;
		for (size_t i = 0; i < left.size(); i++)
			if (!equal(left[i], right[i]))
				return false;
		return true;
	}
	if (left.is_binary()) {
		const auto &a = left.get_binary(), &b = right.get_binary();
		return a == b && a.has_subtype() == b.has_subtype() && (!a.has_subtype() || a.subtype() == b.subtype());
	}
	return left == right;
}

static void roundtrip(const Json &value) {
	auto bytes = Json::to_cbor(value);
	Json reference = Json::from_cbor(bytes, true, true, Json::cbor_tag_handler_t::store);
	Json restored = parseCachedCbor({reinterpret_cast<const char *>(bytes.data()), bytes.size()});
	require(equal(reference, restored));
}

static Json randomValue(std::mt19937_64 &random, unsigned depth) {
	switch (random() % (depth ? 8 : 6)) {
	case 0:
		return nullptr;
	case 1:
		return (random() & 1) != 0;
	case 2:
		return random();
	case 3:
		return std::bit_cast<int64_t>(random());
	case 4:
		return std::bit_cast<double>(random());
	case 5:
		return std::string(random() % 180, static_cast<char>('a' + random() % 26)) + "\0\xC3\xA5"s;
	case 6: {
		Json result = Json::array();
		for (size_t i = 0, count = random() % 5; i < count; i++)
			result.push_back(randomValue(random, depth - 1));
		return result;
	}
	default: {
		Json result = Json::object();
		for (size_t i = 0, count = random() % 5; i < count; i++)
			result[std::to_string(random())] = randomValue(random, depth - 1);
		return result;
	}
	}
}

int main() {
	Json fixtures = Json::array(
		{nullptr,
		 true,
		 false,
		 "",
		 "\0\xC3\xA5"s,
		 int64_t{0},
		 INT64_MIN,
		 INT64_MAX,
		 UINT64_MAX,
		 -0.0,
		 1.5,
		 std::numeric_limits<double>::denorm_min(),
		 std::numeric_limits<double>::max(),
		 std::numeric_limits<double>::infinity(),
		 -std::numeric_limits<double>::infinity(),
		 std::numeric_limits<double>::quiet_NaN(),
		 Json::array(),
		 Json::object(),
		 Json::binary({0, 255, 3}),
		 Json::binary({4, 0, 255}, 77)}
	);
	for (const Json &value : fixtures)
		roundtrip(value);
	for (uint64_t size : {23, 24, 255, 256, 65535, 65536})
		roundtrip(std::string(size, 'x'));
	std::mt19937_64 random(0);
	for (size_t i = 0; i < 1000; i++)
		roundtrip(randomValue(random, 5));
	for (const auto &bytes :
		 {std::string{}, "\x63x"s, "\x18"s, "\x9f\xff"s, "\xa1\x00\xf6"s, "\xa2\x61x\x00\x61x\x01"s, "\xf6\x00"s}) {
		bool failed = false;
		try {
			(void)parseCachedCbor(bytes);
		} catch (const std::runtime_error &) {
			failed = true;
		}
		require(failed);
	}
	Json value{
		{"unsigned", UINT64_MAX},
		{"signed", INT64_MIN},
		{"unicode", "\xC3\xA5"},
		{"binary", Json::binary({0, 255, 3}, 77)},
		{"values", Json::array({false, nullptr, -0.0, 1.5})}
	};
	std::cout << Json{{"value", value}, {"encoded", Json::to_cbor(value)}}.dump() << '\n';
}
