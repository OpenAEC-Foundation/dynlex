#pragma once
#include "type.h"
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

struct Section;
struct ClassDefinition;
using ClassIdentityPaths = std::map<const ClassDefinition *, std::string>;

std::string stableIdentityDigest(std::string_view text);
nlohmann::json sectionIdentity(const Section &section);
void collectSections(Section *section, const std::string &path, std::map<std::string, Section *> &sections);
enum class TypeIdentityMode { Concrete, Descriptor };
std::optional<nlohmann::json>
typeIdentity(const DataType &type, const ClassIdentityPaths &classPaths, TypeIdentityMode mode = TypeIdentityMode::Concrete);
