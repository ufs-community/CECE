"""Unit tests for scripts/verify_submodules.py."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

# Add scripts directory to sys.path
SCRIPTS_DIR = Path(__file__).resolve().parent.parent / "scripts"
sys.path.insert(0, str(SCRIPTS_DIR))

from verify_submodules import (  # noqa: E402
    SubmoduleStatus,
    UpstreamRemoteConfig,
    VerificationStatus,
    generate_step_summary,
    get_declared_submodules,
    get_web_url_from_remote,
    is_submodule_excluded,
    log_verification_report,
    parse_branch_map_args,
    parse_submodule_status_lines,
    resolve_submodule_target_branch,
    verify_submodules,
)


class TestVerifySubmodules(unittest.TestCase):
    """Test suite for submodule verification functions."""

    def test_parse_submodule_status_lines(self) -> None:
        raw_output = """
 9e2f6751c3e276fa9c50b8443f4601c846bc3efd extern/helm (remotes/benkozi/HEAD-6-g9e2f675)
+d21b5278508a5aca4ed3ea59403cc03538b40c7d extern/helm/libs/amio (v0.1.0-53-gd21b527)
-1234567890abcdef1234567890abcdef12345678 uninitialized/submod
"""
        entries = parse_submodule_status_lines(raw_output)
        self.assertEqual(len(entries), 3)

        self.assertEqual(
            entries[0], (" ", "9e2f6751c3e276fa9c50b8443f4601c846bc3efd", "extern/helm")
        )
        self.assertEqual(
            entries[1],
            ("+", "d21b5278508a5aca4ed3ea59403cc03538b40c7d", "extern/helm/libs/amio"),
        )
        self.assertEqual(
            entries[2],
            ("-", "1234567890abcdef1234567890abcdef12345678", "uninitialized/submod"),
        )

    def test_parse_branch_map_args(self) -> None:
        # Empty or None
        self.assertEqual(parse_branch_map_args(None), {})
        self.assertEqual(parse_branch_map_args([]), {})

        # Valid mappings
        mapped = parse_branch_map_args(["extern/helm=develop", "libs/amio=main"])
        self.assertEqual(mapped, {"extern/helm": "develop", "libs/amio": "main"})

        # Invalid format raises exception
        with self.assertRaises(Exception) as ctx:
            parse_branch_map_args(["invalid_format"])
        self.assertIn("Invalid branch map format", str(ctx.exception))

    def test_resolve_submodule_target_branch(self) -> None:
        """Verify target branch resolution with and without branch map override."""
        self.assertEqual(
            resolve_submodule_target_branch(
                sub_path="extern/helm",
                parent_target_branch="develop",
                branch_map={},
            ),
            "develop",
        )
        self.assertEqual(
            resolve_submodule_target_branch(
                sub_path="extern/helm",
                parent_target_branch="develop",
                branch_map={"extern/helm": "feature/custom"},
            ),
            "feature/custom",
        )

    def test_log_verification_report_all_ok(self) -> None:
        statuses = [
            SubmoduleStatus(
                path="extern/helm",
                current_sha="c4d2b5ab1234",
                expected_sha="c4d2b5ab1234",
                target_branch="develop",
                remote_url="https://github.com/example/helm.git",
                status="OK",
                detail="Pointers match",
            ),
            SubmoduleStatus(
                path="extern/helm/libs/amio",
                current_sha="d21b52785678",
                expected_sha="d21b52785678",
                target_branch="develop",
                remote_url="https://github.com/example/amio.git",
                status="OK",
                detail="Pointers match",
                is_nested=True,
            ),
        ]

        all_ok = log_verification_report(statuses, target_branch="develop")
        self.assertTrue(all_ok)

    def test_log_verification_report_mismatch(self) -> None:
        statuses = [
            SubmoduleStatus(
                path="extern/helm",
                current_sha="9e2f67510000",
                expected_sha="c4d2b5ab0000",
                target_branch="develop",
                remote_url="https://github.com/example/helm.git",
                status="OUT_OF_SYNC",
                detail="Behind by 1 commit",
            )
        ]

        all_ok = log_verification_report(statuses, target_branch="develop")
        self.assertFalse(all_ok)

    def test_verification_status_strenum(self) -> None:
        """Test that VerificationStatus is a StrEnum and behaves correctly with SubmoduleStatus."""
        # Enum values and str subclass
        for name in ("PENDING", "OK", "OUT_OF_SYNC", "UNINITIALIZED", "ERROR"):
            self.assertEqual(VerificationStatus[name], name)
            self.assertIsInstance(VerificationStatus[name], str)

        # Unique enum members (verified by @unique)
        self.assertEqual(len(VerificationStatus), 5)
        self.assertEqual(len({s.value for s in VerificationStatus}), 5)

        # Initialized with enum
        s1 = SubmoduleStatus(
            path="sub1",
            current_sha="1111",
            target_branch="develop",
            remote_url="http://example.com",
            status=VerificationStatus.OK,
        )
        self.assertIs(s1.status, VerificationStatus.OK)
        self.assertEqual(s1.status, "OK")

        # Coerced from string
        s2 = SubmoduleStatus(
            path="sub2",
            current_sha="2222",
            target_branch="develop",
            remote_url="http://example.com",
            status="OUT_OF_SYNC",
        )
        self.assertIs(s2.status, VerificationStatus.OUT_OF_SYNC)
        self.assertEqual(s2.status, "OUT_OF_SYNC")

    def test_generate_step_summary(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            summary_file = Path(tmp_dir) / "step_summary.md"
            statuses = [
                SubmoduleStatus(
                    path="extern/helm",
                    current_sha="9e2f6751",
                    expected_sha="c4d2b5ab",
                    target_branch="develop",
                    remote_url="https://github.com/example/helm.git",
                    status="OUT_OF_SYNC",
                    detail="Behind by 1 commit",
                )
            ]

            generate_step_summary(statuses, "develop", summary_file)
            self.assertTrue(summary_file.exists())
            content = summary_file.read_text(encoding="utf-8")
            self.assertIn("## Submodule Verification Report", content)
            self.assertIn("[`extern/helm`](https://github.com/example/helm)", content)
            self.assertIn(
                "[`develop`](https://github.com/example/helm/tree/develop)", content
            )
            self.assertIn(
                "[`9e2f6751`](https://github.com/example/helm/commit/9e2f6751)",
                content,
            )
            self.assertIn(
                "[`c4d2b5ab`](https://github.com/example/helm/commit/c4d2b5ab)",
                content,
            )
            self.assertIn(
                "https://github.com/example/helm/compare/9e2f6751...c4d2b5ab", content
            )
            self.assertIn("OUT_OF_SYNC", content)
            self.assertIn("[!WARNING]", content)

    def test_get_web_url_from_remote(self) -> None:
        self.assertEqual(
            get_web_url_from_remote("https://github.com/bbakernoaa/HELM-Project.git"),
            "https://github.com/bbakernoaa/HELM-Project",
        )
        self.assertEqual(
            get_web_url_from_remote("git@github.com:bbakernoaa/amio.git"),
            "https://github.com/bbakernoaa/amio",
        )
        self.assertEqual(
            get_web_url_from_remote("ssh://git@github.com/owner/repo.git"),
            "https://github.com/owner/repo",
        )
        self.assertIsNone(get_web_url_from_remote(""))

    def test_cli_help(self) -> None:
        """Test CLI --help exits with 0 using subprocess.check_output."""
        script_path = SCRIPTS_DIR / "verify_submodules.py"
        stdout = subprocess.check_output(
            [sys.executable, str(script_path), "--help"],
            text=True,
            stderr=subprocess.DEVNULL,
        )
        self.assertIn("Verify that Git submodules", stdout)
        self.assertIn("--target-branch", stdout)
        self.assertIn("--allow-ancestor", stdout)

    def test_get_declared_submodules_live_repo(self) -> None:
        """Verify get_declared_submodules finds both direct and nested submodules in CECE."""
        repo_root = SCRIPTS_DIR.parent
        declared = get_declared_submodules(repo_root)
        self.assertIn("extern/helm", declared)
        self.assertIn("extern/helm/libs/amio", declared)

    def test_get_declared_submodules_empty_when_no_gitmodules(self) -> None:
        """Verify get_declared_submodules returns empty set if .gitmodules is absent."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            declared = get_declared_submodules(Path(tmp_dir))
            self.assertEqual(declared, set())

    def test_get_declared_submodules_arbitrary_depth(self) -> None:
        """Verify get_declared_submodules recursively discovers submodules beyond 2 levels."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root = Path(tmp_dir)
            for parent_rel, child in [
                ("", "sub1"),
                ("sub1", "sub2"),
                ("sub1/sub2", "sub3"),
            ]:
                target_dir = repo_root / parent_rel if parent_rel else repo_root
                target_dir.mkdir(parents=True, exist_ok=True)
                (target_dir / ".gitmodules").write_text(
                    f'[submodule "{child}"]\n\tpath = {child}\n\turl = https://example.com/{child}\n',
                    encoding="utf-8",
                )

            declared = get_declared_submodules(repo_root)
            self.assertEqual(
                declared,
                {"sub1", "sub1/sub2", "sub1/sub2/sub3"},
            )

    def test_upstream_remote_config_and_branch_validation(self) -> None:
        """Ensure verification enforces canonical remotes and rejects conflicting .gitmodules branches."""
        config = UpstreamRemoteConfig()
        # Verify canonical remotes for bbakernoaa-hosted upstream repositories
        self.assertEqual(
            config.canonical_remotes["extern/helm"],
            "https://github.com/bbakernoaa/HELM-Project",
        )
        self.assertEqual(
            config.canonical_remotes["extern/helm/libs/amio"],
            "https://github.com/bbakernoaa/amio",
        )

        def _mock_git(
            url: str = "https://github.com/bbakernoaa/HELM-Project.git",
            branch: str | None = None,
            sha: str = "9e2f6751",
            remote_head: str | None = None,
        ):
            def fake_git(
                args: list[str], cwd: Path | None = None
            ) -> tuple[int, str, str]:
                if "submodule" in args and "status" in args:
                    return (0, f" {sha} extern/helm", "")
                if "config" in args and "submodule.extern/helm.url" in args:
                    return (0, url, "")
                if "config" in args and "submodule.extern/helm.branch" in args:
                    return (0, branch, "") if branch else (1, "", "key not found")
                if "config" in args and r"^submodule\..*\.path$" in args:
                    return (0, "submodule.extern/helm.path extern/helm", "")
                if "ls-remote" in args and remote_head:
                    return (
                        0,
                        f"{remote_head} refs/heads/{args[-1].split('/')[-1]}",
                        "",
                    )
                return (0, "", "")

            return fake_git

        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root = Path(tmp_dir)
            (repo_root / ".git").mkdir()
            (repo_root / "extern" / "helm").mkdir(parents=True)

            with patch("verify_submodules.run_git_cmd") as mock_git:
                # 1. Test unauthorized fork drift rejection
                mock_git.side_effect = _mock_git(
                    url="https://github.com/attacker/HELM-Project.git"
                )
                statuses_fork = verify_submodules(
                    repo_root=repo_root,
                    target_branch="develop",
                    branch_map={},
                    upstream_config=config,
                )
                self.assertEqual(statuses_fork[0].status, VerificationStatus.ERROR)
                self.assertIn(
                    "differs from canonical upstream", statuses_fork[0].detail
                )

                # 2. When targeting 'main', if .gitmodules has 'develop', it must fail
                mock_git.side_effect = _mock_git(branch="develop")
                statuses = verify_submodules(
                    repo_root=repo_root,
                    target_branch="main",
                    branch_map={},
                    upstream_config=config,
                )
                self.assertEqual(len(statuses), 1)
                self.assertEqual(statuses[0].status, VerificationStatus.ERROR)
                self.assertIn(
                    "differs from parent target branch 'main'", statuses[0].detail
                )

                # 3. Test without pinned .gitmodules branch: commit matching main HEAD passes
                mock_git.side_effect = _mock_git(sha="11112222", remote_head="11112222")
                statuses_main = verify_submodules(
                    repo_root=repo_root,
                    target_branch="main",
                    branch_map={},
                    upstream_config=config,
                )
                self.assertEqual(statuses_main[0].status, VerificationStatus.OK)
                self.assertEqual(statuses_main[0].target_branch, "main")

    def test_is_submodule_excluded(self) -> None:
        """Verify is_submodule_excluded handles exact matches, trailing slashes, and prefixes."""
        excludes = {"extern/foo", "vendor/bar/"}
        self.assertTrue(is_submodule_excluded("extern/foo", excludes))
        self.assertTrue(is_submodule_excluded("extern/foo/nested", excludes))
        self.assertTrue(is_submodule_excluded("vendor/bar", excludes))
        self.assertTrue(is_submodule_excluded("vendor/bar/sub", excludes))
        self.assertFalse(is_submodule_excluded("extern/foo_other", excludes))
        self.assertFalse(is_submodule_excluded("extern/baz", excludes))

    def test_exclude_submodules_via_config(self) -> None:
        """Verify submodule exclusion works via UpstreamRemoteConfig."""
        repo_root = SCRIPTS_DIR.parent
        # In live CECE repo, we have extern/helm and extern/helm/libs/amio
        # 1. Exclude nested submodule via UpstreamRemoteConfig
        config_nested = UpstreamRemoteConfig(
            excluded_submodules={"extern/helm/libs/amio"}
        )
        statuses = verify_submodules(
            repo_root=repo_root,
            target_branch="develop",
            branch_map={},
            upstream_config=config_nested,
        )
        paths = {s.path for s in statuses}
        self.assertIn("extern/helm", paths)
        self.assertNotIn("extern/helm/libs/amio", paths)

        # 2. Exclude top-level submodule via UpstreamRemoteConfig (which also excludes its nested children)
        config_all = UpstreamRemoteConfig(excluded_submodules={"extern/helm"})
        statuses_all = verify_submodules(
            repo_root=repo_root,
            target_branch="develop",
            branch_map={},
            upstream_config=config_all,
        )
        self.assertEqual(statuses_all, [])


if __name__ == "__main__":
    unittest.main()
