#pragma once

#include <map>
#include <set>
#include <string>

struct Section;
inline constexpr const char *aggregateGraphVariant = "*";

// A concrete instantiation is the unit shared by inference and generated IR.
class IncrementalUnitSelection {
  public:
	using Keys = std::set<std::string>;
	using Sections = std::map<const Section *, Keys>;
	bool contains(const Section *section, const std::string &key) const {
		auto found = selected.find(section);
		return found != selected.end() && found->second.contains(key);
	}
	void insert(const Section *section, const std::string &key) { selected[section].insert(key); }
	void erase(const Section *section, const std::string &key) {
		auto found = selected.find(section);
		if (found != selected.end()) {
			found->second.erase(key);
			if (found->second.empty())
				selected.erase(found);
		}
	}
	const Keys &keys(const Section *section) const {
		auto found = selected.find(section);
		return found == selected.end() ? emptyKeys : found->second;
	}
	const Sections &sections() const { return selected; }
	bool empty() const { return selected.empty(); }

  private:
	Sections selected;
	Keys emptyKeys;
};
