#pragma once

namespace tbx {

/**
 * Output format for the compiler
 */
enum class OutputFormat {
  Executable, // Native binary for the target platform
  Object,     // .o file for linking
  LLVM_IR,    // .ll file for inspection
  Assembly    // .s file for inspection
};

} // namespace tbx
