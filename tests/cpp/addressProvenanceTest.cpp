#include "compiler/compiler.h"
#include "compiler/variableReference.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>

namespace {

void expect(bool condition, std::string_view message) {
	if (condition)
		return;
	std::cerr << message << '\n';
	std::exit(1);
}

void checkCompilation(std::string_view body, std::string_view diagnostic = {}) {
	std::string source = R"(
integer means: @intrinsic("type", "int")
pointer type means: @intrinsic("add pointer depth", integer)
null pointer means: @intrinsic("cast", 0, pointer type)
)";
	source += body;
	ParseContext context;
	auto files = std::make_unique<lsp::MemoryFileSystem>();
	files->setFile("memory_diagnostics.dl", source);
	context.fileSystem = std::move(files);
	bool compiled = compile("memory_diagnostics.dl", context);
	if (compiled != diagnostic.empty()) {
		context.printDiagnostics();
		expect(false, std::string("wrong compilation outcome in fixture:\n") + std::string(body));
	}
	if (!diagnostic.empty()) {
		expect(context.diagnostics.size() == 1, "memory violation did not produce exactly one diagnostic");
		expect(context.diagnostics.front().message == diagnostic, "memory violation produced the wrong diagnostic");
	}
}

void checkFacts(
	std::string_view body, bool nullable, bool unknown, std::unordered_set<std::string> targets, bool staticStorage = false
) {
	std::string source = R"(
integer means: @intrinsic("type", "int")
pointer type means: @intrinsic("add pointer depth", integer)
null pointer means: @intrinsic("cast", 0, pointer type)
fixed integer means: @intrinsic("fix", integer)
function set variable to value:
    replacement:
        @intrinsic("store", variable, value)
function return value:
    replacement:
        @intrinsic("return", value)
function discard value:
    replacement:
        @intrinsic("discard", value)
function the pointer from {fixed integer:value}:
    execute:
        return @intrinsic("cast", value, pointer type)
class:
    patterns:
        slot holder
    members:
        slot as pointer type
flex section if condition:
    replacement:
        @intrinsic("if", condition)
flex section else:
    replacement:
        @intrinsic("else")
flex section loop while condition:
    replacement:
        @intrinsic("loop while", condition)
function probe condition:
    execute:
        discard condition
)";
	source += body;
	source += "\ndiscard probe @intrinsic(\"equal\", 1, 1)\n";
	ParseContext context;
	auto files = std::make_unique<lsp::MemoryFileSystem>();
	files->setFile("pointer_facts.dl", source);
	context.fileSystem = std::move(files);
	if (!compile("pointer_facts.dl", context)) {
		context.printDiagnostics();
		expect(false, "pointer-fact fixture failed to compile");
	}
	const Instantiation *probe = nullptr;
	auto visit = [&](auto &&self, Section *section) -> void {
		for (const auto &[key, instance] : section->instantiations) {
			if (key.argumentTypes.size() == 1 && key.argumentTypes.front().kind == DataType::Kind::Bool &&
				instance.returnType.isPointer()) {
				expect(probe == nullptr, "fixture contains more than one probe instantiation");
				probe = &instance;
			}
		}
		for (Section *child : section->children)
			self(self, child);
	};
	visit(visit, context.mainSection);
	expect(probe && probe->valid && probe->hasReturnAddressProvenance, "probe has no inferred pointer facts");
	const AddressProvenance &facts = probe->returnAddressProvenance;
	expect(facts.mayBeNull == nullable, std::string("wrong null possibility in fixture:\n") + std::string(body));
	expect(
		facts.mayBeStatic == staticStorage, std::string("wrong static-storage possibility in fixture:\n") + std::string(body)
	);
	expect(facts.unknown == unknown, std::string("wrong unknown-origin possibility in fixture:\n") + std::string(body));
	std::unordered_set<std::string> actualTargets;
	for (VariableReference *target : facts.mayTargets)
		actualTargets.insert(target->name);
	expect(actualTargets == targets, std::string("wrong storage targets in fixture:\n") + std::string(body));
}

} // namespace

