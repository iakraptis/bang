# Training a BANG! The Duel policy

## What is implemented

The training path is a native C++/LibTorch League PPO system for the standard
four-character duel. It is intentionally CPU-first for the current host:

- all game rules, rollouts, legal masks, and policy inference stay in C++;
- many simultaneous games share batched policy calls;
- a structured actor scores only current legal actions using semantic action
  features rather than learning 997 unrelated output logits;
- the deployable critic consumes only information available to its player;
- an auxiliary training critic may consume full environment state, but that
  privileged tensor never reaches the actor or browser policy;
- PPO uses clipped policy updates, GAE, normalized advantages, entropy
  regularization, gradient clipping, and target-KL early stopping;
- the opponent mixture combines a frontier snapshot, prioritized historical
  snapshots, a deterministic tactical heuristic, and random play;
- final evaluation uses both seats for every seed to reduce first-player and
  deal variance.

This is a strong, reliable training foundation—not a guarantee that a finite
48-hour CPU run will exceed every human player. Strength must be established by
held-out paired matches and, ultimately, human evaluation.

## Quick start

Prerequisites are CMake, a C++20 compiler, Python 3, and a CPU PyTorch install
that exposes `torch.utils.cmake_prefix_path`.

```sh
# Complete lifecycle validation; only a few seconds after the build.
./train.sh --rehearsal --run-dir runs/rehearsal

# Production campaign: 48 hours total, reserving the last hour for selection.
./train.sh
```

The wrapper always creates an optimized build and runs the test suite before
training.

### Windows

`train.ps1` is the Windows wrapper and accepts the same options. It needs the
Visual Studio 2022 C++ build tools (MSVC, plus the bundled CMake and Ninja) and
uses `.venv\Scripts\python.exe` when that virtual environment exists:

```powershell
python -m venv .venv
.venv\Scripts\python.exe -m pip install torch numpy --index-url https://download.pytorch.org/whl/cu124
.\train.ps1 --rehearsal --run-dir runs/rehearsal
```

The script loads the MSVC environment itself, so a plain PowerShell works.
Differences from the Linux path:

- Torch's DLLs (~4 GB with CUDA) are found through `PATH` rather than copied
  next to the binary. To run `build-train\bang_duel.exe` by hand, first prepend
  `.venv\Lib\site-packages\torch\lib` to `PATH`.
- `PYTHONUTF8=1` is set because native telemetry is UTF-8.
- Ctrl-C reaches the native trainer through the console. The controller also
  creates `stop.request` in the run directory, which the trainer polls after
  each committed iteration (`--stop-file`); POSIX additionally forwards SIGINT.
- `OPENBLAS_NUM_THREADS` does not apply: Windows Torch uses MKL.

### GPU learner

`--device cuda` (or `cuda:N`) runs the PPO update on the GPU. Rollouts,
opponents, and evaluations stay on the CPU: the learner is a GPU twin of the
CPU rollout network, copied back after every update, and checkpoints are always
written from the CPU copy, so they load anywhere. A run can resume on a
different device; the flag is deliberately not part of the pinned settings.

Measured on an RTX 4060 Ti with 256 games, four epochs, and eight minibatches:

| Model | CPU learn | CUDA learn |
|---|---:|---:|
| `v2` | ~1.1 s | ~0.65 s |
| `v3` | ~5.3 s | ~0.8 s |

The gain grows with model size; `v2` is small enough that transfer overhead
eats much of it. GPU reductions are not bit-identical to CPU ones, so a CUDA
run reproduces a CPU run only approximately.

To customize the production budget:

```sh
./train.sh \
  --run-dir runs/experiment_01 \
  --model v3 \
  --hours 72 \
  --final-reserve-hours 2 \
  --batch-games 256 \
  --chunk-iterations 10 \
  --self-eval-games 2048 \
  --self-eval-interval-chunks 5 \
  --torch-threads 6
```

Use a new run directory when changing optimization settings. A resume rejects
settings that differ from the original manifest because silently changing an
experiment midway makes checkpoints and comparisons difficult to interpret.

## Model size

`--model` selects the policy architecture, and the choice is recorded in the
checkpoint metadata so every consumer rebuilds the right module:

| Model | Deployable parameters | Measured iteration |
|---|---:|---:|
| `v2` (default) | 222,722 | ~3.6 s |
| `v3` | 2,340,770 | ~8.9 s |

`v3` is roughly ten times larger; see `docs/large_model_design.md` for the
layer table, the reasoning behind where the capacity was placed, and the
measurements. A run directory is pinned to its model: resuming with a different
`--model` is rejected, as is loading a checkpoint into a mismatched module.

**BLAS matters more than either.** This host now uses OpenBLAS rather than
Debian's reference BLAS, which alone made an iteration 13x faster.
`OPENBLAS_NUM_THREADS` must stay pinned — `train.sh` exports `6`. Leaving it
unset lets OpenBLAS oversubscribe all twelve logical cores against Torch's own
thread pool, which measured *slower* than the unoptimized reference BLAS.

## Safe stop and resume

Press `Ctrl-C` once. The controller forwards the signal to the native trainer,
which completes the current update and writes:

