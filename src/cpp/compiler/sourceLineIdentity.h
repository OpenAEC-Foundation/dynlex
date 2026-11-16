#pragma once
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct CodeLine;
struct ParseContext;
struct Section;
struct SourceLineIdentity {
	CodeLine *line;
	nlohmann::json identity;
};

std::vector<SourceLineIdentity>
sourceLineIdentities(const ParseContext &context, const std::map<const Section *, std::string> &paths);
