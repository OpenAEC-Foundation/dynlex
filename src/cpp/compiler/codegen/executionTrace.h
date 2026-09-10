#pragma once
#include "parseContext.h"

namespace llvm {
class Value;
}

// Runtime source events for the input document. Imported implementation details
// remain outside the trace; locations always refer to authored source slices.
void emitExecutionTrace(ParseContext &context, const Range &range, llvm::Value *condition = nullptr);