1. policy weights and strict ABI metadata;
2. optimizer state;
3. a commit marker written only after the bundle is complete;
4. campaign state through an atomic replace.

The controller then marks the campaign stopped. Resume with the exact original
command:

```sh
./train.sh
```

Only one process may own a run directory. The campaign deadline is retained
across restarts, so downtime does not silently extend a time-boxed experiment.
A machine-level failure during a checkpoint write is detected by the missing or
mismatched commit marker rather than loading mixed policy and optimizer state.

## Live telemetry

A production line has this shape:

```text
PPO     7 │ games  256 │ steps   56656 │ learner 51.2% │ form-7 53.0% │ seats L 49.6% · O 52.8% │ ent 1.284 │ KL 0.0041 │ clip 3.2% │ 10309 env/s · 3380 train/s │ 5.5s roll + 29.0s learn │ ckpt 1m 42s · training 46h 12m left
  STRENGTH │ chunk vs current 52.1% (896g) · history 56.4% (614g) · heuristic 64.1% (90g) · random 91.2% (90g)
  STYLE    │ attack 42.3% of chances · defend 71.8% · swap 12.4% · rear target 18.2% · 2.71 cards/turn · top Colt,Duel,Beer
  HEALTH   │ norm-ent 61.0% · confidence 68.0% · critic EV 0.420 · grad 0.380 · KL 0.004/0.015 · clip 3.2%
```

- **learner**: win rate of the seat controlled by the updated policy;
- **form**: rolling learner win rate over up to ten updates, followed by its
  change from the preceding window when one is available;
- **seats**: learner win rate split by Law and Outlaw, with a warning when a
  sufficiently sampled update differs by more than 20 percentage points;
- **ent**: policy entropy over legal actions;
- **KL**: approximate policy divergence; `*` means target-KL stopped epochs;
- **clip**: fraction of samples outside PPO's unclipped ratio range;
- **env/s**: native environment transitions per rollout second;
- **train/s**: learner decisions processed per optimizer second, including
  epochs;
- **roll / learn**: where iteration wall time is spent.

Every fifth update (plus the first and last update of a native training
process), the extra lines summarize:

- **STRENGTH**: chunk-to-date performance split by frozen-current, historical,
  heuristic, and random opponents;
- **STYLE**: choices divided by opportunities, rather than misleading raw
  action counts, plus targeting, cards per learner turn, and most-used cards;
- **HEALTH**: entropy normalized by the number of legal actions, mean top-action
  probability, critic explained variance, pre-clipping gradient norm, and PPO
  update pressure.

At every chunk boundary, `BENCHMARK` evaluates the committed snapshot on fixed
paired seeds against the heuristic, bootstrap policy, and previous chunk. The
console includes approximate 95% Wilson intervals and Law/Outlaw splits. These
fixed benchmarks are better evidence of progress than the rollout win rate,
whose opponent mixture changes over time.

Native telemetry is explicitly flushed after each line, so it remains live
when the trainer is connected to the Python controller through a pipe. To keep
rollout overhead bounded, opportunity and policy-confidence statistics use a
deterministic one-in-four sample of learner choices (normally thousands of
choices per update); game, win, card-use, targeting, and turn counts remain
complete.

## Checkpoint self-evaluation

Every five league chunks by default, the controller runs 2,048 deterministic
games with the latest immutable checkpoint controlling both sides. Using one
policy on both sides isolates deal, side, roster, and matchup effects from
opponent-policy differences. Disable it with `--self-eval-games 0`, or adjust
the game count and cadence with `--self-eval-games` and
`--self-eval-interval-chunks`.

The console reports overall side balance, throughput, game length, and the
strongest/weakest dealt character on each side. Detailed JSON and Markdown
reports include:

- dealt-character win rate, approximate 95% interval, and uplift over its
  side's baseline;
- starting-active win rate;
- reserve entry and elimination rates;
- same-side character-pair synergy;
- every Law-character versus Outlaw-character dealt matchup;
- starting-active matchup results;
- mean, median, and 90th-percentile game length;
- remaining life and characters at the terminal state.

The same seed schedule is reused for every checkpoint report, making changes
between checkpoints less noisy. Results are descriptive rather than causal:
characters are dealt in correlated four-character rosters, so individual and
pair uplifts can still reflect the rest of the roster.

Run the evaluator manually with:

```sh
./build-train/bang_duel self-evaluate \
  runs/bang_league_ppo/main/latest.pt 2048 4470001 6
```

On the current host, a measured 512-game self-evaluation completed in 22.9
seconds while production training was also running (22.4 games/s). At that
observed rate, 2,048 games take about 92 seconds and 4,096 games about three
minutes. Runtime varies with CPU contention and average game length.

`progress.log` mirrors readable campaign output. `native.log` is the full
append-only subprocess log. `main/metrics.jsonl` includes losses, cumulative
steps and decisions, truncations, rolling form, seat and opponent splits,
opportunity-adjusted style, confidence, critic quality, gradient norm,
end-to-end throughput, and timing for plotting or monitoring without scraping
console text. Chunk benchmark results and their confidence intervals are also
stored on each archive entry in `state.json`.

