#!/usr/bin/env python3
"""Exercise release eligibility against real Git branch and tag histories."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


VERIFIER = Path(__file__).resolve().parents[2] / "scripts" / "verify-release-ref.sh"


class ReleaseRefTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="dynlex-release-ref-")
        self.addCleanup(self.temporary.cleanup)
        self.repository = Path(self.temporary.name)
        self.git("init", "--initial-branch=master")
        self.git("config", "user.name", "Release test")
        self.git("config", "user.email", "release-test@example.invalid")
        (self.repository / "metadata").mkdir()
        (self.repository / "metadata/VERSION").write_text("1.2.3\n", encoding="utf-8")
        self.git("add", "metadata/VERSION")
        self.git("commit", "-m", "Accepted release")
        self.git("update-ref", "refs/remotes/origin/master", "HEAD")

    def git(self, *arguments: str) -> None:
        subprocess.run(["git", *arguments], cwd=self.repository, check=True, capture_output=True)

    def verify(self, ref_type: str, ref: str, accepted: bool, message: str = "") -> None:
        result = subprocess.run(
            [os.environ.get("DYNLEX_TEST_BASH", "bash"), str(VERIFIER)],
            cwd=self.repository,
            env={**os.environ, "GITHUB_REF_TYPE": ref_type, "GITHUB_REF": ref, "DEFAULT_BRANCH": "master"},
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode == 0, accepted, result.stdout + result.stderr)
        self.assertIn(message, result.stdout + result.stderr)

    def test_default_branch_validation(self) -> None:
        self.verify("branch", "refs/heads/master", True, "without publication")

    def test_release_tags_on_accepted_commit(self) -> None:
        for tag in ("v1.2.3", "1.2.3"):
            with self.subTest(tag=tag):
                self.git("tag", "-a", tag, "-m", "Release")
                self.git("checkout", "--detach", tag)
                self.verify("tag", f"refs/tags/{tag}", True)

    def test_unmerged_tag_is_rejected(self) -> None:
        self.git("switch", "-c", "unreviewed")
        self.git("commit", "--allow-empty", "-m", "Unmerged change")
        self.git("tag", "v1.2.3")
        self.git("checkout", "--detach", "v1.2.3")
        self.verify("tag", "refs/tags/v1.2.3", False, "must already be merged")

    def test_merged_tag_is_accepted(self) -> None:
        self.git("switch", "-c", "reviewed")
        self.git("commit", "--allow-empty", "-m", "Reviewed change")
        self.git("tag", "v1.2.3")
        self.git("switch", "master")
        self.git("merge", "--no-ff", "reviewed", "-m", "Accept change")
        self.git("update-ref", "refs/remotes/origin/master", "HEAD")
        self.git("checkout", "--detach", "v1.2.3")
        self.verify("tag", "refs/tags/v1.2.3", True)

    def test_version_mismatch_is_rejected(self) -> None:
        self.verify("tag", "refs/tags/v9.9.9", False, "does not match")

    def test_other_branches_and_pull_requests_are_rejected(self) -> None:
        self.verify("branch", "refs/heads/unreviewed", False, "only allowed")
        self.verify("branch", "refs/pull/123/merge", False, "only allowed")
        self.verify("pull_request", "refs/pull/123/merge", False, "Unsupported release ref")


if __name__ == "__main__":
    unittest.main()
