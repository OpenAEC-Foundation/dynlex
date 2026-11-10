Json optionIdentity(const ParseContext::Options &options) {
	auto optionalBool = [](std::optional<bool> value) -> Json {
		return value ? Json(*value) : Json(nullptr);
	};
	llvm::StringMap<bool> hostFeatures = llvm::sys::getHostCPUFeatures();
	std::map<std::string, bool> sortedFeatures;
	for (const auto &feature : hostFeatures)
		sortedFeatures[feature.first().str()] = feature.second;
	return Json{
		{"input", options.inputPath},
		{"cwd", fs::current_path().string()},
		{"no_main", options.noMain},
		{"optimization", options.optimizationLevel},
		{"size", static_cast<int>(options.optimizationSize)},
		{"fast_math", options.fastMath},
		{"contract", static_cast<int>(options.floatingPointContract)},
		{"vectorize", optionalBool(options.loopVectorization)},
		{"slp", optionalBool(options.slpVectorization)},
		{"unroll", optionalBool(options.loopUnrolling)},
		{"cpu", options.targetCPU},
		{"tune", options.targetTuneCPU},
		{"features", options.targetFeatures},
		{"explicit_target", options.hasExplicitTargetConfiguration},
		{"debug", options.emitDebugInfo},
		{"trace", options.traceExecution},
		{"resolution_limit", options.maxResolutionIterations},
		{"host_cpu", llvm::sys::getHostCPUName().str()},
		{"host_features", sortedFeatures}
	};
}
