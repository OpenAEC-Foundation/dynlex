#pragma once
#include "miRecord.h"
#include <memory>
#include <string>

namespace dap {

// Virtualized debugger backend used by the DAP server.
class DebuggerAdapter {
  public:
	virtual ~DebuggerAdapter() = default;

	virtual bool isSupported() const = 0;
	virtual std::string unsupportedReason() const = 0;

	virtual bool launch(const std::string &debuggerPath, std::string &errorMessage) = 0;
	virtual int send(const std::string &command) = 0;
	virtual bool readRecord(MiRecord &record) = 0;
	virtual MiRecord sendAndWait(const std::string &command) = 0;
	virtual void deliverResult(const MiRecord &record) = 0;
	virtual void terminate() = 0;
	virtual bool isRunning() const = 0;
};

std::unique_ptr<DebuggerAdapter> createDebuggerAdapter();

} // namespace dap
