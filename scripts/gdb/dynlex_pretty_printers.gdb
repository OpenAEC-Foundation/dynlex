python
import os
import sys

script_dir = os.environ.get("DYNLEX_GDB_SCRIPT_DIR")
if not script_dir:
    script_dir = os.path.join(os.getcwd(), "scripts", "gdb")
script_dir = os.path.abspath(script_dir)
if script_dir not in sys.path:
    sys.path.insert(0, script_dir)

import dynlex_pretty_printers

dynlex_pretty_printers.register(gdb.current_objfile())
end
