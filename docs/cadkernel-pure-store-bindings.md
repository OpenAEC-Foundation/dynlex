# Pure evaluation of forwarded assignments

An assignment nested through replacement patterns must reach the same caller
variable during compile-time execution as during native execution. A multiplication
wrapper that forwards to a setter exposed a discrepancy: the evaluator stored
the new value in the setter's enclosing argument, while the loop condition read
the original caller variable. The loop therefore never advanced.

The global evaluation budget correctly rejected that execution. The previous
local loop cutoff had hidden the wrong assignment by ending the loop and
allowing a partial value to escape. The existing `pattern` required fixture
then exposed the defect after the budget correction.

The Store evaluator now resolves its destination with
`resolveThroughBindingLayers` and the already selected flex expansion. This is
the shared traversal also used by purity classification and code generation.
It follows caller scopes through all forwarded bindings, without selecting new
overloads or changing the original stack used to evaluate the right-hand side.
Ordinary expression reads retain their existing recursive evaluation.

`pure_execution_nested_store` is a no-import regression: a nested multiply/set
loop computes eight, which determines a fixed array's extent. The native exit
status checks that extent. Thus correct runtime calculation cannot conceal an
incorrect compile-time constant.

Validation:

- The new regression failed with the step-limit diagnostic before repair and
  passes at O0/O2 after repair.
- The existing `pattern` program, its explicitly grouped form and its separated
  assignment form compile and produce the original output at both levels.
- All 68 focused pure/recursive fixture-mode combinations pass.
- Independent review found no defect in the Store change. Twenty additional
  compile/run combinations cover deeper forwarding, reversed argument positions,
  swap temporaries, separate call frames, caller-body scopes and trial rollback.

The review separately found pre-existing problems with repeated pure caller-body
execution. Those require their own regression coverage; successful Store checks
do not establish their correctness.
