Json captureInferenceFailure(const InferenceFailure &failure, InferenceBodySources &sources) {
	auto capture = [&](const Diagnostic &diagnostic) {
		Json record = captureUnitDiagnostic(diagnostic, sources);
		for (size_t index = 0; index < diagnostic.relatedInfo.size(); index++)
			record["related"][index].push_back(diagnostic.relatedInfo[index].inferenceOwnerBase);
		return record;
	};
	Diagnostic detail;
	detail.message = failure.detail;
	detail.configuredMessage = failure.configuredDetail;
	detail.range = failure.snapshot.range;
	detail.relatedInfo = failure.relatedInfo;
	return Json{
		{"detail", capture(detail)},
		{"expression", failure.snapshot.text},
		{"diagnostic", failure.diagnostic ? capture(*failure.diagnostic) : Json(nullptr)},
		{"priority", failure.priority},
		{"traced", failure.traced}
	};
}

std::shared_ptr<const InferenceFailure> restoreInferenceFailure(
	ParseContext &context, const Json &record, InferenceBodySources &sources,
	const std::shared_ptr<const VariableStorageScope> &scope
) {
	auto restore = [&](const Json &value) {
		Diagnostic diagnostic = restoreUnitDiagnostic(context, value, sources, scope);
		for (size_t index = 0; index < diagnostic.relatedInfo.size(); index++)
			diagnostic.relatedInfo[index].inferenceOwnerBase = value.at("related").at(index).at(3).get<int>();
		return diagnostic;
	};
	auto result = std::make_shared<InferenceFailure>();
	Diagnostic detail = restore(record.at("detail"));
	result->detail = std::move(detail.message);
	result->configuredDetail = std::move(detail.configuredMessage);
	result->snapshot = {detail.range, record.at("expression").get<std::string>()};
	result->relatedInfo = std::move(detail.relatedInfo);
	if (!record.at("diagnostic").is_null())
		result->diagnostic = restore(record.at("diagnostic"));
	result->priority = record.at("priority").get<int>();
	result->traced = record.at("traced").get<bool>();
	return result;
}