On the Ryzen 5 3600 host used to build this path, the production default
(256 games, four epochs, eight minibatches, six Torch threads) completed the
first heuristic-opponent update in about 35 seconds, used about 460 MiB peak
RSS, and collected roughly 10,000 environment transitions/s. League iterations
vary with game length and checkpoint-opponent inference.

## League and policy selection

The campaign starts from random weights against 70% heuristic and 30% random
opponents. Each later chunk freezes the current policy and samples:

| Opponent | Mass |
|---|---:|
| Frozen current policy | 50% |
| PFSP historical policies | 40% |
| Tactical heuristic | 5% |
| Random legal policy | 5% |

Before each chunk, small paired evaluations estimate which historical policies
are most informative. Prioritized fictitious self-play gives most mass to
opponents near the current policy's strength while retaining a nonzero floor.
The archive is bounded to avoid unbounded memory and evaluation cost.

The final tournament evaluates up to eight diverse snapshots in a paired-seed
round robin. Every seed is played once from each seat. The highest mean field
win rate becomes the champion; iteration is only a deterministic tie-breaker.

## Artifacts

The default `runs/bang_league_ppo/` contains:

```text
manifest.json                 immutable experiment definition
state.json                    resumable campaign state
progress.log                  readable progress
native.log                    complete trainer output
main/latest.pt                resumable frontier policy
main/latest.pt.metadata.json  strict model/rules/observation ABI
main/latest.pt.optimizer.pt   Adam state
main/latest.pt.commit.json    completed-bundle marker
main/metrics.jsonl            per-update telemetry
archive/*/policy.pt           immutable league snapshots
anchors/*/policy.pt           frozen chunk opponents
evaluations/latest.{json,md}  latest character and matchup self-evaluation
evaluations/iteration_*.{json,md}
final/summary.{json,md}       paired tournament evidence
champion/policy.pt            selected deployable policy
champion/{canary,inference}.json
champion/selection.json
```

Checkpoints fail to load when the ruleset, model, observation layout, semantic
action layout, or tensor sizes do not match the current binary.

## Evaluate and play

Evaluate a candidate against the heuristic:

```sh
./build-train/bang_duel evaluate \
  runs/bang_league_ppo/champion/policy.pt - 512 900001 6
```

Compare two checkpoints with 512 common-seed pairs:

```sh
./build-train/bang_duel evaluate CANDIDATE.pt OPPONENT.pt 512 900001 6
```

An opponent ending in `.json` is an exported browser policy, played greedily.
This makes the committed `policy/static-policy.json` a fixed reference that
needs no training run on the machine:

```sh
./build-train/bang_duel evaluate CANDIDATE.pt policy/static-policy.json 1024 900001 6
```

Run a checkpoint load/tensor canary:

```sh
./build-train/bang_duel checkpoint-canary \
  runs/bang_league_ppo/champion/policy.pt
```

Play against the selected policy:

```sh
./build-train/bang_duel web \
  --run-dir runs/bang_league_ppo \
  --require-trained
```

Browser play is stochastic by default. Add `--greedy` for deterministic
maximum-logit choices. The server reports whether it actually loaded a trained
policy; `--require-trained` prevents an accidental heuristic fallback.

## Magnetic Mirror Descent

`--mmd-alpha A` adds `A · KL(policy ‖ magnet)` to the PPO loss, summed over
each decision's legal actions. PPO's clipping and target-KL stop already supply
MMD's other half, staying near the previous policy. The magnet starts as
uniform play over legal actions; with `--magnet-interval N > 0` it moves to a
frozen copy of the learner every `N` updates. A uniform magnet alone is an
entropy bonus of strength `A`; the moving magnet is what lets the policy
approach an equilibrium rather than a fixed amount of randomness.

```powershell
.\train.ps1 --run-dir runs/mmd_a01 --device cuda --mmd-alpha 0.1 --magnet-interval 50
```

- `--mmd-alpha 0` (the default) is bit-identical to plain PPO.
- The magnet is part of the training bundle (`latest.pt.magnet.pt`, with its
  iteration in the commit marker), so chunk restarts and resumes continue
  exactly where an uninterrupted run would be.
- Both settings are pinned in the manifest when MMD is on; switching MMD on or
  off mid-campaign is rejected.
- The HEALTH line and `metrics.jsonl` report the KL to the magnet and the
  iteration it was taken at.

The league opponent mix is unchanged; MMD only changes the learner's objective.

## Current limitations and next experiments

- The actor is feed-forward. The observation preserves known public card
  identities, and a privacy-tested history utility exists, but the production
  model does not yet learn recurrent belief state from the whole action trace.
- Rewards are terminal win/loss only. This avoids hand-authored reward shaping
  changing the objective, but early learning can be slow.
- PPO and league hyperparameters are conservative CPU defaults, not a completed
  hyperparameter search.
- Statistical promotion gates and human match tracking should be added before
  claiming superhuman strength.

Run separate directories for ablations such as larger batches, entropy
schedules, recurrent policies, or alternative PFSP functions, then compare
champions with the same paired seeds.
