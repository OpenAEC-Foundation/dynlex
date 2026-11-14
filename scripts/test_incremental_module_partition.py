"""Exercise LLVM partition cloning independently of DynLex syntax and inference."""

import os
from pathlib import Path
import subprocess
import sys

project = Path(sys.argv[1]).resolve()
subprocess.run(["cmake", "--build", str(project / "build"), "--target",
                "dynlex_incremental_module_partition_test", "--parallel", "1"], check=True)
executable = project / "build" / ("dynlex_incremental_module_partition_test.exe" if os.name == "nt"
                                else "dynlex_incremental_module_partition_test")
subprocess.run([str(executable)], check=True)
