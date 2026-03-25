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

we sort all expression arguments by their source position, since they didn't get added in order. after this, NO sorting is done.

# macro expansion stage
we expand all macro's. after this stage, no macro calls are found anymore.

# validation stage

# type resolution stage

we loop over the code like it would get executed.
we track each variable that would possibly be a constant. a variable reference can be constant. constant means compile time evaluated here. it doesn't guarantee that the value doesn't change, later.
we can reorder expressions based on types if this is the first valid instantiation, but we cannot change what's a variable and what not.
ALL types of each previous line have to be deduced when right away except in recursive function code.

we only go over loops once. variables modified in there are marked as non-constant.

(print x) as line is incorrect, since void as argument is not allowed unless explicitly specified in the pattern and print x returns void. we know this because we instantiate print x and walk over the code just like we do with the code in the main section. we store the return type so we don't have to instantiate functions with the same (possibly incorrect) combinations again and again. we assume  functions always return the same type for the same argument types and constants.

all instantiations of a function have the same operand reordering for each code line, but can use different overloads. the first valid instantiation determines reordering.

## operand reordering

we iterate over all possible operand orders until we find a valid one.

all instantiation types are known when instantiating a function and do not change. we use this fact for reordering.
the only exception is the store intrinsic: it takes a value whose first arguments type should be determined by its second arguments type. therefore, a store intrinsic wrapper has to be a macro function, so it expands before types are checked.

we prefer sub-first aka left = subexpression, just like pattern matching.


a functions return value may never be unused. this prevents wrong groupings. if we want to discard a functions return value, we can use a discard intrinsic.

enclosed expressions like '1 + 4' in 'the minimum of 1 + 4 and 5 + 6' are inferred first. when an ordering in the parent expression fails, different orderings in enclosed expressions are tried.

so:
(x + y squared) * z + 2
initial sub-first order:
(((x + y) squared) * z) + 2
inner increment:
((x + (y squared)) * z) + 2
all inner orderings have been tried. all inner orderings reset. outer increment:
((x + y) squared) * (z + 2)
inner increment:
(x + (y squared)) * (z + 2)

with multiple separate sub expressions, iterate over 1 first, reset 1 and step 2, iterate 1 again, step 2 again, when 2 is finished reset both and do the same with the outer expression.

so outer innerright innerleft
000
001
010
011
100
101
etc.

for every increment, we need to reset and revalidate the whole expression tree. this is because some intrinsics have side effects. for example, the store intrinsic has a side effect:
'set x to y and print x' <-- second x's type isn't know at first and should be set by the load intrinsic.

therefore, we sadly can't just use a recursive 'increment until next valid option'

therefore, we have to separate increments, validation and clearing.
an increment changes a grouping somewhere (or potentially multiple groupings, as long as groupings have finished iterating).
validation infers the whole tree recursively
clearing clears all types from the tree recursively.

to detect ambiguity, we have to keep incrementing until we found another fully passing tree or we finished. when encountering the first valid state, we save this state by saving the expression pointers.

we don't clone the expression tree for reordering, but reorder it. even when storing correct state and continuing to search for the next valid state so we can give ambiguity warnings, we store our choices instead of cloning the expression tree.
we don't use pointers to expression pointers locations (expression**), since those can be dangling pointers.

ALL functions can be seen as one of these 4 categories of operators.
the ONLY thing we check for this is does the operator START with an argument? and does it END with an argument?
all other arguments are IRRELEVANT. we can never start or end with 2 arguments, since '$$' would just merge to one name when the user supplies two concatenated names 'arg1' and 'arg2' as 'arg1arg2'. ' ' is a SEPARATOR, a pattern element of type 'other'.
prefix operator:
not $, set $ to $, vector of $ $ #
postfix operator:
$ doubled, $%
infix operator:
$ + $
other operator:
true, a $ bit integer

some orderings are ambigous.
for example:
the maximum of 5 and 3 + 4. did the user mean '(the maximum of 5 and 3) + 4' or 'the maximum of 5 and (3 + 4)'? both parse correctly. because of the left expression = subexpression nature, the compiler will choose the first one. this behaviour might cause glitches. but even humans will not be able to tell without context. we'd like the user to specify which option to choose.

# code generation stage

we already know which patterns call which instantiations, the type of every variable etc. but now, we branch off into compilation target: browser, machine code, spirv etc.
