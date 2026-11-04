#include "compiler/compiler.h"
#include "compiler/expression.h"
#include "compiler/section/classSection.h"
#include <cstdlib>
#include <functional>
#include <iostream>
#include <unordered_set>

static void expect(bool condition, const char *message) {
	if (!condition) {
		std::cerr << message << '\n';
		std::exit(1);
	}
}

static void checkBodyOwnership() {
	ParseContext context;
	Section section(SectionType::Function);
	CodeLine line({}, nullptr);
	Expression root, left, right;
	root.arguments = {&left, &right};
	line.expression = &root;
	section.codeLines.push_back(&line);
	auto body = context.cloneSectionBody(&section);
	expect(context.expressionAllocationStats->liveExpressions == 3, "body did not own its three clones");
	// Regrouping can detach and share argument edges; ownership must not
	// depend on walking the final argument tree.
	body->lineExpressions.front()->arguments.clear();
	std::weak_ptr<ExpressionStorage> oldStorage = body->expressions;
	Expression call;
	call.inferredFlexBody = std::move(body);
	call.inferredFlexBody = context.cloneSectionBody(&section);
	expect(oldStorage.expired(), "replacing a flex body retained the old clones");
	expect(context.expressionAllocationStats->liveExpressions == 3, "detached clones were not reclaimed");
	Instantiation instantiation;
	instantiation.body = call.inferredFlexBody;
	call.inferredFlexBody.reset();
	expect(context.expressionAllocationStats->liveExpressions == 3, "shared body was freed before its last user");
	instantiation.body.reset();
	expect(context.expressionAllocationStats->liveExpressions == 0, "last body user did not release its clones");
}

static void checkSectionResultOwnership() {
	std::shared_ptr<ExpressionAllocationStats> stats;
	{
		ParseContext context;
		stats = context.expressionAllocationStats;
		auto files = std::make_unique<lsp::MemoryFileSystem>();
		files->setFile("section-result-ownership.dl", R"(
integer means: @intrinsic("type", "integer")
section answer {name}:
    declares: name
    result: integer
    value:
        42
answer example:
@intrinsic("discard", example)
)");
		context.fileSystem = std::move(files);
		if (!compile("section-result-ownership.dl", context)) {
			context.printDiagnostics();
			expect(false, "section result ownership fixture did not infer");
		}
	}
	expect(stats->liveExpressions == 0, "section result inference leaked clones");
}

static void checkInferenceOwnership() {
	std::string source = R"(
integer means: @intrinsic("type", "int")
function set variable to value:
    replacement:
        @intrinsic("store", variable, value)
function return value:
    replacement:
        @intrinsic("return", value)
value + other means: @intrinsic("add", value, other)
the inner value of operand means: operand + 1
the outer value of operand means: the inner value of (the inner value of operand)
function discard value:
    replacement:
        @intrinsic("discard", value)
to get the result for {integer:value}:
    return the outer value of value
nothing means: @intrinsic("type", "nothing")
function publish value:
    replacement:
        @intrinsic("set subject", value)
it means: @intrinsic("subject")
flex function {nothing:first} and {nothing:second}:
    replacement:
        @intrinsic("discard", first)
        @intrinsic("discard", second)
flex section while condition:
    replacement:
        @intrinsic("loop while", condition)
value < other means: @intrinsic("less than", value, other)
class:
    patterns:
        a number box
    members:
        payload as integer
implicitly convert {integer:number}:
    replacement:
        @intrinsic("construct", a number box, number)
to get the boxed payload of {a number box:container}:
    return @intrinsic("property", container, "payload")
)";
	// Every call enters grouping trials, instantiation reuse, and nested flex
	// replacement. Separate numeric widths also exercise distinct bodies.
	for (int i = 0; i < 40; ++i)
		source += "discard (the result for " + std::to_string(i) + ")\n";
	source += "discard (the result for 5000000000)\n";
	source += "discard (the boxed payload of 7)\n";
	source += "set counter to 0\nwhile counter < 3:\n    publish counter and set captured to it\n    set counter to counter + "
			  "1\n    discard (the outer value of counter)\n";
	std::shared_ptr<ExpressionAllocationStats> stats;
	{
		ParseContext context;
		stats = context.expressionAllocationStats;
		auto files = std::make_unique<lsp::MemoryFileSystem>();
		files->setFile("ownership.dl", source);
		context.fileSystem = std::move(files);
		if (!compile("ownership.dl", context)) {
			context.printDiagnostics();
			expect(false, "ownership fixture did not infer");
		}
		std::unordered_set<ExpressionStorage *> reachableStorage;
		std::unordered_set<Expression *> visited;
		std::unordered_set<Expression *> ownedArguments;
		std::function<void(InstantiatedSectionBody *)> visitBody;
		std::function<void(Expression *)> visitExpression = [&](Expression *expression) {
			if (!expression || !visited.insert(expression).second)
				return;
			for (Expression *argument : expression->arguments)
				visitExpression(argument);
			visitBody(expression->inferredFlexBody.get());
			if (expression->ownedArgument) {
				expect(
					expression->arguments.size() == 1 && expression->arguments.front() == expression->ownedArgument.get(),
					"synthetic call lost its owned source argument"
				);
				ownedArguments.insert(expression->ownedArgument.get());
			}
			if (expression->inferredValueStorage)
				reachableStorage.insert(expression->inferredValueStorage.get());
		};
		visitBody = [&](InstantiatedSectionBody *body) {
			if (!body || !reachableStorage.insert(body->expressions.get()).second)
				return;
			for (Expression *expression : body->lineExpressions)
				visitExpression(expression);
			for (const auto &child : body->childBodies)
				visitBody(child.get());
		};
		std::function<void(Section *)> visitSection = [&](Section *section) {
			if (section->type == SectionType::Class) {
				auto *classSection = static_cast<ClassSection *>(section);
				if (classSection->classDefinition) {
					for (const auto &field : classSection->classDefinition->fields)
						visitExpression(field.declaredType.typeExpression);
				}
			}
			for (CodeLine *line : section->codeLines)
				visitExpression(line->expression);
			for (const auto &[key, instantiation] : section->instantiations)
				visitBody(instantiation.body.get());
			for (Section *child : section->children)
				visitSection(child);
		};
		visitSection(context.mainSection);
		size_t reachable = ownedArguments.size();
		for (ExpressionStorage *storage : reachableStorage)
			reachable += storage->expressions.size();
		expect(stats->liveExpressions == reachable, "inference retained clones outside the final bodies");
		expect(stats->peakExpressions > stats->liveExpressions, "fixture did not exercise temporary trial clones");
		std::cout << "reachable clones: " << reachable << ", peak: " << stats->peakExpressions << '\n';
	}
	expect(stats->liveExpressions == 0, "compilation teardown leaked clones");
}

int main() {
	checkBodyOwnership();
	checkSectionResultOwnership();
	checkInferenceOwnership();
}
