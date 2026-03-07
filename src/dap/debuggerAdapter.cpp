#include "debuggerAdapter.h"

#ifndef _WIN32
#include "gdbmi.h"
#endif

namespace dap {

#ifndef _WIN32
class PosixGdbAdapter final : public DebuggerAdapter {
  public:
	bool isSupported() const override { return true; }
	std::string unsupportedReason() const override { return ""; }

	bool launch(const std::string &debuggerPath, std::string &errorMessage) override {
		if (gdb.launch(debuggerPath))
			return true;
		errorMessage = "Failed to launch debugger process";
		return false;
	}

	int send(const std::string &command) override { return gdb.send(command); }
	bool readRecord(MiRecord &record) override { return gdb.readRecord(record); }
	MiRecord sendAndWait(const std::string &command) override { return gdb.sendAndWait(command); }
	void deliverResult(const MiRecord &record) override { gdb.deliverResult(record); }
	void terminate() override { gdb.terminate(); }
	bool isRunning() const override { return gdb.isRunning(); }

  private:
	GdbMI gdb;
};
#else
class UnsupportedDebuggerAdapter final : public DebuggerAdapter {
  public:
	bool isSupported() const override { return false; }
	std::string unsupportedReason() const override { return "DAP debugger backend is not implemented for Windows yet."; }

	bool launch(const std::string &, std::string &errorMessage) override {
		errorMessage = unsupportedReason();
		return false;
	}

	int send(const std::string &) override { return -1; }
	bool readRecord(MiRecord &) override { return false; }
	MiRecord sendAndWait(const std::string &) override {
		MiRecord result;
		result.type = MiRecord::Result;
		result.recordClass = "error";
		result.values = {{"msg", unsupportedReason()}};
		return result;
	}
	void deliverResult(const MiRecord &) override {}
	void terminate() override {}
	bool isRunning() const override { return false; }
};
#endif

std::unique_ptr<DebuggerAdapter> createDebuggerAdapter() {
#ifndef _WIN32
	return std::make_unique<PosixGdbAdapter>();
#else
	return std::make_unique<UnsupportedDebuggerAdapter>();
#endif
}

} // namespace dap