int main() {
	constexpr std::string_view nullDiagnostic = "cannot dereference a null pointer";
	checkCompilation("@intrinsic(\"discard\", @intrinsic(\"dereference\", null pointer))\n", nullDiagnostic);
	checkCompilation(
		"@intrinsic(\"discard\", @intrinsic(\"dereference\", @intrinsic(\"construct\", pointer type)))\n", nullDiagnostic
	);
	checkCompilation("@intrinsic(\"store at\", null pointer, 1)\n", nullDiagnostic);
	checkCompilation("@intrinsic(\"initialize at\", null pointer, 1)\n", nullDiagnostic);
	checkCompilation("@intrinsic(\"discard\", @intrinsic(\"atomic load\", null pointer, \"relaxed\"))\n", nullDiagnostic);
	checkCompilation("@intrinsic(\"call pointer\", null pointer, @intrinsic(\"type\", \"nothing\"))\n", nullDiagnostic);
	checkCompilation(
		R"(
@intrinsic("store", pointer, null pointer)
@intrinsic("store", link, @intrinsic("address of", pointer))
@intrinsic("discard", @intrinsic("dereference", @intrinsic("dereference", link)))
)",
		nullDiagnostic
	);
	checkCompilation(
		R"(
class:
    patterns:
        holder
    members:
        value as integer
@intrinsic("store", pointer, @intrinsic("cast", 0, @intrinsic("add pointer depth", holder)))
@intrinsic("discard", @intrinsic("property", pointer, "value"))
)",
		nullDiagnostic
	);
	checkCompilation(
		R"(
function exercise:
    execute:
        @intrinsic("store", value, 0)
        @intrinsic("check deallocation", @intrinsic("address of", value))
exercise
)",
		"cannot deallocate stack storage"
	);
	checkCompilation(
		"@intrinsic(\"check deallocation\", \"static literal\")\n", "cannot deallocate static or variable storage"
	);
	checkCompilation("@intrinsic(\"check deallocation\", null pointer)\n");
	checkCompilation(R"(
@intrinsic("store", pointer, @intrinsic("call", "libc", "malloc", pointer type, 4))
@intrinsic("check deallocation", pointer)
@intrinsic("discard", @intrinsic("dereference", pointer))
)");
	checkCompilation(R"(
function exercise condition:
    execute:
        @intrinsic("store", value, 1)
        @intrinsic("store", pointer, @intrinsic("select", condition, null pointer, @intrinsic("address of", value)))
        @intrinsic("check deallocation", pointer)
        @intrinsic("discard", @intrinsic("dereference", pointer))
exercise @intrinsic("equal", 1, 1)
)");
	checkCompilation(R"(
@intrinsic("store", pointer, null pointer)
@intrinsic("discard", @intrinsic("call", "libc", "opaque", integer, @intrinsic("address of", pointer)))
@intrinsic("discard", @intrinsic("dereference", pointer))
)");
	checkCompilation("@intrinsic(\"discard\", @intrinsic(\"address of\", @intrinsic(\"dereference\", null pointer)))\n");
	checkCompilation("@intrinsic(\"discard\", @intrinsic(\"type of\", @intrinsic(\"dereference\", null pointer)))\n");
	checkCompilation(R"(
the value at address means: @intrinsic("dereference", address)
the type of value means: @intrinsic("type of", value)
the address of value means: @intrinsic("address of", value)
@intrinsic("discard", the type of the value at null pointer)
@intrinsic("discard", the address of the value at null pointer)
)");
	checkCompilation(
		R"(
the value at address means: @intrinsic("dereference", address)
flex function consume value:
    replacement:
        @intrinsic("discard", value)
consume the value at null pointer
)",
		nullDiagnostic
	);
	checkCompilation(R"(
function invalid value:
    execute:
        @intrinsic("return", @intrinsic("dereference", null pointer))
@intrinsic("discard", @intrinsic("type of", invalid value))
)");
	checkCompilation(
		R"(
function invalid value:
    execute:
        @intrinsic("return", @intrinsic("dereference", null pointer))
@intrinsic("discard", @intrinsic("type of", invalid value))
@intrinsic("discard", invalid value)
)",
		nullDiagnostic
	);
	checkCompilation(R"(
flex function invalid value:
    replacement:
        @intrinsic("discard", @intrinsic("dereference", null pointer))
        1
@intrinsic("discard", @intrinsic("type of", invalid value))
)");
	checkCompilation(
		R"(
flex function invalid value:
    replacement:
        @intrinsic("discard", @intrinsic("dereference", null pointer))
        1
@intrinsic("discard", invalid value)
)",
		nullDiagnostic
	);
	checkCompilation(
		"@intrinsic(\"discard\", @intrinsic(\"address of\", @intrinsic(\"dereference\", "
		"@intrinsic(\"dereference\", @intrinsic(\"cast\", 0, @intrinsic(\"add pointer depth\", pointer type))))))\n",
		nullDiagnostic
	);
	checkCompilation(R"(
function exercise value:
    execute:
        @intrinsic("check deallocation", @intrinsic("address of", value))
@intrinsic("store", pointer, @intrinsic("call", "libc", "malloc", pointer type, 4))
exercise @intrinsic("dereference", pointer)
)");
	checkCompilation("@intrinsic(\"discard\", @intrinsic(\"dereference\", @intrinsic(\"cast\", 1, pointer type)))\n");
	checkCompilation("@intrinsic(\"destroy at\", null pointer)\n");
	checkCompilation("@intrinsic(\"store at\", \"literal\", 1)\n", "cannot write to static literal storage");
	checkCompilation(R"(
wide integer means: @intrinsic("type", "int", 64)
@intrinsic("store", value, 0)
@intrinsic("store", encoded, @intrinsic("cast", @intrinsic("address of", value), wide integer))
@intrinsic("store", cleared, @intrinsic("bitwise and", encoded, 0))
@intrinsic("check deallocation", @intrinsic("cast", cleared, pointer type))
)");
	checkCompilation(R"(
wide integer means: @intrinsic("type", "int", 64)
@intrinsic("store", value, 0)
@intrinsic("store", encoded, @intrinsic("cast", @intrinsic("address of", value), wide integer))
@intrinsic("store", cleared, @intrinsic("subtract", encoded, encoded))
@intrinsic("check deallocation", @intrinsic("cast", cleared, pointer type))
)");
	checkCompilation(R"(
narrow integer means: @intrinsic("type", "int", 8)
@intrinsic("store", value, 0)
@intrinsic("store", narrowed, @intrinsic("cast", @intrinsic("address of", value), narrow integer))
@intrinsic("check deallocation", @intrinsic("cast", narrowed, pointer type))
)");
	checkCompilation(R"(
function the address of value:
    execute:
        @intrinsic("return", @intrinsic("address of", value))
@intrinsic("store", pointer, @intrinsic("call", "libc", "malloc", pointer type, 4))
@intrinsic("check deallocation", the address of @intrinsic("dereference", pointer))
)");
	checkCompilation(R"(
function the address of value:
    execute:
        @intrinsic("return", @intrinsic("address of", value))
@intrinsic("check deallocation", the address of @intrinsic("dereference", null pointer))
)");
	checkCompilation(R"(
class:
    patterns:
        holder
    members:
        slot as pointer type
        number as integer
@intrinsic("store", value, @intrinsic("construct", holder))
@intrinsic("store", @intrinsic("property", value, "number"), 1)
@intrinsic("discard", @intrinsic("dereference", @intrinsic("cast", @intrinsic("property", value, "number"), pointer type)))
)");
	checkCompilation(R"(
@intrinsic("store", value, 1)
@intrinsic("discard", @intrinsic("dereference", @intrinsic("address of", value)))
)");
	checkFacts("        set pointer to null pointer\n        return pointer\n", true, false, {});
	checkFacts("        set value to 0\n        return @intrinsic(\"address of\", value)\n", false, false, {"value"});
	checkFacts(
		R"(        set value to 0
        set pointer to null pointer
        if condition:
            set pointer to @intrinsic("address of", value)
        return pointer
)",
		true, false, {"value"}
	);
	checkFacts(
		R"(        set value to 0
        set pointer to @intrinsic("select", condition, null pointer, @intrinsic("address of", value))
        set alias to pointer
        return alias
)",
		true, false, {"value"}
	);
	checkFacts(
		R"(        set first to 0
        set second to 0
        set pointer to null pointer
        if condition:
            set pointer to @intrinsic("address of", first)
        else:
            set pointer to @intrinsic("address of", second)
        return pointer
)",
		false, false, {"first", "second"}
	);
	checkFacts(
		R"(        set value to 0
        set pointer to @intrinsic("address of", value)
        set pointer to null pointer
        return pointer
)",
		true, false, {}
	);
	checkFacts(
		R"(        set pointer to null pointer
        set link to @intrinsic("address of", pointer)
        return @intrinsic("dereference", link)
)",
		true, false, {}
	);
	checkFacts(
		R"(        set value to 0
        set pointer to null pointer
        loop while condition:
            set pointer to @intrinsic("address of", value)
            set condition to @intrinsic("equal", 0, 1)
        return pointer
)",
		true, false, {"value"}
	);
	checkFacts(
		R"(        set pointer to @intrinsic("call", "libc", "malloc", pointer type, 4)
        return pointer
)",
		false, true, {}
	);
	checkFacts(
		R"(        set pointer to null pointer
        discard @intrinsic("call", "libc", "opaque", integer, @intrinsic("address of", pointer))
        return pointer
)",
		false, true, {}
	);
	checkFacts("        return @intrinsic(\"construct\", pointer type)\n", true, false, {});
	checkFacts("        return the pointer from 0\n", true, false, {});
	checkFacts("        return the pointer from 1\n", false, true, {});
	checkFacts(
		R"(        set holder to @intrinsic("construct", slot holder)
        return @intrinsic("property", holder, "slot")
)",
		true, false, {}
	);
	checkFacts(
		R"(        set value to 0
        set pointer to null pointer
        set destination to @intrinsic("select", condition, @intrinsic("cast", 0, @intrinsic("add pointer depth", pointer type)), @intrinsic("address of", pointer))
        @intrinsic("store at", destination, @intrinsic("address of", value))
        return pointer
)",
		true, false, {"value"}
	);
	checkFacts("        return @intrinsic(\"add\", null pointer, 0)\n", true, false, {});
	checkFacts("        return @intrinsic(\"add\", null pointer, 1)\n", false, true, {});
	checkFacts("        return \"literal\"\n", false, false, {}, true);
	checkFacts(
		R"(        return @intrinsic("select", condition, @intrinsic("cast", 0, @intrinsic("type", "string")), "literal")
)",
		true, false, {}, true
	);
	return 0;
}
