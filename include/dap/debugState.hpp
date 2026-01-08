#pragma once

namespace tbx {

// Debug execution state
enum class DebugState {
  Stopped, // Not running
  Running, // Running normally
  Paused,  // Paused at breakpoint or step
  Stepping // Executing a step operation
};

} // namespace tbx
