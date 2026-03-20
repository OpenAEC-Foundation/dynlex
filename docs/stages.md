this document explains how the compiler is supposed to behave.

all code should be as DRY, agnostic, user friendly and performant as possible. do more with less code.
when we encounter an error, we add an error diagnostic and return false if this error could cause dependent errors. when a child function returns false, return false as well. this will make the compiler exit cleanly with a single diagnostic. we don't continue scanning for other diagnostics, because one error will cause lots of other errors most of the times and will make it unclear for users what they need to focus on to fix.

later stages of the compiler are very dependent on earlier stages. every line of code has to be carefully thought out.

# import stage

the compiler combines all files to one large file.

# parse stage

sections are analyzed. we do basic parsing WITHOUT hardcoding. which line opens a new section? what patterns does each section have?

# pattern matching stage

patterns are matched. here we identify:
- what's a variable
- what's an argument
we match with multiple iterations. this will make sure that patterns earlier in the file can call functions later in the file.
we discover what's a variable based on these principles:
- a single word functions pattern is never a variable. therefore, all functions with single word patterns are parsed in the first round.
- a single word as argument to an intrinsic is always a variable, unless it references a single word function. since functions are parsed before references to them, we are guaranteed that single word functions exist from the start.
we use this logic to determine what's a variable and what not, all the way from the simplest intrinsics to the most complex functions.
- alphanumeric strings in argument positions of pattern calls are variables.

the consequence: an unused argument isn't an argument.

pattern matching is type agnostic. this is because we can't easily match based on types if we don't even know if a variable exists, yet. and because variables come from the callee to the caller, (the function signature defines what's a variable), while types come from the caller to the callee (the arguments define the type).

the consequence: we can't know what order a nested expressions should have.
example:
print x as line
we don't know that 'print x' returns void and cannot be used as argument for 'as line'.
since we are fully agnostic, we will make all left expressions subexpressions:
(print x) as line.
((x + x) + x) + x.

# type resolution stage

we loop over the code like it would get executed.
we track each variable that would possibly be a constant. a variable reference can be constant. constant means compile time evaluated here. it doesn't guarantee that the value doesn't change, later.
we can reorder expressions based on types, but we cannot change what's a variable and what not.

we only go over loops once. variables modified in there are marked as non-constant.

(print x) as line is incorrect, since void as argument is not allowed unless explicitly specified in the pattern and print x returns void. we know this because we expand print x, which is a macro pattern, to the intrinsic("print") and that intrinsic returns void. when encountering a non-macro function, we do the same, but instead of expanding it, we instantiate it and walk over the code just like we do with the code in the main section. we store the return type of non-macro functions so we don't have to instantiate functions with the same (possibly incorrect) combinations again and again. we assume non-macro functions always return the same type for the same argument types and constants.

all instantiations of a function have the same operand reordering for each code line, but can use different overloads. the first valid instantiation determines reordering.

## operand reordering

we iterate over all possible operand orders until we find a valid one.

all instantiation types are known when instantiating a function and do not change. we use this fact for reordering.
the only exception is the store intrinsic: it takes a value whose first arguments type should be determined by its second arguments type. this only affects macros.

we prefer depth-first, just like pattern matching.


a functions return value may never be unused. this prevents wrong groupings. if we want to discard a functions return value, we can use a discard intrinsic.

enclosed expressions like '1 + 4' in 'the minimum of 1 + 4 and 5 + 6' are inferred first. when an ordering in the parent expression fails, different orderings in enclosed expressions are tried.

we don't clone the expression tree for reordering, but reorder it. even when storing correct state and continuing to search for the next valid state so we can give ambiguity warnings, we store our choices instead of cloning the expression tree.
we don't use pointers to expression locations, since those can be dangling pointers.

some orderings are ambigous.
for example:
the maximum of 5 and 3 + 4. did the user mean '(the maximum of 5 and 3) + 4' or 'the maximum of 5 and (3 + 4)'? both parse correctly. because of the left expression = subexpression nature, the compiler will choose the first one. this behaviour might cause glitches. but even humans will not be able to tell without context. we'd like the user to specify which option to choose.

# code generation stage

we already know which patterns call which instantiations, the type of every variable etc. but now, we branch off into compilation target: browser, machine code, spirv etc.
