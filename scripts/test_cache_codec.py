#!/usr/bin/env python3
"""Check cache value fidelity and Python/C++ artifact interoperability."""

import json
import math
from pathlib import Path
import subprocess
import sys

from cache_payloads import decode_cbor

project = Path(sys.argv[1]).resolve()
executable = project / "build" / ("dynlex_cached_cbor_test.exe" if sys.platform == "win32"
                                 else "dynlex_cached_cbor_test")
result = subprocess.run([str(executable)], capture_output=True, text=True, check=True)
fixture = json.loads(result.stdout)
value = decode_cbor(bytes(fixture["encoded"]))
assert value == fixture["value"]
assert math.copysign(1, value["values"][2]) == -1
for malformed in (b"", b"\x63x", b"\x18", b"\x9f\xff", b"\xa1\x00\xf6",
                  b"\xa2\x61x\x00\x61x\x01", b"\xf6\x00"):
    try:
        decode_cbor(malformed)
    except (ValueError, IndexError):
        continue
    raise AssertionError("malformed cache CBOR was accepted")
print("Cache codec values and artifact interoperability passed")
