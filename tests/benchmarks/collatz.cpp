#include <cinttypes>
#include <cstdint>
#include <cstdio>

int main() {
	std::int64_t steps = 0;
	std::int64_t number = 1;
	while (number < 1000000) {
		std::int64_t current = number;
		while (current > 1) {
			steps++;
			std::int64_t remainder = current % 2;
			current = remainder == 0 ? current / 2 : (current * 3) + 1;
		}
		number++;
	}

	std::printf("%" PRId64 "\n", steps);
	return 0;
}
