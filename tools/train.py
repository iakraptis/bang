#!/usr/bin/env python3
"""Resumable from-scratch League PPO campaign for BANG! The Duel."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

if os.name == "nt":
    import ctypes
    import msvcrt
else:
    import fcntl


def prevent_system_sleep() -> None:
    """Keep Windows from sleeping while this campaign process lives.

    An idle-timer sleep suspends training mid-iteration and still spends the
    wall-clock budget. The request ends with the process; the display may sleep.
    """
    if os.name != "nt":
        return
    es_continuous, es_system_required = 0x80000000, 0x00000001
    ctypes.windll.kernel32.SetThreadExecutionState(es_continuous | es_system_required)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Train and select a BANG! The Duel agent with League PPO."
    )
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--run-dir", type=Path, default=Path("runs/bang_league_ppo_v3_lr2e4"))
    parser.add_argument("--hours", type=float, default=48.0)
    parser.add_argument("--final-reserve-hours", type=float, default=1.0)
    parser.add_argument("--model", choices=("v2", "v3"), default="v3")
    parser.add_argument("--batch-games", type=int, default=256)
    parser.add_argument("--chunk-iterations", type=int, default=10)
    parser.add_argument("--update-epochs", type=int, default=4)
    parser.add_argument("--minibatches", type=int, default=8)
    parser.add_argument("--torch-threads", type=int, default=min(6, os.cpu_count() or 1))
    # Where PPO updates run. Rollouts and evaluations stay on the CPU. Not part
    # of settings(): it changes speed, not the experiment, so a run may resume
    # on different hardware.
    parser.add_argument("--device", default="cpu", help="cpu, cuda, or cuda:N")
    # v3 is ten times v2's width and moves the policy much further per gradient
    # step: at matched iteration counts the measured approximate KL was 0.0020 for
    # v2 at 3e-4 against 0.0042 for v3 at 2e-4. That keeps v3 pressed against this
    # trust region — it tripped the KL early stop on 100% of a 500-iteration window
    # and averaged 2.93 of 4 update epochs. Widening the region to 0.035 was tried
    # at iteration 5491 and reverted: it bought ~0.8 epochs for about 300
    # iterations, then re-saturated, and one window averaged KL 0.092 while the
    # policy took oversized steps. Measured against the finished v2 run it cost 6.5
    # points of win rate, 45.4% to 38.9% over 2048 paired games. The early stopping
    # is a symptom of v3's step size, not the thing holding it back, and 0.015 is
    # doing real work.
    parser.add_argument("--learning-rate", type=float, default=2e-4)
    parser.add_argument("--entropy-coef", type=float, default=0.01)
    parser.add_argument("--target-kl", type=float, default=0.015)
    # Magnetic Mirror Descent: adds alpha * KL(policy || magnet) to the PPO loss.
    # The magnet starts as uniform play and moves to the current policy every
    # --magnet-interval iterations (0 keeps it uniform). Alpha 0 is plain PPO.
    parser.add_argument("--mmd-alpha", type=float, default=0.0)
    parser.add_argument("--magnet-interval", type=int, default=0)
    parser.add_argument("--scouting-pairs", type=int, default=64)
    parser.add_argument("--final-pairs", type=int, default=512)
    parser.add_argument("--self-eval-games", type=int, default=2048)
    parser.add_argument("--self-eval-interval-chunks", type=int, default=5)
    parser.add_argument("--max-archive", type=int, default=10)
    parser.add_argument("--max-finalists", type=int, default=8)
    parser.add_argument("--seed", type=int, default=470_001)
    parser.add_argument("--max-chunks", type=int, default=0)
    parser.add_argument("--stop-after-chunks", type=int, default=-1, help=argparse.SUPPRESS)
    parser.add_argument("--rehearsal", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.rehearsal:
        args.batch_games = 4
        args.chunk_iterations = 1
        args.update_epochs = 1
        args.minibatches = 1
        args.torch_threads = min(2, args.torch_threads)
        args.scouting_pairs = 1
        args.final_pairs = 1
        args.self_eval_games = 8
        args.self_eval_interval_chunks = 1
        args.max_archive = 3
        args.max_finalists = 3
        args.max_chunks = args.max_chunks or 1
    return args


def validate_args(args: argparse.Namespace) -> None:
    positive = {
        "hours": args.hours,
        "batch-games": args.batch_games,
        "chunk-iterations": args.chunk_iterations,
        "update-epochs": args.update_epochs,
        "minibatches": args.minibatches,
        "torch-threads": args.torch_threads,
        "learning-rate": args.learning_rate,
        "entropy-coef": args.entropy_coef,
        "target-kl": args.target_kl,
        "scouting-pairs": args.scouting_pairs,
        "final-pairs": args.final_pairs,
        "self-eval-interval-chunks": args.self_eval_interval_chunks,
        "max-archive": args.max_archive,
        "max-finalists": args.max_finalists,
    }
    for name, value in positive.items():
        if not math.isfinite(value) or value <= 0:
            raise SystemExit(f"--{name} must be positive")
    if args.final_reserve_hours < 0 or args.final_reserve_hours >= args.hours:
        raise SystemExit("--final-reserve-hours must be in [0, --hours)")
    if args.max_chunks < 0 or args.stop_after_chunks < -1:
        raise SystemExit("chunk limits must be non-negative")
    if args.mmd_alpha < 0 or args.magnet_interval < 0:
        raise SystemExit("--mmd-alpha and --magnet-interval must be non-negative")
    if not re.fullmatch(r"cpu|cuda(:\d+)?", args.device):
        raise SystemExit("--device must be cpu, cuda, or cuda:N")
    if args.self_eval_games < 0:
        raise SystemExit("--self-eval-games must be non-negative")


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z")


def atomic_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    os.replace(temporary, path)


def read_json(path: Path) -> Any:
    return json.loads(path.read_text())


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def metadata_path(checkpoint: Path) -> Path:
    return Path(str(checkpoint) + ".metadata.json")


def optimizer_path(checkpoint: Path) -> Path:
    return Path(str(checkpoint) + ".optimizer.pt")


def require_checkpoint(path: Path) -> Path:
    path = path.resolve()
    if not path.is_file() or not metadata_path(path).is_file():
        raise RuntimeError(f"incomplete checkpoint bundle: {path}")
    return path


def format_duration(seconds: float) -> str:
    seconds = max(0, int(seconds))
    hours, remainder = divmod(seconds, 3600)
    minutes, secs = divmod(remainder, 60)
    if hours:
        return f"{hours}h {minutes:02d}m"
    if minutes:
        return f"{minutes}m {secs:02d}s"
    return f"{secs}s"


def wilson_interval(wins: int, games: int) -> tuple[float, float]:
    """Approximate 95% binomial interval for a displayed benchmark rate."""
    if games <= 0:
        return (0.0, 0.0)
    z = 1.959963984540054
    rate = wins / games
    denominator = 1.0 + z * z / games
    center = (rate + z * z / (2.0 * games)) / denominator
    radius = (
        z
        * math.sqrt(rate * (1.0 - rate) / games + z * z / (4.0 * games * games))
        / denominator
    )
    return (max(0.0, center - radius), min(1.0, center + radius))


def benchmark_text(label: str, result: dict[str, Any]) -> str:
    low, high = wilson_interval(
        int(result["candidateWins"]),
        int(result["games"]),
    )
    return (
        f"{label} {result['candidateWinRate'] * 100:.1f}% "
        f"[{low * 100:.1f}, {high * 100:.1f}]"
    )


class Console:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.color = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None
        self.stream: Any = None

    def style(self, text: str, code: str) -> str:
        return f"\033[{code}m{text}\033[0m" if self.color else text

    def line(self, text: str = "") -> None:
        if self.stream is None:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            self.stream = self.path.open("a", buffering=1)
        print(text, flush=True)
        clean = re.sub(r"\x1b\[[0-9;]*m", "", text)
        self.stream.write(f"{utc_now()} {clean}\n")

    def heading(self, text: str) -> None:
        self.line()
        self.line(self.style(text, "1;36"))
        self.line(self.style("━" * min(78, max(24, len(text))), "2"))

    def good(self, text: str) -> None:
        self.line(self.style("✓ " + text, "1;32"))

    def warn(self, text: str) -> None:
        self.line(self.style("⚠ " + text, "1;33"))


class Campaign:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.repo = args.repo_root.resolve()
        self.run_dir = (
            args.run_dir.resolve()
            if args.run_dir.is_absolute()
            else (self.repo / args.run_dir).resolve()
        )
        executable = "bang_duel.exe" if os.name == "nt" else "bang_duel"
        self.binary = self.repo / "build-train" / executable
        self.state_path = self.run_dir / "state.json"
        self.console = Console(self.run_dir / "progress.log")
        self.state: dict[str, Any] = {}
        self.stop_requested = False
        self.active_process: subprocess.Popen[str] | None = None
        self.lock: Any = None

    def settings(self) -> dict[str, Any]:
        return {
            "ruleset": "base_four_character",
            "model": self.args.model,
            "batchGames": self.args.batch_games,
            "chunkIterations": self.args.chunk_iterations,
            "updateEpochs": self.args.update_epochs,
            "minibatches": self.args.minibatches,
            "torchThreads": self.args.torch_threads,
            "learningRate": self.args.learning_rate,
            "entropyCoef": self.args.entropy_coef,
            "targetKl": self.args.target_kl,
            "seed": self.args.seed,
            # Recorded only when enabled, so runs from before MMD still resume.
            **({
                "mmdAlpha": self.args.mmd_alpha,
                "magnetInterval": self.args.magnet_interval,
            } if self.args.mmd_alpha > 0 else {}),
        }

    # Optimizer knobs a resume may simply change: they steer the next update
    # without invalidating the ones already taken, so a mid-campaign retune is a
    # legitimate thing to do and the run journals it. Everything else in
    # settings() fixes the shape of the run — the architecture, the rollout size,
    # the seed — and a checkpoint carries no meaning across a change to those.
    RETUNABLE = frozenset({"learningRate", "entropyCoef", "targetKl", "updateEpochs"})

    def apply_retune(self, stored: dict[str, Any]) -> None:
        current = self.settings()
        differing = {
            key for key in set(stored) | set(current)
            if stored.get(key) != current.get(key)
        }
        structural = sorted(differing - self.RETUNABLE)
        if structural:
            raise SystemExit(
                "resume settings differ in "
                + ", ".join(structural)
                + "; use the original flags or a new run directory"
            )
        record = {
            "at": utc_now(),
            "iteration": self.state.get("iteration"),
            "changed": {
                key: {"from": stored.get(key), "to": current.get(key)}
                for key in sorted(differing)
            },
        }
        self.state.setdefault("retunes", []).append(record)
        self.state["settings"] = current
        self.save_state()
        for key in sorted(differing):
            self.console.warn(
                f"retuned {key}: {stored.get(key)} → {current.get(key)}"
            )

    def evaluation_settings(self) -> dict[str, Any]:
        return {
            "selfEvalGames": self.args.self_eval_games,
            "selfEvalIntervalChunks": self.args.self_eval_interval_chunks,
            "selfEvalMethod": "greedy_checkpoint_self_play_v1",
        }

    def acquire_lock(self) -> None:
        self.run_dir.mkdir(parents=True, exist_ok=True)
        self.lock = (self.run_dir / "campaign.lock").open("a+")
        try:
            if os.name == "nt":
                # Byte-range lock on the first byte; released when the process exits.
                self.lock.seek(0)
                msvcrt.locking(self.lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                fcntl.flock(self.lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise SystemExit(f"another campaign owns {self.run_dir}") from error

    @property
    def stop_file(self) -> Path:
        return self.run_dir / "stop.request"

    def save_state(self) -> None:
        self.state["updatedAt"] = utc_now()
        atomic_json(self.state_path, self.state)

    def install_signals(self) -> None:
        def stop(_signum: int, _frame: Any) -> None:
            if self.stop_requested:
                return
            self.stop_requested = True
            self.console.warn("stop requested; finishing and checkpointing the PPO iteration")
            self.stop_file.touch()
            # Windows cannot send SIGINT to one child; a console Ctrl-C reaches
            # it directly, and the stop file covers every other stop.
            if self.active_process is not None and os.name != "nt":
                self.active_process.send_signal(signal.SIGINT)

        signal.signal(signal.SIGINT, stop)
        signal.signal(signal.SIGTERM, stop)

    def initialize(self) -> None:
        if not self.binary.is_file():
            raise SystemExit(f"training binary is missing: {self.binary}")
        if self.state_path.exists():
            self.state = read_json(self.state_path)
            if self.state.get("settings") != self.settings():
                self.apply_retune(self.state.get("settings") or {})
            evaluation_settings = self.state.get("evaluationSettings")
            if (
                evaluation_settings is not None
                and evaluation_settings != self.evaluation_settings()
            ):
                raise SystemExit(
                    "resume evaluation settings differ; use the original flags"
                )
            if evaluation_settings is None:
                self.state["evaluationSettings"] = self.evaluation_settings()
                self.save_state()
            if self.state["phase"] == "stopped":
                self.state["phase"] = "training"
                self.save_state()
            self.console.heading("Resuming BANG! League PPO")
            self.console.line(f"  run        {self.run_dir}")
            self.console.line(f"  iteration  {self.state['iteration']}")
            self.console.line(f"  chunks     {self.state['chunk']}")
            self.console.line(
                f"  remaining  {format_duration(self.state['deadlineEpoch'] - time.time())}"
            )
            return
        unexpected = [
            path for path in self.run_dir.iterdir()
            if path.name not in {"campaign.lock", "progress.log"}
        ]
        if unexpected:
            raise SystemExit(f"non-empty run directory has no state: {self.run_dir}")
        started = time.time()
        self.state = {
            "schemaVersion": 1,
            "createdAt": utc_now(),
            "deadlineEpoch": started + self.args.hours * 3600,
            "trainingDeadlineEpoch": started
            + (self.args.hours - self.args.final_reserve_hours) * 3600,
            "phase": "bootstrap",
            "bootstrapComplete": False,
            "iteration": 0,
            "chunk": 0,
            "mainCheckpoint": "",
            "archive": [],
            "championCheckpoint": "",
            "evaluationSettings": self.evaluation_settings(),
            "settings": self.settings(),
        }
        atomic_json(
            self.run_dir / "manifest.json",
            {
                "schemaVersion": 1,
                "algorithm": "bang_league_ppo_pfsp_v1",
                "settings": self.settings(),
                "opponents": {
                    "frozenCurrent": 0.5,
                    "pfspHistory": 0.4,
                    "heuristic": 0.05,
                    "random": 0.05,
                },
                "evaluation": self.evaluation_settings(),
            },
        )
        self.save_state()
        self.console.heading("BANG! The Duel · League PPO")
        self.console.line(f"  run        {self.run_dir}")
        self.console.line("  format     standard four-character duel · both sides")
        self.console.line(
            f"  hardware   {self.args.torch_threads} CPU threads · batched native inference"
            f" · learner {self.args.device}"
        )
        self.console.line(
            f"  model      {self.args.model} · "
            + ("2.34M deployable parameters" if self.args.model == "v3"
               else "222k deployable parameters")
        )
        self.console.line(
            f"  PPO        {self.args.batch_games} games × {self.args.chunk_iterations} updates/chunk"
        )
        if self.args.mmd_alpha > 0:
            magnet = (
                f"moves every {self.args.magnet_interval} updates"
                if self.args.magnet_interval > 0 else "stays uniform"
            )
            self.console.line(
                f"  MMD        alpha {self.args.mmd_alpha} · magnet starts uniform, {magnet}"
            )
        self.console.line(
            f"  budget     {format_duration(self.args.hours * 3600)} total · "
            f"{format_duration(self.args.final_reserve_hours * 3600)} final selection"
        )
        self.console.line("  objective  terminal ±1 · public + privileged critics · entropy retained")
        self.console.line("  league     50% current · 40% PFSP history · 5% heuristic · 5% random")
        if self.args.self_eval_games > 0:
            self.console.line(
                "  self-eval  "
                f"{self.args.self_eval_games} games every "
                f"{self.args.self_eval_interval_chunks} chunks"
            )

    def telemetry_eta(self, iteration: int, target_iteration: int) -> str:
        metrics_path = self.run_dir / "main" / "metrics.jsonl"
        durations: list[float] = []
        if metrics_path.is_file():
            for line in metrics_path.read_text().splitlines()[-10:]:
                try:
                    durations.append(float(json.loads(line)["iterationSeconds"]))
                except (KeyError, TypeError, ValueError, json.JSONDecodeError):
                    continue
        average = sum(durations) / len(durations) if durations else 0.0
        checkpoint_eta = average * max(0, target_iteration - iteration)
        training_left = max(
            0.0,
            float(self.state.get("trainingDeadlineEpoch", time.time())) - time.time(),
        )
        return (
            f" │ ckpt {format_duration(checkpoint_eta)}"
            f" · training {format_duration(training_left)} left"
        )

    def run_native(
        self,
        command: list[str],
        target_iteration: int | None = None,
    ) -> int:
        with (self.run_dir / "native.log").open("a", buffering=1) as log:
            log.write(f"\n{utc_now()} START {' '.join(command)}\n")
            self.active_process = subprocess.Popen(
                command,
                cwd=self.repo,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                bufsize=1,
            )
            assert self.active_process.stdout is not None
            for line in self.active_process.stdout:
                text = line.rstrip()
                log.write(line)
                if text:
                    match = re.match(r"^PPO\s+(\d+)\b", text)
                    if match and target_iteration is not None:
                        text += self.telemetry_eta(
                            int(match.group(1)),
                            target_iteration,
                        )
                    self.console.line("  " + text)
            code = self.active_process.wait()
            self.active_process = None
            log.write(f"{utc_now()} END exit={code}\n")
        if code not in {0, 75}:
            raise RuntimeError(f"native command failed with exit {code}; see native.log")
        return code

    def copy_checkpoint(self, source: Path, destination: Path, optimizer: bool = False) -> None:
        source = require_checkpoint(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        shutil.copy2(metadata_path(source), metadata_path(destination))
        if optimizer and optimizer_path(source).is_file():
            shutil.copy2(optimizer_path(source), optimizer_path(destination))

    def snapshot(self, checkpoint: Path, identifier: str, role: str) -> dict[str, Any]:
        destination = self.run_dir / "archive" / identifier / "policy.pt"
        self.copy_checkpoint(checkpoint, destination)
        metadata = read_json(metadata_path(destination))
        record = {
            "id": identifier,
            "path": str(destination),
            "role": role,
            "iteration": int(metadata["iteration"]),
            "sha256": sha256(destination),
            "active": True,
        }
        atomic_json(destination.parent / "snapshot.json", record)
        return record

    def training_command(
        self,
        target: int,
        opponents: list[tuple[str, str, float, Path | None]],
        resume: Path | None,
    ) -> list[str]:
        main = self.run_dir / "main"
        command = [
            str(self.binary),
            "train",
            "--model", self.args.model,
            "--iterations", str(target),
            "--batch-games", str(self.args.batch_games),
            "--update-epochs", str(self.args.update_epochs),
            "--minibatches", str(self.args.minibatches),
            "--torch-threads", str(self.args.torch_threads),
            "--learning-rate", str(self.args.learning_rate),
            "--entropy-coef", str(self.args.entropy_coef),
            "--target-kl", str(self.args.target_kl),
            "--seed", str(self.args.seed),
            "--checkpoint-dir", str(main),
            "--stop-file", str(self.stop_file),
            "--device", self.args.device,
            "--mmd-alpha", str(self.args.mmd_alpha),
            "--magnet-interval", str(self.args.magnet_interval),
        ]
        if resume is not None:
            command.extend(["--resume", str(resume)])
        for kind, identifier, probability, checkpoint in opponents:
            if kind == "checkpoint":
                assert checkpoint is not None
                value = f"checkpoint:{identifier}:{checkpoint}:{probability}"
            else:
                value = f"{kind}:{identifier}:{probability}"
            command.extend(["--opponent", value])
        return command

    def bootstrap(self) -> None:
        if self.state["bootstrapComplete"]:
            return
        self.console.heading("Bootstrap · scratch policy vs anchors")
        command = self.training_command(
            1,
            [("heuristic", "heuristic", 0.7, None), ("random", "random", 0.3, None)],
            None,
        )
        self.run_native(command, 1)
        latest = require_checkpoint(self.run_dir / "main" / "latest.pt")
        bootstrap = self.snapshot(latest, "bootstrap_000001", "bootstrap")
        self.state.update({
            "bootstrapComplete": True,
            "phase": "training",
            "iteration": 1,
            "mainCheckpoint": str(latest),
            "archive": [bootstrap],
        })
        self.save_state()
        self.console.good("scratch bootstrap checkpoint committed")

    def evaluate(self, candidate: Path, opponent: Path | None, pairs: int, seed: int) -> dict[str, Any]:
        command = [
            str(self.binary),
            "evaluate",
            str(require_checkpoint(candidate)),
            str(require_checkpoint(opponent)) if opponent else "-",
            str(pairs),
            str(seed),
            str(self.args.torch_threads),
        ]
        result = subprocess.run(command, cwd=self.repo, text=True, capture_output=True)
        if result.returncode != 0:
            raise RuntimeError(f"evaluation failed: {result.stderr.strip()}")
        return json.loads(result.stdout.strip().splitlines()[-1])

    def self_evaluate(
        self,
        checkpoint: Path,
        games: int,
        seed: int,
        chunk: int,
        iteration: int,
    ) -> dict[str, Any]:
        checkpoint = require_checkpoint(checkpoint)
        started = time.monotonic()
        result = subprocess.run(
            [
                str(self.binary),
                "self-evaluate",
                str(checkpoint),
                str(games),
                str(seed),
                str(self.args.torch_threads),
            ],
            cwd=self.repo,
            text=True,
            capture_output=True,
        )
        elapsed = time.monotonic() - started
        if result.returncode != 0:
            raise RuntimeError(
                f"self-evaluation failed: {result.stderr.strip()}"
            )
        report = json.loads(result.stdout.strip().splitlines()[-1])
        report.update({
            "checkpoint": str(checkpoint),
            "checkpointSha256": sha256(checkpoint),
            "chunk": chunk,
            "iteration": iteration,
            "createdAt": utc_now(),
            "elapsedSeconds": elapsed,
            "gamesPerSecond": games / max(elapsed, 1e-9),
        })
        evaluation_dir = self.run_dir / "evaluations"
        report_path = evaluation_dir / f"iteration_{iteration:06d}.json"
        atomic_json(report_path, report)
        atomic_json(evaluation_dir / "latest.json", report)
        self.write_self_evaluation_markdown(
            evaluation_dir / f"iteration_{iteration:06d}.md",
            report,
        )
        self.write_self_evaluation_markdown(
            evaluation_dir / "latest.md",
            report,
        )

        characters = report["characters"]
        leaders: list[str] = []
        for side in ("law", "outlaw"):
            side_rows = [
                row
                for row in characters
                if row["side"] == side and row["games"] > 0
            ]
            strongest = max(side_rows, key=lambda row: row["winRate"])
            weakest = min(side_rows, key=lambda row: row["winRate"])
            leaders.append(
                f"{side.title()} {strongest['name']} "
                f"{strongest['winRate'] * 100:.1f}%"
                f" / {weakest['name']} {weakest['winRate'] * 100:.1f}%"
            )
        self.console.line(
            "  SELF-EVAL │ "
            f"{games} games · {format_duration(elapsed)} · "
            f"{report['gamesPerSecond']:.1f} games/s · "
            f"Law {report['lawWinRate'] * 100:.1f}% · "
            f"{report['meanTurns']:.1f} turns/game"
        )
        self.console.line("    characters │ " + " · ".join(leaders))
        self.console.line(f"    report     {report_path}")
        return {
            "path": str(report_path),
            "games": games,
            "elapsedSeconds": elapsed,
            "gamesPerSecond": report["gamesPerSecond"],
            "lawWinRate": report["lawWinRate"],
            "outlawWinRate": report["outlawWinRate"],
        }

    @staticmethod
    def write_self_evaluation_markdown(
        path: Path,
        report: dict[str, Any],
    ) -> None:
        def interval(row: dict[str, Any]) -> str:
            low, high = row["confidence95"]
            return f"{low * 100:.1f}–{high * 100:.1f}%"

        lines = [
            f"# BANG! checkpoint self-evaluation · PPO {report['iteration']}",
            "",
            f"- Games: {report['games']}",
            f"- Method: `{report['method']}`",
            f"- Law win rate: {report['lawWinRate'] * 100:.2f}%",
            f"- Outlaw win rate: {report['outlawWinRate'] * 100:.2f}%",
            f"- Mean game length: {report['meanTurns']:.2f} turns / "
            f"{report['meanSteps']:.2f} decisions",
            f"- Runtime: {format_duration(report['elapsedSeconds'])} "
            f"({report['gamesPerSecond']:.1f} games/s)",
            "",
            "## Characters",
            "",
            "| Character | Side | Games | Win rate | Approx. 95% interval | "
            "vs side | Active win | Reserve entry | Eliminated |",
            "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
        for row in sorted(
            report["characters"],
            key=lambda value: (value["side"], -value["winRate"]),
        ):
            active_rate = (
                f"{row['startingActiveWinRate'] * 100:.1f}%"
                if row["startingActiveGames"] > 0
                else "n/a"
            )
            lines.append(
                f"| {row['name']} | {row['side'].title()} | {row['games']} | "
                f"{row['winRate'] * 100:.1f}% | {interval(row)} | "
                f"{row['uplift'] * 100:+.1f} pp | "
                f"{active_rate} | "
                f"{row['reserveEntryRate'] * 100:.1f}% | "
                f"{row['eliminationRate'] * 100:.1f}% |"
            )

        eligible_pairs = [
            row for row in report["characterPairs"] if row["games"] >= 20
        ]
        lines.extend([
            "",
            "## Character-pair synergies",
            "",
            "Pairs are ranked by win-rate uplift over their side baseline. "
            "The JSON report contains every observed pair.",
            "",
            "| Side | Pair | Games | Win rate | Uplift |",
            "| --- | --- | ---: | ---: | ---: |",
        ])
        for row in sorted(
            eligible_pairs,
            key=lambda value: value["uplift"],
            reverse=True,
        )[:10]:
            lines.append(
                f"| {row['side'].title()} | "
                f"{row['firstName']} + {row['secondName']} | "
                f"{row['games']} | {row['winRate'] * 100:.1f}% | "
                f"{row['uplift'] * 100:+.1f} pp |"
            )
        lines.extend([
            "",
            "## Notes",
            "",
            "- Both sides use the same checkpoint with greedy action selection.",
            "- Character uplift is descriptive, not causal; roster composition "
            "and matchup correlations remain.",
            "- Full pair, dealt-character matchup, and starting-active matchup "
            "tables are retained in the JSON artifact.",
        ])
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("\n".join(lines) + "\n")

    @staticmethod
    def normalize(values: list[tuple[str, float]]) -> dict[str, float]:
        total = sum(max(0.0, value) for _, value in values)
        return {key: max(0.0, value) / total for key, value in values}

    @staticmethod
    def pfsp_score(win_rate: float) -> float:
        return max(0.05, 1.0 - abs(win_rate - 0.5) / 0.5)

    def active_archive(self) -> list[dict[str, Any]]:
        return [entry for entry in self.state["archive"] if entry.get("active", True)]

    def league_opponents(self, chunk: int) -> list[tuple[str, str, float, Path | None]]:
        current = require_checkpoint(Path(self.state["mainCheckpoint"]))
        current_digest = sha256(current)
        history = [entry for entry in self.active_archive() if entry["sha256"] != current_digest]
        scouting: dict[str, Any] = {}
        self.console.line(f"  scouting   {len(history)} historical opponents")
        for entry in history:
            result = self.evaluate(
                current,
                Path(entry["path"]),
                self.args.scouting_pairs,
                self.args.seed + chunk * 10_000,
            )
            scouting[entry["id"]] = result
            self.console.line(
                f"    {entry['id']:<22} "
                + benchmark_text("paired", result)
            )
        weights = self.normalize([
            (entry["id"], self.pfsp_score(scouting[entry["id"]]["candidateWinRate"]))
            for entry in history
        ]) if history else {}
        anchor = self.run_dir / "anchors" / f"chunk_{chunk:04d}" / "policy.pt"
        self.copy_checkpoint(current, anchor)
        opponents: list[tuple[str, str, float, Path | None]] = [
            ("checkpoint", "current", 0.5 + (0.4 if not history else 0.0), anchor),
            ("heuristic", "heuristic", 0.05, None),
            ("random", "random", 0.05, None),
        ]
        opponents.extend(
            ("checkpoint", entry["id"], 0.4 * weights[entry["id"]], Path(entry["path"]))
            for entry in history
        )
        self.state["lastScouting"] = scouting
        self.state["lastOpponentWeights"] = {
            identifier: probability for _, identifier, probability, _ in opponents
        }
        self.save_state()
        self.console.line(
            "  opponents  "
            + " · ".join(
                f"{identifier} {probability * 100:.1f}%"
                for _, identifier, probability, _ in sorted(
                    opponents, key=lambda row: row[2], reverse=True
                )[:5]
            )
        )
        return opponents

    def prune_archive(self) -> None:
        active = self.active_archive()
        while len(active) > self.args.max_archive:
            removable = [entry for entry in active if entry["role"] != "bootstrap"]
            if not removable:
                return
            oldest = min(removable, key=lambda entry: entry["iteration"])
            oldest["active"] = False
            active = self.active_archive()

    def run_chunk(self, chunk: int) -> None:
        opponents = self.league_opponents(chunk)
        previous = next(
            checkpoint
            for kind, identifier, _, checkpoint in opponents
            if kind == "checkpoint" and identifier == "current"
        )
        assert previous is not None
        current = require_checkpoint(Path(self.state["mainCheckpoint"]))
        target = int(self.state["iteration"]) + self.args.chunk_iterations
        self.console.heading(
            f"League chunk {chunk} · PPO {self.state['iteration'] + 1}–{target}"
        )
        self.console.line(
            "  budget     "
            + format_duration(float(self.state["trainingDeadlineEpoch"]) - time.time())
            + " training remains"
        )
        code = self.run_native(
            self.training_command(target, opponents, current),
            target,
        )
        latest = require_checkpoint(self.run_dir / "main" / "latest.pt")
        metadata = read_json(metadata_path(latest))
        iteration = int(metadata["iteration"])
        identifier = f"main_{iteration:06d}"
        record = next(
            (entry for entry in self.state["archive"] if entry["id"] == identifier),
            None,
        )
        if record is None:
            record = self.snapshot(latest, identifier, "main")
            self.state["archive"].append(record)
        candidate = Path(record["path"])
        bootstrap = Path(self.state["archive"][0]["path"])
        benchmark = self.evaluate(
            candidate,
            bootstrap,
            self.args.scouting_pairs,
            self.args.seed + 900_000,
        )
        heuristic = self.evaluate(
            candidate,
            None,
            self.args.scouting_pairs,
            self.args.seed + 910_000,
        )
        previous_result = (
            benchmark
            if sha256(previous) == sha256(bootstrap)
            else self.evaluate(
                candidate,
                previous,
                self.args.scouting_pairs,
                self.args.seed + 920_000,
            )
        )
        benchmarks = {
            "heuristic": heuristic,
            "bootstrap": benchmark,
            "previousChunk": previous_result,
        }
        for result in benchmarks.values():
            result["confidence95"] = list(wilson_interval(
                int(result["candidateWins"]),
                int(result["games"]),
            ))
        record["benchmarkWinRate"] = benchmark["candidateWinRate"]
        record["benchmarks"] = benchmarks
        self.console.line(
            "  BENCHMARK │ "
            + " · ".join([
                benchmark_text("heuristic", heuristic),
                benchmark_text("bootstrap", benchmark),
                benchmark_text("previous", previous_result),
            ])
        )
        self.console.line(
            "    seats   │ "
            f"heuristic L {heuristic['candidateLawWinRate'] * 100:.1f}% "
            f"· O {heuristic['candidateOutlawWinRate'] * 100:.1f}%"
            " · "
            f"previous L {previous_result['candidateLawWinRate'] * 100:.1f}% "
            f"· O {previous_result['candidateOutlawWinRate'] * 100:.1f}%"
        )
        if (
            self.args.self_eval_games > 0
            and chunk % self.args.self_eval_interval_chunks == 0
        ):
            self_evaluation = self.self_evaluate(
                candidate,
                self.args.self_eval_games,
                self.args.seed + 4_000_000,
                chunk,
                iteration,
            )
            record["selfEvaluation"] = self_evaluation
            self.state["lastSelfEvaluation"] = self_evaluation
        self.state.update({
            "iteration": iteration,
            "chunk": chunk,
            "mainCheckpoint": str(latest),
        })
        self.prune_archive()
        self.save_state()
        self.console.good(f"chunk committed at PPO iteration {iteration}")
        if code == 75:
            self.stop_requested = True

    def finalists(self) -> list[dict[str, Any]]:
        unique = {entry["sha256"]: entry for entry in self.state["archive"]}
        ranked = sorted(
            unique.values(),
            key=lambda entry: (
                float(entry.get("benchmarkWinRate", -1.0)),
                int(entry["iteration"]),
            ),
            reverse=True,
        )
        bootstrap = next(entry for entry in ranked if entry["role"] == "bootstrap")
        selected = [bootstrap]
        for entry in ranked:
            if len(selected) >= self.args.max_finalists:
                break
            if entry["sha256"] != bootstrap["sha256"]:
                selected.append(entry)
        return selected

    def final_tournament(self) -> None:
        self.state["phase"] = "final_evaluation"
        self.save_state()
        finalists = self.finalists()
        self.console.heading(f"Final paired-seed tournament · {len(finalists)} finalists")
        ranking: list[dict[str, Any]] = []
        for candidate in finalists:
            matchups = []
            for opponent in finalists:
                if candidate["sha256"] == opponent["sha256"]:
                    continue
                result = self.evaluate(
                    Path(candidate["path"]),
                    Path(opponent["path"]),
                    self.args.final_pairs,
                    self.args.seed + 2_000_000,
                )
                matchups.append({"opponent": opponent["id"], **result})
            field_win = (
                sum(row["candidateWinRate"] for row in matchups) / len(matchups)
                if matchups else 0.5
            )
            ranking.append({**candidate, "fieldWinRate": field_win, "matchups": matchups})
            self.console.line(f"  {candidate['id']:<22} field win {field_win * 100:6.2f}%")
        ranking.sort(key=lambda entry: (entry["fieldWinRate"], entry["iteration"]), reverse=True)
        final_dir = self.run_dir / "final"
        atomic_json(final_dir / "summary.json", {
            "schemaVersion": 1,
            "method": "paired_common_seed_round_robin_v1",
            "pairsPerMatchup": self.args.final_pairs,
            "ranking": ranking,
        })
        lines = [
            "# BANG! League PPO final tournament",
            "",
            "| Rank | Policy | PPO iteration | Field win rate |",
            "| ---: | --- | ---: | ---: |",
        ]
        for index, row in enumerate(ranking, 1):
            lines.append(
                f"| {index} | {row['id']} | {row['iteration']} | {row['fieldWinRate'] * 100:.2f}% |"
            )
        (final_dir / "summary.md").write_text("\n".join(lines) + "\n")
        winner = ranking[0]
        champion = self.run_dir / "champion" / "policy.pt"
        self.copy_checkpoint(Path(winner["path"]), champion)
        canary = subprocess.run(
            [str(self.binary), "checkpoint-canary", str(champion)],
            cwd=self.repo, text=True, capture_output=True, check=True,
        )
        inference = subprocess.run(
            [str(self.binary), "infer", str(champion), "8", str(self.args.seed + 3_000_000),
             str(self.args.torch_threads)],
            cwd=self.repo, text=True, capture_output=True, check=True,
        )
        atomic_json(champion.parent / "canary.json", json.loads(canary.stdout))
        atomic_json(champion.parent / "inference.json", json.loads(inference.stdout))
        atomic_json(champion.parent / "selection.json", winner)
        self.state.update({
            "phase": "complete",
            "championCheckpoint": str(champion),
            "champion": winner,
        })
        self.save_state()
        self.console.good(
            f"champion {winner['id']} · field win {winner['fieldWinRate'] * 100:.2f}%"
        )
        self.console.line(f"  checkpoint  {champion}")
        self.console.line(f"  report      {final_dir / 'summary.md'}")

    def run(self) -> int:
        if self.args.dry_run:
            if not self.binary.is_file():
                raise SystemExit(f"training binary is missing: {self.binary}")
            print(json.dumps({
                "ok": True,
                "runDir": str(self.run_dir),
                "settings": self.settings(),
                "evaluation": self.evaluation_settings(),
            }, indent=2))
            return 0
        self.acquire_lock()
        # A request left by the previous stop must not end this resume at once.
        self.stop_file.unlink(missing_ok=True)
        prevent_system_sleep()
        self.install_signals()
        self.initialize()
        if self.state["phase"] == "complete":
            self.console.good(f"campaign already complete: {self.state['championCheckpoint']}")
            return 0
        if self.state["phase"] == "final_evaluation":
            self.console.warn("final tournament was interrupted; restarting paired evaluation")
            self.final_tournament()
            return 0
        self.bootstrap()
        training_deadline = float(self.state["trainingDeadlineEpoch"])
        while time.time() < training_deadline and not self.stop_requested:
            if self.args.stop_after_chunks >= 0 and self.state["chunk"] >= self.args.stop_after_chunks:
                self.state["phase"] = "stopped"
                self.save_state()
                self.console.warn("validation stop reached; rerun the same command to resume")
                return 0
            next_chunk = int(self.state["chunk"]) + 1
            if self.args.max_chunks and next_chunk > self.args.max_chunks:
                break
            self.run_chunk(next_chunk)
        if self.stop_requested:
            self.state["phase"] = "stopped"
            self.save_state()
            self.console.warn("campaign stopped safely; rerun the same command to resume")
            return 0
        self.final_tournament()
        return 0


def main() -> int:
    args = parse_args()
    validate_args(args)
    # OpenBLAS otherwise starts one thread per logical core and oversubscribes
    # against Torch's own pool, which measured slower than an unoptimized BLAS.
    # Every native child process inherits this.
    os.environ.setdefault("OPENBLAS_NUM_THREADS", str(args.torch_threads))
    return Campaign(args).run()


if __name__ == "__main__":
    raise SystemExit(main())
