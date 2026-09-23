# SPDX-License-Identifier: MPL-2.0
"""Run the shared Curve2/Polyline2 source differential."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "curve2"))
from verify import main, unattended_child_processes

if __name__ == "__main__":
    with unattended_child_processes():
        main()
