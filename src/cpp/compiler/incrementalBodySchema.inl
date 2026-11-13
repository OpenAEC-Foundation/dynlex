// Shared positional schema for typed expression records. The names are stored
// once per body so tools can inspect a snapshot without compiler internals.
#define DYNLEX_INFERENCE_EXPRESSION_FIELDS(X)                                                                                  \
	X(Reference, "reference")                                                                                                  \
	X(Kind, "kind")                                                                                                            \
	X(Type, "type")                                                                                                            \
	X(Value, "value")                                                                                                          \
	X(Memory, "memory")                                                                                                        \
	X(Range, "range")                                                                                                          \
	X(Literal, "literal")                                                                                                      \
	X(Variable, "variable")                                                                                                    \
	X(Declaration, "declaration")                                                                                              \
	X(Data, "data")                                                                                                            \
	X(DataValue, "data_value")                                                                                                 \
	X(Deferred, "deferred")                                                                                                    \
	X(Owner, "owner")                                                                                                          \
	X(Match, "match")                                                                                                          \
	X(Definition, "definition")                                                                                                \
	X(Path, "path")                                                                                                            \
	X(Callable, "callable")                                                                                                    \
	X(CallablePath, "callable_path")                                                                                           \
	X(Subject, "subject")                                                                                                      \
	X(Capture, "capture")                                                                                                      \
	X(Instance, "instance")                                                                                                    \
	X(Expansion, "expansion")                                                                                                  \
	X(Pointer, "pointer")                                                                                                      \
	X(Flex, "flex")                                                                                                            \
	X(Caller, "caller")                                                                                                        \
	X(Loops, "loops")                                                                                                          \
	X(Outcome, "outcome")                                                                                                      \
	X(FallsThrough, "falls_through")                                                                                           \
	X(Reachable, "reachable")                                                                                                  \
	X(Execution, "execution")                                                                                                  \
	X(BodyFallsThrough, "body_falls_through")                                                                                  \
	X(Branch, "branch")                                                                                                        \
	X(Template, "template")                                                                                                    \
	X(Intrinsic, "intrinsic")                                                                                                  \
	X(Arguments, "arguments")                                                                                                  \
	X(Submatch, "submatch")                                                                                                    \
	X(Grouped, "grouped")                                                                                                      \
	X(GroupIndices, "group_indices")                                                                                           \
	X(Adjacent, "adjacent")                                                                                                    \
	X(StartsArgument, "starts_argument")                                                                                       \
	X(EndsArgument, "ends_argument")

enum class InferenceBodyExpressionField : size_t {
#define DYNLEX_INFERENCE_EXPRESSION_ENUM(name, text) name,
	DYNLEX_INFERENCE_EXPRESSION_FIELDS(DYNLEX_INFERENCE_EXPRESSION_ENUM)
#undef DYNLEX_INFERENCE_EXPRESSION_ENUM
		Count
};
constexpr std::array<std::string_view, static_cast<size_t>(InferenceBodyExpressionField::Count)> inferenceBodyExpressionFields{
#define DYNLEX_INFERENCE_EXPRESSION_NAME(name, text) text,
	DYNLEX_INFERENCE_EXPRESSION_FIELDS(DYNLEX_INFERENCE_EXPRESSION_NAME)
#undef DYNLEX_INFERENCE_EXPRESSION_NAME
};
#undef DYNLEX_INFERENCE_EXPRESSION_FIELDS
