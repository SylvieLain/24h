#!/usr/bin/env python3
import sys
from pathlib import Path

TOOL_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOL_ROOT / "src"))

from dap_cli.cli import main


if __name__ == "__main__":
    raise SystemExit(main())
