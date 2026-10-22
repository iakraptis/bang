# Contributing

Contributions are welcome. This project is small on purpose — one game, one
engine, one training stack — which makes it a realistic repo to land a change in
without a week of onboarding.

There are two contribution paths. Pick the one that matches what you have:

- **[Code](#code-contributions)** — engine, trainer, web table, docs.
- **[Trained checkpoints](#checkpoint-contributions)** — you trained a stronger
  agent and want it to be the one people play at bang.matehegedus.com.

---

## Code contributions

### The contribution checkpoint

One command, run before you open a PR. It is the project's full gate:

```sh
scripts/check.sh
```

That covers the engine build, all native/web/data tests, a deterministic
simulation, and the browser-policy smoke test — about a minute on a normal
laptop. There is no CI, so this run is the gate.

Touched something bigger? Add the matching flag:

| You changed | Run |
|---|---|
| Engine, web server, tests, docs | `scripts/check.sh` |
| Anything under `web/`, or the WASM build | `scripts/check.sh --with-web` (needs `em++`) |
| `src/training.cpp`, `src/policy_network.cpp`, `tools/train.py` | `scripts/check.sh --with-training` (needs CPU PyTorch) |
| Not sure | `scripts/check.sh --all` |

### Workflow

1. Fork and branch from `main`.
2. Make the change. Add or extend a test for it — see below for where tests live.
3. Run `scripts/check.sh` (plus flags).
4. Open a PR and fill in the template. Paste the last lines of the check output.

There is no CI: the check output in the PR is the evidence the change passes.
Deployment to production is manual (see the README's static web build).

### Where tests live

| Area | File |
|---|---|
| Rules, action legality, information boundaries | `tests/native_tests.cpp` |
| Browser targeting, card FX, assets, WASM engine bridge | `tests/web_*.js` |
| About-page data generation | `tests/about_data_tests.py` |
| Training CLI and campaign lifecycle | `tests/train_cli_tests.py` |

### Ground rules for the engine

These are the invariants that keep the whole thing honest. A PR that breaks one
will not be merged, even if tests pass:

1. **The engine is the only source of rules truth.** No rules logic in
   JavaScript. The frontend renders whatever `LegalActions()` returns.
2. **No hidden-information leaks.** `ObservationTensor(observer)` and the
   player-scoped JSON view must never expose the opponent's hand, reserve, or
   uncommitted setup choices. The privileged training critic tensor must never
   reach the actor or the browser policy. Add a test when you touch this.
3. **Determinism holds.** A game must be reproducible from its seed and action
   sequence. All randomness goes through state-owned RNG seeded by `GameConfig`.
4. **Checkpoint ABI is strict.** If you change the observation layout, action
   features, or model architecture, existing checkpoints must fail to load
   loudly — not silently misbehave.

### Style

Match the surrounding code. C++20, `-Wall -Wextra -Wpedantic` clean, two-space
indent. No new third-party dependencies without discussing it in an issue first
— the "small project" property is the point.

### Good first contributions

The [known limitations](README.md#known-limitations) each make a solid PR:

- a recurrent actor that learns belief state from the action trace;
- reward shaping ablations against the terminal-only baseline;
- statistical promotion gates for champion selection;
- rules coverage gaps in [docs/rulebook_coverage.md](docs/rulebook_coverage.md);
- web table UX from [docs/web_ux_tasks.md](docs/web_ux_tasks.md).

---

## Checkpoint contributions

If you trained an agent that beats the current champion, you can make it the one
the live site plays. `policy/static-policy.json` is the deployed policy: the static web build
(`tools/build_static_web.sh`) bakes it into the WASM bundle.

Checkpoints (`.pt` files) are **not** committed — only the exported browser
policy, which is a single JSON file.

### Step 1 — train

```sh
./train.sh --run-dir runs/my_experiment
```

Use a fresh run directory per experiment. Resuming with different settings is
rejected on purpose: silently changing an experiment midway makes the
checkpoints impossible to compare. See [docs/training.md](docs/training.md).

### Step 2 — benchmark against the current champion, with paired seeds

This is the number that decides the PR. Both seats are played for every seed, so
first-player and deal variance cancel out.

```sh
# your candidate vs the current champion, 512 common-seed pairs
./build-train/bang_duel evaluate \
  runs/my_experiment/champion/policy.pt \
  CURRENT_CHAMPION.pt \
  512 900001 6

# and vs the heuristic baseline, as a sanity check
./build-train/bang_duel evaluate \
  runs/my_experiment/champion/policy.pt - 512 900001 6
```

Run at least **512 pairs**. Use a seed you did not train or tune against.

The bar: a candidate should win **> 52%** of paired games against the current
champion, and not regress against the heuristic. That threshold is deliberately
modest — this is a hobby project, not a leaderboard — but report the raw counts
so the noise is visible, not just the percentage.

If your candidate uses a different architecture (`--model v3`) or changes the
observation layout, say so explicitly. It still has to export and run in the
browser.

### Step 3 — export to the browser format

The exported actor runs in C++/WASM with no LibTorch, so it must be exportable:

```sh
python3 tools/export_static_policy.py \
  runs/my_experiment/champion/policy.pt \
  policy/static-policy.json
```

### Step 4 — verify it actually plays

```sh
scripts/check.sh                 # includes the browser-policy smoke test
scripts/check.sh --with-web      # build the WASM bundle with your policy
python3 -m http.server 8765 --directory web/dist-static
```

Open <http://127.0.0.1:8765> and play a few games against it. A policy that
benchmarks well but plays visibly badly is a bug report, not a promotion.

### Step 5 — open the PR

Include:

- [ ] the updated `policy/static-policy.json`;
- [ ] paired-benchmark results vs the current champion — raw win/loss counts,
      pair count, seed, and the exact command you ran;
- [ ] benchmark vs the heuristic baseline;
- [ ] training config: model, hours, batch games, host, any changed
      hyperparameters;
- [ ] a sentence on what you changed and what you think caused the improvement;
- [ ] confirmation that you played it in the browser.

Also update the iteration and source checkpoint noted in
[policy/README.md](policy/README.md).

An independent re-run of your benchmark on a different seed is the usual review
step, so keep the run directory around until the PR is resolved.

### If it loses

Open a PR anyway, or an issue, with the numbers. A well-documented negative
result — "larger network, 48 hours, no improvement" — is genuinely useful here
and there is no separate place to publish it.

---

## Reporting bugs

Rules bugs are the most valuable kind. Include:

- the **seed** and the roster, or the session/game state if you have it;
- what the engine did and what the rulebook says should happen;
- ideally a failing case added to `tests/native_tests.cpp`.

Because games are seed-reproducible, a seed plus an action sequence is usually a
complete reproduction.

## Licensing of contributions

By contributing you agree your contribution is licensed under the
[MIT License](LICENSE), same as the rest of the code.

Do not add third-party assets (card art, rulebook text, images) in a PR. The
existing third-party material is documented in [LICENSE](LICENSE) and
[web/cards/README.md](web/cards/README.md); new material creates licensing
problems this project cannot absorb.
