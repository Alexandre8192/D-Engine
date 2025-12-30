"""Self-test to ensure policy_lint flags forbidden constructs.

Copies the known violation source into a temporary Source/Core tree and runs
policy_lint in strict mode. The test passes only if policy_lint returns non-zero
and reports the violation.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    repo = Path(__file__).resolve().parent.parent
    violation_src = repo / "tests" / "Policy" / "policy_violation_throw.cpp"
    if not violation_src.is_file():
        print(f"Violation source not found: {violation_src}")
        return 1

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_root = Path(tmpdir)
        dst = tmp_root / "Source" / "Core" / "PolicyViolations" / "policy_violation_throw.cpp"
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(violation_src, dst)

        cmd = [
            sys.executable,
            str(repo / "tools" / "policy_lint.py"),
            "--root",
            str(tmp_root),
            "--strict",
        ]
        result = subprocess.run(cmd, capture_output=True, text=True)
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)

        if result.returncode == 0:
            print("policy_lint self-test FAILED: expected a violation but lint returned success")
            return 1
        if "throw" not in result.stdout:
            print("policy_lint self-test FAILED: violation output did not mention 'throw'")
            return 1

    print("policy_lint self-test: OK (violation detected as expected)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
