bool initializeTargetLayout(ParseContext &context) {
	requireCompilerInvariant(!context.llvmContext && !context.llvmModule, "target layout was initialized twice");
	context.llvmContext = new llvm::LLVMContext();
	context.llvmModule = new llvm::Module("dynlex_module", *context.llvmContext);
#ifdef DYNLEX_WEB
	std::string targetError;
	std::unique_ptr<llvm::TargetMachine> targetMachine = createWASMTargetMachine(context, targetError);
	if (!targetMachine) {
		context.addDiagnostic(
			Diagnostic(context, Diagnostic::Level::Error, "wasm target not available", Range(), "error", targetError)
		);
		return false;
	}
	context.llvmModule->setDataLayout(targetMachine->createDataLayout());
#else
	if (context.options.emitSPIRV) {
		std::string error;
		std::unique_ptr<llvm::TargetMachine> targetMachine = createSPIRVTargetMachine(context, error);
		if (!targetMachine) {
			context.addDiagnostic(
				Diagnostic(context, Diagnostic::Level::Error, "spirv target not available", Range(), "error", error)
			);
			return false;
		}
		context.llvmModule->setDataLayout(targetMachine->createDataLayout());
	} else if (context.options.emitWASM) {
		std::string error;
		std::unique_ptr<llvm::TargetMachine> targetMachine = createWASMTargetMachine(context, error);
		if (!targetMachine) {
			context.addDiagnostic(
				Diagnostic(context, Diagnostic::Level::Error, "wasm target not available", Range(), "error", error)
			);
			return false;
		}
		context.llvmModule->setDataLayout(targetMachine->createDataLayout());
	} else {
		std::string error;
		std::unique_ptr<llvm::TargetMachine> targetMachine = createNativeTargetMachine(context, error);
		if (!targetMachine) {
			context.addDiagnostic(
				Diagnostic(context, Diagnostic::Level::Error, "native target not available", Range(), "error", error)
			);
			return false;
		}
		context.llvmModule->setDataLayout(targetMachine->createDataLayout());
	}
#endif
	return true;
}

