---
paths:
  - "src/compiler/compiler.cpp"
  - "src/compiler/patternResolution.cpp"
  - "src/compiler/typeInference.cpp"
  - "src/compiler/section/**"
---

# Variable Scoping & Globals

## Function-boundary scoping
- Variables are local to their function scope by default
- Phase 4 (variable resolution) stops grouping at non-flex Expression/Effect sections
- Variables in nested blocks (loops, if) still access parent function's variables

## Global variables
- Declared via `globals:` section (inline or block form)
- Stored as `globalVariables` on Section, `declaredGlobalVariables` set on ParseContext
- LLVM: `@name = internal global <type> zeroinitializer`
- Stored via `reinterpret_cast<AllocaInst*>(globalVar)` in `varDef->alloca` so existing codegen paths work

## Bugs Fixed
- **Cross-function grouping**: Same-named vars in different functions merged. Fix: stop parent-chain walk at function boundaries unless global.
- **variableDefinitions in wrong section**: Used earliest reference's section. Fix: use `highestSection->variableDefinitions[name]`.
