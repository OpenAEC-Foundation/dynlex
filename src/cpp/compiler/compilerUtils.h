#pragma once
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

[[noreturn]] inline void crashCompilerBug(const std::string &message) {
	std::cerr << "FATAL: compiler bug: " << message << '\n';
	std::abort();
}

inline void requireCompilerInvariant(bool condition, std::string_view message) {
	if (!condition)
		crashCompilerBug(std::string(message));
}

[[noreturn]] inline void
crashUnimplementedIntrinsic(const char *subsystem, const std::string &name, const std::string &uri = "", int line = -1) {
	std::string message = "unimplemented intrinsic in ";
	message += subsystem;
	message += ": \"";
	message += name;
	message += "\"";
	if (!uri.empty() && line >= 1)
		message += " at " + uri + ":" + std::to_string(line);
	crashCompilerBug(message);
}