bool generateCode(ParseContext &context) {
	requireCompilerInvariant(
		context.llvmContext && context.llvmModule && !context.llvmBuilder,
		"code generation requires one initialized target layout"
	);
	context.llvmBuilder = new llvm::IRBuilder<>(*context.llvmContext);

	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	if (context.flexBindingFrames.empty())
		context.flexBindingFrames.pushFrame(BindingFrame{});

	// Initialize debug info builder (skip for SPIR-V — no DWARF in SPIR-V)
	if (context.options.emitDebugInfo && !context.options.emitSPIRV) {
		context.diBuilder = new llvm::DIBuilder(*context.llvmModule);
		llvm::DIFile *mainFile = getOrCreateDIFile(context, context.mainSourceFile);
		context.diCompileUnit = context.diBuilder->createCompileUnit(
			llvm::dwarf::DW_LANG_C, mainFile, "DynLex Compiler", context.options.optimizationLevel > 0, "", 0
		);
		context.currentDebugScope = context.diCompileUnit;

		context.llvmModule->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 5);
		context.llvmModule->addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);
	}

	// No first pass — non-flex functions are generated on-demand via monomorphization.
	const bool supportsCommandLineArguments = !context.options.emitSPIRV && !context.options.emitWASM;
	if (supportsCommandLineArguments) {
		context.commandLineArgumentCountGlobal = new llvm::GlobalVariable(
			*context.llvmModule, builder.getInt32Ty(), false, llvm::GlobalValue::InternalLinkage, builder.getInt32(0),
			"dynlex.command_line_argument_count"
		);
		context.commandLineArgumentValuesGlobal = new llvm::GlobalVariable(
			*context.llvmModule, builder.getPtrTy(), false, llvm::GlobalValue::InternalLinkage,
			llvm::Constant::getNullValue(builder.getPtrTy()), "dynlex.command_line_argument_values"
		);
	}

	// In SPIR-V mode, declare shader I/O globals before generating code
	llvm::GlobalVariable *shaderInputGlobal = nullptr;
	llvm::GlobalVariable *shaderOutputGlobal = nullptr;
	if (context.options.emitSPIRV) {
		llvm::Type *vec4Ty = llvm::FixedVectorType::get(builder.getFloatTy(), 4);
		bool isVertex = context.options.shaderStage == ParseContext::ShaderStage::Vertex;

		// Input global (address space 1 = SPIR-V Input storage class)
		std::string inputName = isVertex ? "in_Position" : "gl_FragCoord";
		shaderInputGlobal = new llvm::GlobalVariable(
			*context.llvmModule, vec4Ty, false, llvm::GlobalValue::ExternalLinkage, nullptr, inputName, nullptr,
			llvm::GlobalValue::NotThreadLocal, 1
		);
		shaderInputGlobal->setInitializer(llvm::Constant::getNullValue(vec4Ty));

		// Output global (address space 2 = SPIR-V Output storage class)
		std::string outputName = isVertex ? "gl_Position" : "gl_FragColor";
		shaderOutputGlobal = new llvm::GlobalVariable(
			*context.llvmModule, vec4Ty, false, llvm::GlobalValue::ExternalLinkage, nullptr, outputName, nullptr,
			llvm::GlobalValue::NotThreadLocal, 2
		);
		shaderOutputGlobal->setInitializer(llvm::Constant::getNullValue(vec4Ty));

		// Shader uniform globals are created lazily during codegen when
		// "shader uniform" intrinsics are encountered (see generateIntrinsicCode).
	}

	// Create main function: void main() for shaders, int main() for WASM,
	// and int main(int, char **) for native programs.
	llvm::Function *mainFunc;
	if (context.options.emitSPIRV) {
		llvm::FunctionType *mainType = llvm::FunctionType::get(builder.getVoidTy(), false);
		mainFunc = llvm::Function::Create(mainType, llvm::Function::ExternalLinkage, "main", context.llvmModule);
	} else if (context.options.emitWASM) {
		llvm::FunctionType *mainType = llvm::FunctionType::get(builder.getInt32Ty(), false);
		mainFunc = llvm::Function::Create(mainType, llvm::Function::ExternalLinkage, "main", context.llvmModule);
	} else {
		llvm::FunctionType *mainType =
			llvm::FunctionType::get(builder.getInt32Ty(), {builder.getInt32Ty(), builder.getPtrTy()}, false);
		mainFunc = llvm::Function::Create(mainType, llvm::Function::ExternalLinkage, "main", context.llvmModule);
		auto argument = mainFunc->arg_begin();
		argument->setName("argument_count");
		(++argument)->setName("argument_values");
	}

	// Create debug info subprogram for main
	if (context.diBuilder) {
		llvm::DIFile *mainFile = getOrCreateDIFile(context, context.mainSourceFile);
		unsigned mainLine = 1;
		std::vector<llvm::Metadata *> mainTypeMetadata;
		mainTypeMetadata.push_back(context.options.emitSPIRV ? nullptr : getDIType(context, {DataType::Kind::Int, 4}));
		if (supportsCommandLineArguments) {
			mainTypeMetadata.push_back(getDIType(context, {DataType::Kind::Int, 4}));
			DataType argumentValuesType{DataType::Kind::Int, 1};
			argumentValuesType.pointerDepth = 2;
			mainTypeMetadata.push_back(getDIType(context, argumentValuesType));
		}
		auto *mainFuncDIType =
			context.diBuilder->createSubroutineType(context.diBuilder->getOrCreateTypeArray(mainTypeMetadata));
		auto *mainSP = context.diBuilder->createFunction(
			mainFile, "main", "main", mainFile, mainLine, mainFuncDIType, mainLine, llvm::DINode::FlagPrototyped,
			llvm::DISubprogram::SPFlagDefinition
		);
		mainFunc->setSubprogram(mainSP);
		context.currentDebugScope = mainSP;
	}

	llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context.llvmContext, "entry", mainFunc);
	builder.SetInsertPoint(entry);
	if (supportsCommandLineArguments) {
		auto argument = mainFunc->arg_begin();
		llvm::Value *rawArgumentCount = &*argument++;
		llvm::Value *rawArgumentValues = &*argument;
		llvm::Value *hasExecutableName = builder.CreateICmpSGT(rawArgumentCount, builder.getInt32(0), "has_executable_name");
		llvm::Value *userArgumentCount = builder.CreateSelect(
			hasExecutableName, builder.CreateSub(rawArgumentCount, builder.getInt32(1)), builder.getInt32(0),
			"user_argument_count"
		);
		llvm::Value *firstUserArgument =
			builder.CreateGEP(builder.getPtrTy(), rawArgumentValues, builder.getInt32(1), "first_user_argument");
		llvm::Value *userArgumentValues =
			builder.CreateSelect(hasExecutableName, firstUserArgument, rawArgumentValues, "user_argument_values");
		builder.CreateStore(userArgumentCount, context.commandLineArgumentCountGlobal);
		builder.CreateStore(userArgumentValues, context.commandLineArgumentValuesGlobal);
	}
	context.mainLLVMFunction = mainFunc;
	context.mainCleanupBlock = llvm::BasicBlock::Create(*context.llvmContext, "main.cleanup", mainFunc);
	if (!context.options.emitSPIRV)
		context.mainReturnStorage = builder.CreateAlloca(builder.getInt32Ty(), nullptr, "main.return");

	if (!generateSectionCode(context, context.mainSection))
		return false;

	if (!generateExposedFunctions(context, context.mainSection))
		return false;

	if (!builder.GetInsertBlock()->getTerminator()) {
		if (!context.options.emitSPIRV)
			builder.CreateStore(builder.getInt32(0), context.mainReturnStorage);
		builder.CreateBr(context.mainCleanupBlock);
	}
	builder.SetInsertPoint(context.mainCleanupBlock);
	if (!releaseAllManagedStorage(context))
		return false;
	if (context.options.emitSPIRV)
		builder.CreateRetVoid();
	else
		builder.CreateRet(builder.CreateLoad(builder.getInt32Ty(), context.mainReturnStorage, "main.return.value"));

	// Add SPIR-V metadata for shader execution model and decorations
	if (context.options.emitSPIRV) {
		llvm::LLVMContext &ctx = *context.llvmContext;
		bool isVertex = context.options.shaderStage == ParseContext::ShaderStage::Vertex;

		// spirv.ExecutionMode: OriginUpperLeft (required for Fragment only)
		if (!isVertex) {
			llvm::Metadata *execModeOps[] = {
				llvm::ValueAsMetadata::get(mainFunc),
				llvm::ConstantAsMetadata::get(builder.getInt32(7)), // OriginUpperLeft
			};
			llvm::MDNode *execModeNode = llvm::MDNode::get(ctx, execModeOps);
			context.llvmModule->getOrInsertNamedMetadata("spirv.ExecutionMode")->addOperand(execModeNode);
		}
	}

	// Finalize debug info before verification
	if (context.diBuilder)
		context.diBuilder->finalize();

	// Verify
	std::string error;
	llvm::raw_string_ostream errorStream(error);
	if (llvm::verifyModule(*context.llvmModule, &errorStream)) {
		llvm::errs() << "\n=== Invalid LLVM IR (for debugging) ===\n";
		context.llvmModule->print(llvm::errs(), nullptr);
		llvm::errs() << "=== End Invalid LLVM IR ===\n\n";
		context.addDiagnostic(Diagnostic(context, Diagnostic::Level::Error, "llvm verification failed", Range(), "error", error)
		);
		return false;
	}

	// Optimization passes
	if (context.options.optimizationLevel > 0) {
		llvm::LoopAnalysisManager lam;
		llvm::FunctionAnalysisManager fam;
		llvm::CGSCCAnalysisManager cgam;
		llvm::ModuleAnalysisManager mam;

		llvm::PassBuilder pb;
		pb.registerModuleAnalyses(mam);
		pb.registerCGSCCAnalyses(cgam);
		pb.registerFunctionAnalyses(fam);
		pb.registerLoopAnalyses(lam);
		pb.crossRegisterProxies(lam, fam, cgam, mam);

		llvm::OptimizationLevel optLevel;
		switch (context.options.optimizationLevel) {
		case 1:
			optLevel = llvm::OptimizationLevel::O1;
			break;
		case 2:
			optLevel = llvm::OptimizationLevel::O2;
			break;
		case 3:
			optLevel = llvm::OptimizationLevel::O3;
			break;
		default:
			optLevel = llvm::OptimizationLevel::O1;
			break;
		}

		llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(optLevel);
		mpm.run(*context.llvmModule, mam);
	}

	// Output
#ifdef DYNLEX_WEB
	if (!context.options.emitWASM) {
		context.addDiagnostic(
			Diagnostic(context, Diagnostic::Level::Error, "web build only supports --emit-wasm output mode", Range())
		);
		return false;
	}
	if (!emitWASMModule(context))
		return false;
#else
	if (context.options.emitSPIRV) {
		if (!emitSPIRVModule(context))
			return false;
	} else if (context.options.emitWASM) {
		if (!emitWASMModule(context))
			return false;
	} else if (context.options.emitLLVM) {
		std::string outputPath = context.options.outputPath;
		if (outputPath.empty())
			outputPath = context.options.inputPath + ".ll";
		std::error_code ec;
		llvm::raw_fd_ostream out(outputPath, ec);
		if (ec) {
			context.addDiagnostic(
				Diagnostic(context, Diagnostic::Level::Error, "failed to open output file", Range(), "error", ec.message())
			);
			return false;
		}
		context.llvmModule->print(out, nullptr);
	} else {
		if (!emitNativeExecutable(context))
			return false;
	}
#endif

	return true;
}
