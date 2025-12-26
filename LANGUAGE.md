# 3BX
3bx is a language defined to help humans understand code better and agents code better.

almost all syntax is defined in the language itself. the compiler should focus on context.

indents are done with either tabs or spaces. a source file has either a fixed amount of tabs or spaces per indent.

3bx is not aimed to 'satisfy the compiler'. still, the compiler has to figure out every little detail, like variables and the type of each variable.

Code should use as less special charachters as possible (so the custom patterns can use them), but here are some:
':' indicates a section.
'#' indicates a comment.
" are string starts and ends.

to see how well the compiler is doing, use the run_tests.sh. for now it isn't bad if the tests fail, but implement the missing features one by one.

DO NOT modify any tests in the required folder. you may modify all files in the tests/agents folder to test out things.

there are some basic syntax sections. they add new features to the language:

expression: can be set, get, added to etc.
event: defines an event which will trigger all event listeners when triggered
effect: executes code.

we also have usage sections, which do not add any syntax but use them. they will be used the most.
condition: when the expression before the ":" evaluates to true, the section will be executed.
loop: the section will be executed multiple times depending on what the loop does.

patterns are built up using required and optional segments.
below are some examples of combinations a pattern can evaluate to.

print [a|the] message:
print message
print a message
print the message

the pattern tree should join branches after optional segments again to avoid having 200 branches for one pattern with 8 optional segments. when another pattern also uses the same branch, it should split them. else issues like these occur:

give [some] bread to dave
give some bread to jane <-- no optional segment! but since the tree wired the end of [some] back to the end of give, "give bread to jane" will compile.

in that case, split the branches, so duplicate 'bread to dave' to the 'some' branch.

missing features can be added:
import 