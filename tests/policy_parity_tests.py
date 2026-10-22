#!/usr/bin/env python3
"""The committed browser policy must keep scoring base-game moves exactly as
recorded in tests/fixtures/policy_logits_base.json.

The fixture was recorded before the Renegades expansion grew the card,
character, and observation layouts. Base games must be unchanged by that work,
and the deployed policy must keep playing them identically, whether it is
loaded as recorded or converted to a newer layout.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

# The fixture keeps 7 significant digits (float32 precision); conversions may
# also reorder float sums, so allow a little more than that.
TOLERANCE = 1e-4


def main() -> int:
    binary = Path(sys.argv[1]).resolve()
    repo = Path(sys.argv[2]).resolve()
    fixture = json.loads((repo / "tests" / "fixtures" / "policy_logits_base.json").read_text())
    policy = repo / fixture["policy"]
    checked = 0
    for run in fixture["runs"]:
        result = subprocess.run(
            [str(binary), "policy-logits", str(policy),
             str(fixture["decisionsPerRun"]), str(int(run["seed"]))],
            cwd=repo, capture_output=True, text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(f"policy-logits failed for seed {run['seed']}:\n{result.stderr}")
        actual = json.loads(result.stdout)["decisions"]
        expected = run["decisions"]
        if len(actual) != len(expected):
            raise RuntimeError(f"seed {run['seed']}: {len(actual)} decisions, expected {len(expected)}")
        for index, (got, want) in enumerate(zip(actual, expected)):
            where = f"seed {run['seed']} decision {index}"
            if got["moves"] != want["moves"]:
                raise RuntimeError(f"{where}: legal moves changed\n got {got['moves']}\nwant {want['moves']}")
            for move, a, b in zip(want["moves"], got["logits"], want["logits"]):
                if abs(a - b) > TOLERANCE * max(1.0, abs(b)):
                    raise RuntimeError(f"{where}: {move} scored {a}, recorded {b}")
            checked += 1
    print(f"policy parity: {checked} base-game decisions match the recorded scores")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
