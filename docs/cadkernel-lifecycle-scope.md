# Lifecycle variable scope

Retain and release bodies generate separate functions. Their local declarations
must therefore stop at the lifecycle body, just as ordinary function locals stop
at their function. Previously variable resolution recognized only non-flex
function sections as boundaries. A hook-local name could bind to a same-named
top-level variable; this produced either incorrect global mutation or LLVM code
that used another function's stack allocation.

The shared boundary predicate now covers ordinary non-flex functions, retain
bodies and release bodies. Explicit lookup, implicit declaration grouping and
global classification use that boundary. Implicit lookup includes the boundary
itself before stopping, since lifecycle bodies can contain declarations directly.

Four new required fixtures cover implicit names, explicit declarations, nested
loops and flex bodies, and top-level globals used by ordinary functions. Together
with six existing globals/lifecycle fixtures they pass **20 O0/O2 combinations**.
The repaired code also lets the NURBS curve/surface and ownership fixtures run.
Independent source review found no additional defect in the scope change.

The no-import reproduction is
`tests/cadkernel/nurbs3/lifecycle_scope_direct.dl`.
It previously produced invalid LLVM IR referring to the main function's
allocation from a release function. Compiler verification remains enabled;
there is no workaround in the NURBS implementation. The complete compiler
suite still needs to pass after all compiler repairs are integrated.
