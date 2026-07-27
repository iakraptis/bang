#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value))


def main() -> int:
    repo = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="bang-about-data-test-") as temporary:
        run_dir = Path(temporary) / "campaign"
        write_json(
            run_dir / "manifest.json",
            {
                "settings": {
                    "batchGames": 2,
                    "learningRate": 0.0003,
                    "targetKl": 0.015,
                    "updateEpochs": 4,
                    "minibatches": 8,
                    "entropyCoef": 0.01,
                },
                "opponents": {
                    "frozenCurrent": 0.5,
                    "pfspHistory": 0.4,
                    "heuristic": 0.05,
                    "random": 0.05,
                },
            },
        )
        write_json(
            run_dir / "main" / "latest.pt.metadata.json",
            {
                "iteration": 2,
                "environmentSteps": 120,
                "learnerDecisions": 48,
                "elapsedSeconds": 3.5,
                "parameterCount": 276291,
                "observationSize": 1229,
                "actionFeatureSize": 91,
                "privilegedSize": 448,
            },
        )
        style = {
            "attacks": 2,
            "attackOpportunities": 4,
            "defenses": 3,
            "defenseOpportunities": 3,
            "swaps": 1,
            "swapOpportunities": 4,
            "abilities": 1,
            "abilityOpportunities": 2,
            "rearTargets": 1,
            "opponentTargets": 5,
            "cardsPlayed": 6,
            "turns": 3,
            "equipmentPlayed": 2,
            "cards": [
                {
                    "card": "stealing",
                    "selected": 3,
                    "opportunities": 4,
                    "used": 5,
                }
            ],
        }
        opponents = [
            {"id": "current", "kind": "checkpoint", "games": 4, "wins": 2},
            {"id": "main_000001", "kind": "checkpoint", "games": 2, "wins": 2},
            {"id": "heuristic", "kind": "heuristic", "games": 2, "wins": 2},
            {"id": "random", "kind": "random", "games": 2, "wins": 2},
        ]
        metrics = [
            {
                "iteration": 1,
                "cumulativeEnvironmentSteps": 60,
                "games": 2,
                "learnerWinRate": 0.5,
                "normalizedEntropy": 0.8,
                "topActionProbability": 0.4,
                "rolloutSeconds": 1.0,
                "updateSeconds": 3.0,
                "opponentPerformance": opponents,
                "style": style,
            },
            {
                "iteration": 2,
                "cumulativeEnvironmentSteps": 120,
                "games": 2,
                "learnerWinRate": 0.75,
                "normalizedEntropy": 0.6,
                "topActionProbability": 0.7,
                "rolloutSeconds": 1.0,
                "updateSeconds": 3.0,
                "opponentPerformance": opponents,
                "style": style,
            },
        ]
        metrics_path = run_dir / "main" / "metrics.jsonl"
        metrics_path.write_text(
            "".join(json.dumps(row) + "\n" for row in metrics)
            + '{"iteration":'
        )
        report = {
            "iteration": 2,
            "lawWinRate": 0.45,
            "outlawWinRate": 0.55,
            "meanTurns": 31,
            "games": 8,
            "seed": 42,
            "characters": [
                {
                    "name": "Dalon Ranger",
                    "side": "law",
                    "winRate": 0.55,
                    "uplift": 0.1,
                    "eliminationRate": 0.75,
                },
                {
                    "name": "Tom Thorn",
                    "side": "outlaw",
                    "winRate": 0.65,
                    "uplift": 0.1,
                    "eliminationRate": 0.65,
                },
            ],
            "characterPairs": [
                {
                    "firstName": "Dalon Ranger",
                    "secondName": "Dalon Ranger",
                    "side": "law",
                    "winRate": 0.5,
                    "uplift": 0.05,
                }
            ],
            "matchups": [
                {
                    "lawName": "Dalon Ranger",
                    "outlawName": "Tom Thorn",
                    "lawWinRate": 0.4,
                },
                {
                    "lawName": "Dalon Ranger",
                    "outlawName": "Pearl Hat",
                    "lawWinRate": 0.6,
                },
            ],
        }
        write_json(run_dir / "evaluations" / "iteration_000002.json", report)
        write_json(run_dir / "evaluations" / "latest.json", report)

        output = Path(temporary) / "about-data.js"
        subprocess.run(
            [
                sys.executable,
                str(repo / "tools" / "build_about_data.py"),
                str(run_dir),
                str(output),
            ],
            cwd=repo,
            check=True,
        )
        text = output.read_text()
        assignment = next(
            line for line in text.splitlines()
            if line.startswith("window.BANG_ABOUT_DATA=")
        )
        payload = json.loads(assignment.split("=", 1)[1].rstrip(";"))
        if payload["training"]["games"] != 4:
            raise RuntimeError("training games were not aggregated")
        if payload["training"]["points"][-1]["iteration"] != 2:
            raise RuntimeError("training timeline did not reach the checkpoint")
        if payload["style"]["defenseRate"] != 1:
            raise RuntimeError("opportunity-adjusted style was not aggregated")
        if payload["style"]["cards"][0]["pickRate"] != 0.75:
            raise RuntimeError("card pick rate was not aggregated")
        if payload["evaluation"]["latest"]["outlawWinRate"] != 0.55:
            raise RuntimeError("evaluation summary is incomplete")
        if payload["training"]["opponentResults"]["frozenCurrent"] != {
            "games": 8,
            "winRate": 0.5,
        }:
            raise RuntimeError("league opponent results were not aggregated")
        if payload["training"]["opponentResults"]["heuristic"]["winRate"] != 1:
            raise RuntimeError("scripted-opponent results were not aggregated")
        pairs = payload["evaluation"]["latest"]["pairs"]
        if len(pairs) != 1 or abs(pairs[0]["synergy"] - (0.05 - 0.2)) > 1e-9:
            raise RuntimeError("pair synergy was not computed against the parts")
        if payload["evaluation"]["latest"]["matchupSpread"] != 0.6 - 0.4:
            raise RuntimeError("matchup spread was not computed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
