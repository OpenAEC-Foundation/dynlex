# Native fixture checks; every child inherits unattended Windows error mode.
import argparse
from pathlib import Path
import subprocess
import sys
import os

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from process_error_mode import unattended_child_processes

def run(command, timeout=60):
    with unattended_child_processes():
        return subprocess.run([str(x) for x in command], cwd=ROOT, capture_output=True, text=True, timeout=timeout)

def main():
    p=argparse.ArgumentParser()
    p.add_argument("fixture", type=Path)
    p.add_argument("--compiler", type=Path, default=ROOT/"build"/("dynlex.exe" if os.name=="nt" else "dynlex"))
    args=p.parse_args()
    directory=ROOT/"build"/"cadkernel-curve2-checks"
    directory.mkdir(exist_ok=True)
    for optimization in ("O0","O2"):
        binary=directory/(args.fixture.parent.name+"-"+args.fixture.stem+"-"+optimization+".out")
        result=run([args.compiler,args.fixture,"-"+optimization,"-o",binary])
        print(optimization,"compiler",result.returncode,result.stdout,result.stderr,flush=True)
        if result.returncode: return 1
        if result.stdout.strip() or result.stderr.strip(): raise AssertionError("unexpected diagnostics")
        result=run([binary],30)
        print(optimization,"runtime",result.returncode,result.stdout,result.stderr,flush=True)
        if result.returncode: return 1
        expected=args.fixture.with_name("expected.txt")
        if expected.exists() and result.stdout.replace("\r\n","\n") != expected.read_text(encoding="utf-8"):
            raise AssertionError("fixture output mismatch")
    return 0

if __name__=="__main__":
    with unattended_child_processes():
        sys.exit(main())
