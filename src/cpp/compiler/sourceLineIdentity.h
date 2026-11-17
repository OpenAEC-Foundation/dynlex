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

class SourceLineIdentityBuilder {
  public:
	explicit SourceLineIdentityBuilder(std::string owner) : owner(std::move(owner)) {}
	SourceLineIdentity append(CodeLine *line);

  private:
	std::string owner;
	std::map<std::string, size_t> occurrences;
};

std::vector<SourceLineIdentity>
sourceLineIdentities(const ParseContext &context, const std::map<const Section *, std::string> &paths);
