#pragma once
#include <string>
struct InferenceContext;
struct ClassDefinition;
void recordInferenceClassRestoration(InferenceContext *context, ClassDefinition *definition);
void recordInferenceClassRequestRestoration(InferenceContext *context, ClassDefinition *definition, const std::string &key);
