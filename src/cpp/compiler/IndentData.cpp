#include "IndentData.h"
using namespace std::literals;

std::string charName(char c) {
	switch (c) {
	case ' ':
		return "space";
	case '\t':
		return "tab";
	default:
		return "'"s + c + "'";
	}
}