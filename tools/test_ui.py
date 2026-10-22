#!/usr/bin/env python3
"""Run every shared-UI test; missing Node is a refusal, not a skipped pass."""
from pathlib import Path
import shutil
import subprocess
import sys

if not shutil.which("node"):
    raise SystemExit("Node.js is required; no UI tests were run")
root = Path(__file__).resolve().parents[1]
result = subprocess.call([sys.executable, "-B", "-m", "unittest", "discover", "-s",
                         str(root / "src/blackwire/desk/tests"), "-p", "test_ui.py", "-v"], cwd=root)
if result:
    raise SystemExit(result)
raise SystemExit(subprocess.call(["node", str(root / "src/blackwire/desk/tests/browser_handoff_harness.js"),
                                 str(root / "src/blackwire/desk/index.html")], cwd=root))
