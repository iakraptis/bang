#!/usr/bin/env python3

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def main() -> int:
    binary = Path(sys.argv[1]).resolve()
    repo = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="bang-training-test-") as temporary:
        run_dir = Path(temporary) / "campaign"
        campaign = run([
            sys.executable,
            str(repo / "tools" / "train.py"),
            "--repo-root", str(repo),
            "--run-dir", str(run_dir),
            "--rehearsal",
        ], repo)
        if "champion" not in campaign.stdout:
            raise RuntimeError("rehearsal did not report champion selection")
        state = json.loads((run_dir / "state.json").read_text())
        if state["phase"] != "complete" or not state["championCheckpoint"]:
            raise RuntimeError("rehearsal did not commit a completed campaign")
        champion = Path(state["championCheckpoint"])
        required = [
            champion,
            Path(str(champion) + ".metadata.json"),
            champion.parent / "canary.json",
            champion.parent / "inference.json",
            run_dir / "final" / "summary.md",
            run_dir / "main" / "metrics.jsonl",
            run_dir / "evaluations" / "latest.json",
            run_dir / "evaluations" / "latest.md",
            Path(str(run_dir / "main" / "latest.pt") + ".commit.json"),
        ]
        missing = [str(path) for path in required if not path.is_file()]
        if missing:
            raise RuntimeError(f"rehearsal artifacts are missing: {missing}")
        metrics = [
            json.loads(line)
            for line in (run_dir / "main" / "metrics.jsonl").read_text().splitlines()
            if line
        ]
        expected_metrics = {
            "rolloutStepsPerSecond",
            "endToEndStepsPerSecond",
            "trainingDecisionsPerSecond",
            "approximateKl",
            "entropy",
            "clipFraction",
            "formWinRate",
            "learnerLawWinRate",
            "learnerOutlawWinRate",
            "opponentPerformance",
            "style",
            "normalizedEntropy",
            "topActionProbability",
            "criticExplainedVariance",
            "gradientNorm",
        }
        if not metrics or not expected_metrics.issubset(metrics[-1]):
            raise RuntimeError("training metrics are incomplete")
        expected_style = {
            "attackRate",
            "defenseRate",
            "swapRate",
            "rearTargetRate",
            "cardsPerTurn",
            "cards",
            "totalDecisions",
            "sampleRate",
        }
        if not expected_style.issubset(metrics[-1]["style"]):
            raise RuntimeError("play-style metrics are incomplete")
        for marker in (
            "STRENGTH",
            "STYLE",
            "HEALTH",
            "BENCHMARK",
            "SELF-EVAL",
            "ckpt",
        ):
            if marker not in campaign.stdout:
                raise RuntimeError(f"console telemetry is missing {marker}")
        main_snapshots = [
            entry for entry in state["archive"] if entry["role"] == "main"
        ]
        if not main_snapshots or {
            "heuristic", "bootstrap", "previousChunk"
        } - set(main_snapshots[-1].get("benchmarks", {})):
            raise RuntimeError("fixed chunk benchmarks are incomplete")
        if not main_snapshots[-1].get("selfEvaluation"):
            raise RuntimeError("checkpoint self-evaluation was not recorded")
        self_evaluation = json.loads(
            (run_dir / "evaluations" / "latest.json").read_text()
        )
        if (
            self_evaluation["games"] != 8
            or len(self_evaluation["characters"]) != 24
            or not self_evaluation["characterPairs"]
            or not self_evaluation["matchups"]
            or "reserveEntryRate" not in self_evaluation["characters"][0]
        ):
            raise RuntimeError("checkpoint self-evaluation is incomplete")
        if len({entry["sha256"] for entry in state["archive"]}) < 2:
            raise RuntimeError("PPO update did not produce distinct policy weights")
        canary = json.loads(
            run([str(binary), "checkpoint-canary", str(champion)], repo).stdout
        )
        if not canary["ok"] or canary["logits"] < 2:
            raise RuntimeError("checkpoint canary did not validate policy tensors")
        web = json.loads(
            run([str(binary), "web-smoke", str(champion)], repo).stdout
        )
        if not web["ok"] or not web["ai"]["trained"]:
            raise RuntimeError("trained checkpoint was not used by the browser session")
        static_policy = Path(temporary) / "static-policy.json"
        run([
            sys.executable,
            str(repo / "tools" / "export_static_policy.py"),
            str(champion),
            str(static_policy),
        ], repo)
        exported = json.loads(static_policy.read_text())
        if (
            exported["format"] != "bang_static_policy_v1"
            or exported["metadata"]["iteration"] <= 0
            or not exported["parameters"]
        ):
            raise RuntimeError("static policy export is incomplete")
        static_web = json.loads(
            run([
                str(binary),
                "static-policy-smoke",
                str(static_policy),
            ], repo).stdout
        )
        if not static_web["ok"] or not static_web["ai"]["trained"]:
            raise RuntimeError("static policy was not used by the browser session")
        evaluation = json.loads(
            run([
                str(binary), "evaluate", str(champion), "-", "2", "98765", "2"
            ], repo).stdout
        )
        if evaluation["games"] != 4 or evaluation["truncated"] != 0:
            raise RuntimeError("paired evaluation did not finish expected games")
        self_play = json.loads(
            run([
                str(binary), "self-evaluate", str(champion), "8", "98765", "2"
            ], repo).stdout
        )
        if (
            self_play["games"] != 8
            or self_play["terminalGames"] != 8
            or len(self_play["characters"]) != 24
        ):
            raise RuntimeError("self-evaluation CLI output is incomplete")
        resumed = run([
            sys.executable,
            str(repo / "tools" / "train.py"),
            "--repo-root", str(repo),
            "--run-dir", str(run_dir),
            "--rehearsal",
        ], repo)
        if "already complete" not in resumed.stdout:
            raise RuntimeError("completed campaign did not resume idempotently")

        broken = Path(temporary) / "broken.pt"
        latest = run_dir / "main" / "latest.pt"
        for suffix in ("", ".metadata.json", ".optimizer.pt"):
            shutil.copy2(Path(str(latest) + suffix), Path(str(broken) + suffix))
        incomplete = subprocess.run([
            str(binary), "train",
            "--iterations", str(int(metrics[-1]["iteration"]) + 1),
            "--batch-games", "2",
            "--update-epochs", "1",
            "--minibatches", "1",
            "--torch-threads", "1",
            "--checkpoint-dir", str(Path(temporary) / "broken-resume"),
            "--resume", str(broken),
        ], cwd=repo, text=True, capture_output=True)
        if incomplete.returncode == 0 or "commit" not in incomplete.stderr:
            raise RuntimeError("incomplete checkpoint bundle was accepted for resume")

        resume_dir = Path(temporary) / "resume-campaign"
        stopped = run([
            sys.executable,
            str(repo / "tools" / "train.py"),
            "--repo-root", str(repo),
            "--run-dir", str(resume_dir),
            "--rehearsal",
            "--stop-after-chunks", "0",
        ], repo)
        if "rerun the same command to resume" not in stopped.stdout:
            raise RuntimeError("campaign did not stop at a committed boundary")
        run([
            sys.executable,
            str(repo / "tools" / "train.py"),
            "--repo-root", str(repo),
            "--run-dir", str(resume_dir),
            "--rehearsal",
        ], repo)
        resumed_state = json.loads((resume_dir / "state.json").read_text())
        if resumed_state["phase"] != "complete":
            raise RuntimeError("stopped campaign did not resume to completion")

        # Optimizer knobs may be retuned straight into an existing run; the
        # architecture and the rollout shape may not, because the checkpoint
        # would not mean the same thing afterwards.
        def resume_with(*extra: str) -> subprocess.CompletedProcess[str]:
            return subprocess.run([
                sys.executable,
                str(repo / "tools" / "train.py"),
                "--repo-root", str(repo),
                "--run-dir", str(resume_dir),
                "--rehearsal",
                *extra,
            ], cwd=repo, text=True, capture_output=True)

        original_kl = resumed_state["settings"]["targetKl"]
        retuned_kl = original_kl / 2
        structural = resume_with("--seed", "24680")
        if structural.returncode == 0 or "seed" not in structural.stderr:
            raise RuntimeError("resume accepted a change to the run's shape")
        retuned = resume_with("--target-kl", str(retuned_kl))
        if retuned.returncode != 0:
            raise RuntimeError("a retuned optimizer setting did not resume the campaign")
        retuned_state = json.loads((resume_dir / "state.json").read_text())
        if retuned_state["settings"]["targetKl"] != retuned_kl:
            raise RuntimeError("resume did not persist the retuned setting")
        history = retuned_state.get("retunes") or []
        if len(history) != 1 or history[0]["changed"]["targetKl"]["from"] != original_kl:
            raise RuntimeError("resume did not journal the retuned setting")
        settled = resume_with("--target-kl", str(retuned_kl))
        if settled.returncode != 0 or len(
            (json.loads((resume_dir / "state.json").read_text()).get("retunes") or [])
        ) != 1:
            raise RuntimeError("an unchanged resume journaled a spurious retune")

        # The larger v3 architecture must survive every path that rebuilds a
        # module from checkpoint metadata rather than from a hardcoded preset.
        large_dir = Path(temporary) / "large"
        run([
            str(binary), "train",
            "--model", "v3",
            "--iterations", "1",
            "--batch-games", "2",
            "--update-epochs", "1",
            "--minibatches", "1",
            "--torch-threads", "1",
            "--checkpoint-dir", str(large_dir),
            "--opponent", "heuristic:tactical:1.0",
        ], repo)
        large = large_dir / "latest.pt"
        large_canary = json.loads(
            run([str(binary), "checkpoint-canary", str(large)], repo).stdout
        )
        if (
            not large_canary["ok"]
            or large_canary["modelType"] != "bang_structured_v3"
            or large_canary["parameters"] < 2_000_000
        ):
            raise RuntimeError("v3 checkpoint canary did not validate the larger model")
        large_web = json.loads(
            run([str(binary), "web-smoke", str(large)], repo).stdout
        )
        if not large_web["ok"] or not large_web["ai"]["trained"]:
            raise RuntimeError("v3 checkpoint was not loaded by the browser session")
        large_static = Path(temporary) / "static-policy-v3.json"
        run([
            sys.executable,
            str(repo / "tools" / "export_static_policy.py"),
            str(large),
            str(large_static),
        ], repo)
        exported_large = json.loads(large_static.read_text())
        residual_blocks = {
            entry["name"].split(".")[1]
            for entry in exported_large["parameters"]
            if entry["name"].startswith("residuals.")
        }
        if (
            exported_large["metadata"]["modelType"] != "bang_structured_v3"
            or len(residual_blocks) != 4
        ):
            raise RuntimeError("v3 static export did not carry every residual block")
        large_static_web = json.loads(
            run([str(binary), "static-policy-smoke", str(large_static)], repo).stdout
        )
        if not large_static_web["ok"] or not large_static_web["ai"]["trained"]:
            raise RuntimeError("v3 static policy was not used by the browser session")
        crossed = subprocess.run([
            str(binary), "train",
            "--model", "v2",
            "--iterations", "2",
            "--batch-games", "2",
            "--update-epochs", "1",
            "--minibatches", "1",
            "--torch-threads", "1",
            "--checkpoint-dir", str(Path(temporary) / "crossed"),
            "--resume", str(large),
        ], cwd=repo, text=True, capture_output=True)
        if crossed.returncode == 0 or "bang_structured_v3" not in crossed.stderr:
            raise RuntimeError("a v3 checkpoint was accepted for a v2 resume")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
