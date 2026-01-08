#pragma once

namespace tbx {

/**
 * Optimization level enumeration
 * Maps to standard compiler optimization flags
 */
enum class OptimizationLevel {
  O0, // No optimization (for debugging)
  O1, // Basic optimizations
  O2, // Standard optimizations (default)
  O3  // Aggressive optimizations
};

} // namespace tbx
