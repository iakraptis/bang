#!/usr/bin/env python3
"""Build the small, static dataset used by the training story page."""

from __future__ import annotations

import argparse
import json
import math
from collections import defaultdict
from pathlib import Path
from typing import Any


def read_json(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise FileNotFoundError(f"missing training artifact: {path}")
    value = json.loads(path.read_text())
    if not isinstance(value, dict):
        raise ValueError(f"expected a JSON object: {path}")
    return value


def read_metrics(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        raise FileNotFoundError(f"missing training metrics: {path}")
    rows: list[dict[str, Any]] = []
    with path.open() as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                # A live trainer can be in the middle of appending its final line.
                if not stream.read().strip():
                    break
                raise ValueError(
                    f"invalid metric JSON on line {line_number}: {path}"
                ) from None
            if not isinstance(row, dict):
                raise ValueError(
                    f"expected a metric object on line {line_number}: {path}"
                )
            rows.append(row)
    if not rows:
        raise ValueError(f"training metrics are empty: {path}")
    return rows


def finite_mean(rows: list[dict[str, Any]], key: str) -> float | None:
    values = [
        float(row[key])
        for row in rows
        if isinstance(row.get(key), (int, float))
        and math.isfinite(float(row[key]))
    ]
    return sum(values) / len(values) if values else None


def sample_training(rows: list[dict[str, Any]], buckets: int = 64) -> list[dict[str, Any]]:
    width = max(1, math.ceil(len(rows) / buckets))
    points: list[dict[str, Any]] = []
    for start in range(0, len(rows), width):
        bucket = rows[start : start + width]
        last = bucket[-1]
        point: dict[str, Any] = {
            "iteration": int(last["iteration"]),
            "environmentSteps": int(last["cumulativeEnvironmentSteps"]),
        }
        for output, source in (
            ("winRate", "learnerWinRate"),
            ("entropy", "normalizedEntropy"),
            ("confidence", "topActionProbability"),
        ):
            value = finite_mean(bucket, source)
            if value is not None:
                point[output] = value
        points.append(point)
    return points


def aggregate_style(rows: list[dict[str, Any]]) -> dict[str, Any]:
    totals: defaultdict[str, float] = defaultdict(float)
    cards: defaultdict[str, dict[str, float]] = defaultdict(
        lambda: {"selected": 0, "opportunities": 0, "used": 0}
    )
    for row in rows:
        style = row.get("style")
        if not isinstance(style, dict):
            continue
        for key in (
            "attacks",
            "attackOpportunities",
            "defenses",
            "defenseOpportunities",
            "swaps",
            "swapOpportunities",
            "abilities",
            "abilityOpportunities",
            "rearTargets",
            "opponentTargets",
            "cardsPlayed",
            "equipmentPlayed",
            "turns",
        ):
            totals[key] += float(style.get(key, 0))
        for card in style.get("cards", []):
            if not isinstance(card, dict) or not card.get("card"):
                continue
            target = cards[str(card["card"])]
            for key in target:
                target[key] += float(card.get(key, 0))

    def rate(numerator: str, denominator: str) -> float:
        return (
            totals[numerator] / totals[denominator]
            if totals[denominator]
            else 0.0
        )

    ranked_cards = []
    for name, values in cards.items():
        opportunities = values["opportunities"]
        ranked_cards.append(
            {
                "name": name,
                "pickRate": values["selected"] / opportunities
                if opportunities
                else 0.0,
                "opportunities": int(opportunities),
                "used": int(values["used"]),
            }
        )
    ranked_cards.sort(key=lambda card: (-card["pickRate"], card["name"]))
    return {
        "windowIterations": len(rows),
        "attackRate": rate("attacks", "attackOpportunities"),
        "defenseRate": rate("defenses", "defenseOpportunities"),
        "swapRate": rate("swaps", "swapOpportunities"),
        "abilityRate": rate("abilities", "abilityOpportunities"),
        "rearTargetRate": rate("rearTargets", "opponentTargets"),
        "cardsPerTurn": rate("cardsPlayed", "turns"),
        "equipmentShare": rate("equipmentPlayed", "cardsPlayed"),
        "cards": ranked_cards,
    }


def aggregate_opponents(rows: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    """Win rate against each kind of league opponent over the sampled window.

    `opponentPerformance` is per-iteration, so the entries add up; the
    chunk-to-date field next to it would double count.
    """
    groups = {
        key: {"wins": 0.0, "games": 0.0}
        for key in ("frozenCurrent", "pfspHistory", "heuristic", "random")
    }
    for row in rows:
        entries = row.get("opponentPerformance")
        if not isinstance(entries, list):
            continue
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            kind = str(entry.get("kind", ""))
            if kind == "checkpoint":
                key = (
                    "frozenCurrent"
                    if str(entry.get("id", "")) == "current"
                    else "pfspHistory"
                )
            elif kind in ("heuristic", "random"):
                key = kind
            else:
                continue
            groups[key]["wins"] += float(entry.get("wins", 0))
            groups[key]["games"] += float(entry.get("games", 0))
    return {
        key: {
            "games": int(value["games"]),
            "winRate": value["wins"] / value["games"] if value["games"] else 0.0,
        }
        for key, value in groups.items()
    }


def pair_summary(
    pairs: list[Any], uplifts: dict[str, float], count: int = 3
) -> list[dict[str, Any]]:
    """Strongest and weakest same-side duos, with their synergy over the parts."""
    ranked = []
    for pair in pairs:
        if not isinstance(pair, dict):
            continue
        first = str(pair["firstName"])
        second = str(pair["secondName"])
        if first not in uplifts or second not in uplifts:
            continue
        parts = uplifts[first] + uplifts[second]
        ranked.append(
            {
                "first": first,
                "second": second,
                "side": str(pair["side"]),
                "winRate": float(pair["winRate"]),
                "uplift": float(pair["uplift"]),
                "parts": parts,
                "synergy": float(pair["uplift"]) - parts,
            }
        )
    ranked.sort(key=lambda pair: -pair["uplift"])
    if len(ranked) <= 2 * count:
        return ranked
    return ranked[:count] + ranked[-count:]


def matchup_summary(matchups: list[Any], count: int = 4) -> list[dict[str, Any]]:
    """The most one-sided cells of the law × outlaw grid, best first."""
    ranked = [
        {
            "law": str(matchup["lawName"]),
            "outlaw": str(matchup["outlawName"]),
            "lawWinRate": float(matchup["lawWinRate"]),
        }
        for matchup in matchups
        if isinstance(matchup, dict)
    ]
    ranked.sort(key=lambda matchup: -matchup["lawWinRate"])
    if len(ranked) <= 2 * count:
        return ranked
    return ranked[:count] + ranked[-count:]


def evaluation_summary(run_dir: Path) -> dict[str, Any]:
    reports = []
    for path in sorted((run_dir / "evaluations").glob("iteration_*.json")):
        report = read_json(path)
        reports.append(
            {
                "iteration": int(report["iteration"]),
                "lawWinRate": float(report["lawWinRate"]),
                "outlawWinRate": float(report["outlawWinRate"]),
            }
        )
    if not reports:
        raise ValueError(f"no checkpoint evaluations found under {run_dir}")

    latest = read_json(run_dir / "evaluations" / "latest.json")
    characters = [
        {
            "name": str(character["name"]),
            "side": str(character["side"]),
            "winRate": float(character["winRate"]),
            "uplift": float(character["uplift"]),
            "eliminationRate": float(character["eliminationRate"]),
        }
        for character in latest["characters"]
    ]
    uplifts = {
        character["name"]: character["uplift"] for character in characters
    }
    matchups = matchup_summary(latest.get("matchups") or [])
    return {
        "points": reports,
        "latest": {
            "iteration": int(latest["iteration"]),
            "games": int(latest["games"]),
            "lawWinRate": float(latest["lawWinRate"]),
            "outlawWinRate": float(latest["outlawWinRate"]),
            "meanTurns": float(latest.get("meanTurns", 0.0)),
            "characters": characters,
            "pairs": pair_summary(latest.get("characterPairs") or [], uplifts),
            "matchups": matchups,
            "matchupSpread": (
                max(matchup["lawWinRate"] for matchup in matchups)
                - min(matchup["lawWinRate"] for matchup in matchups)
                if matchups
                else 0.0
            ),
        },
    }


def build_data(run_dir: Path) -> dict[str, Any]:
    manifest = read_json(run_dir / "manifest.json")
    metadata = read_json(run_dir / "main" / "latest.pt.metadata.json")
    metrics = read_metrics(run_dir / "main" / "metrics.jsonl")
    checkpoint_iteration = int(metadata["iteration"])
    metrics = [
        row for row in metrics
        if int(row.get("iteration", -1)) <= checkpoint_iteration
    ]
    if not metrics or int(metrics[-1]["iteration"]) != checkpoint_iteration:
        raise ValueError("metrics do not contain the latest checkpoint iteration")
    style_rows = metrics[-min(100, len(metrics)) :]
    return {
        "source": {
            "run": run_dir.name,
            "metrics": "main/metrics.jsonl",
            "evaluation": "evaluations/latest.json",
        },
        "training": {
            "iterations": int(metadata["iteration"]),
            "environmentSteps": int(metadata["environmentSteps"]),
            "learnerDecisions": int(metadata["learnerDecisions"]),
            "games": sum(int(row.get("games", 0)) for row in metrics),
            "elapsedSeconds": float(metadata["elapsedSeconds"]),
            "parameterCount": int(metadata["parameterCount"]),
            "observationSize": int(metadata["observationSize"]),
            "actionFeatureSize": int(metadata["actionFeatureSize"]),
            "privilegedSize": int(metadata["privilegedSize"]),
            "batchGames": int(manifest["settings"]["batchGames"]),
            "learningRate": float(manifest["settings"]["learningRate"]),
            "targetKl": float(manifest["settings"]["targetKl"]),
            "updateEpochs": int(manifest["settings"]["updateEpochs"]),
            "minibatches": int(manifest["settings"]["minibatches"]),
            "entropyCoef": float(manifest["settings"]["entropyCoef"]),
            "opponents": manifest["opponents"],
            "opponentResults": aggregate_opponents(style_rows),
            "rolloutSeconds": sum(
                float(row.get("rolloutSeconds", 0.0)) for row in metrics
            ),
            "updateSeconds": sum(
                float(row.get("updateSeconds", 0.0)) for row in metrics
            ),
            "points": sample_training(metrics),
        },
        "evaluation": evaluation_summary(run_dir),
        "style": aggregate_style(style_rows),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    payload = json.dumps(build_data(args.run_dir), separators=(",", ":"))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_text(
        "/* Generated from repository training artifacts. */\n"
        f"window.BANG_ABOUT_DATA={payload};\n"
        'window.dispatchEvent(new Event("bang-about-data"));\n'
    )
    temporary.replace(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
