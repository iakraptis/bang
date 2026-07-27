# BANG! The Duel — a small PPO project

**[▶ Play against the trained agent at bang.matehegedus.com](https://bang.matehegedus.com)**
— no install, no account, runs entirely in your browser.

This is a deliberately small project that explores one question:

> How far can plain PPO self-play get on an **imperfect-information board game**,
> on a single CPU, in a couple of days?

Not a research framework. Not a general game platform. One game, one engine, one
training stack, one deployable agent — small enough to read end to end in an
afternoon.

**BANG! The Duel** is a good subject: two players, hidden hands, hidden setup
choices, 31 card types, 24 asymmetric characters (12 per side), and a branching
reaction system where a single BANG! can cascade through several optional
responses. Half the board state is secret, so two different hidden hands can
produce the exact same observation. The agent has to play the average.

---

## What's here

| Piece | Where | Size |
|---|---|---|
| Deterministic C++20 rules engine | `src/game.cpp`, `include/bang/game.hpp` | ~1.7M decisions/sec, single core |
| League PPO trainer (C++ / LibTorch) | `src/training.cpp`, `tools/train.py` | 222k-param default actor |
| Browser-native inference (no LibTorch) | `src/static_policy.cpp` | plays from a 1-file JSON policy |
| Web table (native server + WASM) | `web/`, `src/web.cpp` | zero rules logic in JavaScript |
| Trained champion policy | `policy/static-policy.json` | what the live site plays |

Everything shares one source of rules truth. The frontend calls the engine; it
never reimplements a rule.

---

## Getting started

### 1. Build and test (about a minute)

Needs CMake ≥ 3.24 and a C++20 compiler — nothing else. Node and Python 3 are
optional; their test suites are skipped if you don't have them.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### 2. See the engine run

```sh
./build/bang_duel simulate 10000 42 4
# games=10000 law_wins=5414 outlaw_wins=4586 decisions_per_second=1740996
```

Every game is reproducible from its seed and action sequence — all randomness is
owned by the state and seeded through `GameConfig`.

### 3. Play locally

```sh
./build/bang_duel web 8765 127.0.0.1
```

Open <http://127.0.0.1:8765>. The server owns the session and plays the other
side with the built-in heuristic. Pick your side, a 2–12 character roster, and
an optional seed from the **New duel** dialog.

To play the trained agent locally instead of the heuristic, build the static
webapp (below) or point the server at a training run with `--require-trained`.

### 4. (Optional) Build the static webapp

The whole engine compiles to WebAssembly, so the production site is just files
on a static host — no backend, no API calls. Requires Emscripten (`em++`).

```sh
tools/build_static_web.sh web/dist-static policy/static-policy.json
python3 -m http.server 8765 --directory web/dist-static
```

That is exactly what CI deploys to bang.matehegedus.com on every push to `main`.
The bundle also contains `about.html`, a scroll-driven explainer of the game, the
training system, and the learned meta, with figures generated from the real
training metrics.

---

## Train your own agent

Needs Python 3 and a CPU PyTorch install. `train.sh` configures an optimized
build, runs the full test suite, then starts a resumable campaign.

```sh
# seconds-long rehearsal of the whole bootstrap → league → tournament lifecycle
./train.sh --rehearsal --run-dir runs/rehearsal

# the real thing: 48 hours, last hour reserved for champion selection
./train.sh
```

`Ctrl-C` finishes the current PPO update, commits a checkpoint, and exits. Run
the same command again to resume with optimizer, league, deadline, and
evaluation state intact.

**Watch out for BLAS.** Pin `OPENBLAS_NUM_THREADS` (train.sh exports `6`).
Leaving it unset lets OpenBLAS oversubscribe against Torch's own thread pool and
measured *slower* than unoptimized reference BLAS — a 13x difference.

Full details, telemetry reference, and artifact layout:
**[docs/training.md](docs/training.md)**.

---

## How it works

**Engine contract** — `bang::GameState` exposes the interface an RL stack needs:

- `LegalActions()` returns the complete legal decision set;
- `ApplyAction()` rejects anything outside it;
- `ObservationTensor(observer)` gives public state plus that observer's private
  hand, and nothing of the opponent's;
- `TrainingObservationTensor(observer)` adds legitimately-known card identities
  without leaking hidden state;
- `LegalActionFeatures()` maps stable action IDs to semantic features, so
  equivalent copies of a card share statistical strength;
- `Returns()` gives terminal zero-sum results.

**The actor scores legal actions, not slots.** Instead of learning 997 unrelated
output logits, it embeds each currently-legal action and scores it against the
state — so the action space can be large and sparse without the network having to
memorize its indexing.

**Information boundaries are tested, not assumed.** An auxiliary training critic
may see full environment state, but that privileged tensor never reaches the
actor or the browser policy, and tests enforce it.

**League PPO.** Clipped updates, GAE, normalized advantages, entropy bonus,
gradient clipping, target-KL early stopping. Opponents are a mixture of a frozen
frontier snapshot, prioritized historical snapshots, a tactical heuristic, and
random play. Champion selection is a paired common-seed round robin — both seats
for every seed, to cancel first-player and deal variance.

More: **[docs/architecture.md](docs/architecture.md)** ·
**[docs/large_model_design.md](docs/large_model_design.md)**

---

## What the agent learned

From 5 successive checkpoints × 2048 greedy self-play games
(**[docs/meta_analysis.md](docs/meta_analysis.md)**):

- **The game is outlaw-favoured by ~11 points** — 55.5% / 44.5%, stable across
  every checkpoint measured.
- **Character power spread is wider than the side imbalance** — 38.5% to 64.8%
  win rate depending on who you draw.
- **It plays control, not aggression.** Defends when able 88.4% of the time,
  attacks only 45.1%. The most-picked card is `stealing` (61.6%); `dynamite` is
  dead last (14.3%) despite being offered 33,000 times.
- **Pairs are near-additive** — this is a power meta, not a combo meta.

Read the caveats section before quoting any of that. These are greedy
evaluations of one converged policy that had been plateaued for ~400 iterations;
they describe *this equilibrium*, not proven facts about the game.

### Known limitations

- The actor is feed-forward — no recurrent belief state over the action trace.
- Rewards are terminal win/loss only, so early learning is slow.
- Hyperparameters are conservative CPU defaults, not a completed search.
- No statistical promotion gates or human-match tracking, so **no superhuman
  claims** are being made here.

Each of these is a good first contribution.

---

## Contributing

Contributions are very welcome — this project is small on purpose, which makes
it an easy one to actually land a change in.

Two paths, both documented in **[CONTRIBUTING.md](CONTRIBUTING.md)**:

1. **Code contributions.** Run `scripts/check.sh` before you open a PR. It is
   the same gate CI runs — build, all tests, deterministic simulation, and the
   browser-policy smoke test.
2. **Trained checkpoints.** Trained a stronger agent? There is a documented
   submission workflow: benchmark it against the current champion with paired
   seeds, export it to the browser format, and open a PR with the numbers. A
   winning policy replaces `policy/static-policy.json` and goes live.

Good first issues: recurrent policy, reward shaping ablations, hyperparameter
sweeps, rules coverage (see
[docs/rulebook_coverage.md](docs/rulebook_coverage.md)), or web table UX
(see [docs/web_ux_tasks.md](docs/web_ux_tasks.md)).

---

## Documentation map

| Document | What it covers |
|---|---|
| [docs/architecture.md](docs/architecture.md) | Engine design, state machine, hidden-information rules |
| [docs/training.md](docs/training.md) | League PPO stack, telemetry, artifacts, evaluation |
| [docs/large_model_design.md](docs/large_model_design.md) | The 10x network (`v3`) and where the capacity went |
| [docs/meta_analysis.md](docs/meta_analysis.md) | What the converged policy actually plays |
| [docs/rulebook_coverage.md](docs/rulebook_coverage.md) | Rules implemented vs the base rulebook |
| [docs/web_ux_tasks.md](docs/web_ux_tasks.md) | Open web table improvements |

---

## License and attribution

Code, docs, and build scripts: **[MIT](LICENSE)**.

This is an **unofficial, non-commercial fan project**. BANG! and BANG! The Duel
are trademarks of **dV Giochi**, with no affiliation to or endorsement by them.
The card and character artwork in `web/cards/` and the rulebook PDF are the
property of their copyright holders and are **not** covered by the MIT license —
see [LICENSE](LICENSE) and [web/cards/README.md](web/cards/README.md). Rights
holders can open an issue for removal.

If you enjoy the game, [buy a copy](https://www.dvgiochi.com/).
