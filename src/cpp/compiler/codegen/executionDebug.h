#pragma once
#include "parseContext.h"

void emitDebugLocation(ParseContext &context, const Range &range);
void finalizeDebugInstrumentation(ParseContext &context);
