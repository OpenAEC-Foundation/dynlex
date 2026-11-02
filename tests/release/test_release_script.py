#!/usr/bin/env python3
"""Exercise release commits and tags using a local Git remote and GitHub fixture."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[2]


class ReleaseScriptTests(unittest.TestCase):
    def test_calendar_release_uses_push_pipeline_and_prefixed_tag(self):
        with tempfile.TemporaryDirectory(prefix="dynlex-release-script-") as directory:
            root = Path(directory)
            checkout = root / "checkout"
            remote = root / "remote.git"
            checkout.mkdir()

            def git(*arguments):
                return subprocess.run(["git", *arguments], cwd=checkout, check=True, capture_output=True, text=True).stdout.strip()

            git("init", "--initial-branch=master")
            git("config", "user.name", "Release test")
            git("config", "user.email", "release-test@example.invalid")
            git("init", "--bare", str(remote))
            git("remote", "add", "origin", str(remote))
            scripts = checkout / "scripts"
            scripts.mkdir()
            for name in ("release.sh", "update-version.mjs"):
                shutil.copyfile(PROJECT / "scripts" / name, scripts / name)
            (scripts / "test.sh").write_text("#!/usr/bin/env bash\nexit 0\n", encoding="utf-8")
            (scripts / "test.sh").chmod(0o755)
            (checkout / "metadata").mkdir()
            (checkout / "metadata/VERSION").write_text("2026.45.0\n", encoding="utf-8")
            extension = checkout / "vscode-extension"
            extension.mkdir()
            (extension / "package.json").write_text(json.dumps({"version": "2026.45.0"}), encoding="utf-8")
            (extension / "package-lock.json").write_text(json.dumps({
                "version": "2026.45.0", "packages": {"": {"version": "2026.45.0"}},
            }), encoding="utf-8")
            translator = checkout / "tools/wgsl-translator"
            translator.mkdir(parents=True)
            package = 'name = "dynlex-wgsl-translator"\nversion = "2026.45.0"\n'
            (translator / "Cargo.toml").write_text("[package]\n" + package, encoding="utf-8")
            (translator / "Cargo.lock").write_text("version = 4\n\n[[package]]\n" + package, encoding="utf-8")
            git("add", ".")
            git("commit", "-m", "Accepted source")
            git("push", "--set-upstream", "origin", "master")

            bin_directory = root / "bin"
            bin_directory.mkdir()
            gh = bin_directory / "gh"
            gh.write_text('''#!/usr/bin/env bash
set -euo pipefail
case "$1 $2" in
  "auth status"|"run watch") ;;
  "repo view") echo fixture/dynlex ;;
  "run list")
    shift 2
    while [[ $# -gt 0 ]]; do
      if [[ "$1" == --workflow ]]; then
        case "$2" in
          pipeline.yml|release.yml) echo "$2" >> "$DYNLEX_RELEASE_TEST_LOG" ;;
          *) echo "Wrong workflow: $2" >&2; exit 1 ;;
        esac
      fi
      shift
    done
    echo 1
    ;;
  *) echo "Unexpected GitHub command" >&2; exit 1 ;;
esac
''', encoding="utf-8")
            gh.chmod(0o755)
            log = root / "github.log"
            environment = {
                **os.environ,
                "PATH": str(bin_directory) + os.pathsep + os.environ["PATH"],
                "DYNLEX_RELEASE_TEST_LOG": str(log),
                "RELEASE_WORKFLOW_TIMEOUT_SECONDS": "2",
                "RELEASE_WORKFLOW_POLL_SECONDS": "1",
            }
            environment.pop("RELEASE_CI_WORKFLOW_FILE", None)
            environment.pop("RELEASE_WORKFLOW_FILE", None)
            result = subprocess.run(
                [os.environ.get("DYNLEX_TEST_BASH", "bash"), str(scripts / "release.sh"), "2026.45.1"],
                cwd=checkout, env=environment, capture_output=True, text=True, timeout=20,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(log.read_text().splitlines(), ["pipeline.yml", "pipeline.yml", "release.yml"])
            self.assertEqual(git("tag", "--list"), "v2026.45.1")
            self.assertEqual(git("status", "--porcelain"), "")
            self.assertEqual(git("log", "-1", "--format=%s"), "Release 2026.45.1")
            self.assertIn("refs/tags/v2026.45.1", git("ls-remote", "--tags", "origin"))
            self.assertIn('version = "2026.45.1"', (translator / "Cargo.toml").read_text())


if __name__ == "__main__":
    unittest.main()
