#!/usr/bin/env python3
import subprocess
import sys
import time


if len(sys.argv) > 1 and sys.argv[1] == "--descendant":
    time.sleep(60)
else:
    subprocess.Popen(
        [sys.executable, __file__, "--descendant"],
        stdout=sys.stdout,
        stderr=sys.stderr,
    )
    time.sleep(60)
