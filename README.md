# BANG! The Duel simulation engine

This repository is building a deterministic, high-throughput implementation of
the base **BANG! The Duel** rules for AI self-play and a browser game.

The repository includes the audited native engine, a CPU-optimized League PPO
training stack, paired-seed evaluation, and an engine-backed browser table that
can load the selected trained policy.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run deterministic native simulations:

```sh
./build/bang_duel simulate 1000 42 4
```

## Train an agent

The training build uses LibTorch. On this machine, the wrapper discovers the
installed CPU PyTorch package, builds the optimized binary, runs both native and
end-to-end training tests, and then starts a resumable 48-hour campaign:

```sh
./train.sh
```

First run the short complete rehearsal if you want to validate the entire
bootstrap → league → tournament → champion lifecycle:

```sh
./train.sh --rehearsal --run-dir runs/rehearsal
```

Progress is printed as compact PPO telemetry and mirrored to
`runs/bang_league_ppo/progress.log`; complete native output is retained in
`native.log`, and machine-readable metrics are appended to
`main/metrics.jsonl`. Live summaries cover rolling and per-seat strength,
opportunity-adjusted play style, policy/critic health, throughput, and
checkpoint ETA. Each league chunk also runs paired fixed benchmarks against
the heuristic, bootstrap policy, and previous chunk. Every five chunks, the
latest checkpoint also plays 2,048 greedy games against itself to report
character win rates, pair synergies, cross-side matchups, active-character
effects, reserve entry, elimination, and game length. `Ctrl-C` finishes the
active PPO update, commits a checkpoint, and exits. Run the same command again
to resume with the optimizer, league, time deadline, and evaluation state
intact.

The campaign trains from random initialization against heuristic and random
anchors, then uses frozen-current and prioritized historical opponents. It
selects `champion/policy.pt` with a paired common-seed round robin. See
[the training guide](docs/training.md) for architecture, controls, artifacts,
evaluation methodology, and limitations.

## Play in the browser

Start the local server with the frontier heuristic:

```sh
./build/bang_duel web 8765 127.0.0.1
```

Or play a completed training campaign and fail closed if its checkpoint is
missing or incompatible:

```sh
./build-train/bang_duel web \
  --run-dir runs/bang_league_ppo \
  --require-trained
```

Then open [http://127.0.0.1:8765](http://127.0.0.1:8765). The server owns every
game session and automatically advances the non-human side with either the
validated trained checkpoint or the legal-action heuristic. Choose either
side, a 2–12 character roster, and an optional deterministic seed from the
**New duel** dialog. Training and checkpoint inference use the standard
four-character duel. Every visible card and in-play character includes concise
engine-owned rules text, including Equipment and legal-action previews.

The browser receives a player-scoped JSON view: opponent hands and reserves
stay masked, simultaneous setup choices remain hidden until both sides commit,
and every displayed action ID comes directly from `LegalActions()`. The
frontend contains no parallel rules implementation.

### Build a completely static webapp

The game engine can run in the browser as WebAssembly, so the production
webapp needs only a static file host and makes no API requests:

```sh
tools/build_static_web.sh web/dist-static
python3 -m http.server 8765 --directory web/dist-static
```

Open [http://127.0.0.1:8765](http://127.0.0.1:8765). The generated directory
contains the HTML, JavaScript, CSS, card images, and WASM engine and can be
uploaded as-is to any static host. Emscripten's `em++` compiler is required to
build it. Assets use relative URLs, so the directory can also be hosted below a
path prefix. The build also includes `about.html`, a scroll-driven account of
the game, League PPO system, network architecture, training run, and learned
meta. Its figures are generated from the repository's real training metrics
and checkpoint evaluations.

Like the native server's fallback mode, the static build uses the frontier
heuristic when no policy is supplied. To embed a trained checkpoint, export it
to the small browser-native inference format and pass it to the builder:

```sh
python3 tools/export_static_policy.py \
  runs/bang_league_ppo/main/latest.pt \
  /tmp/bang-policy.json
tools/build_static_web.sh web/dist-static /tmp/bang-policy.json
```

The exported actor runs directly in C++/WASM without LibTorch. Sessions live
in WASM memory and therefore start fresh after a page reload; use the native
server when durable sessions are required. Pass the training run as the
optional third builder argument to refresh the about-page figures:

```sh
tools/build_static_web.sh \
  web/dist-static \
  /tmp/bang-policy.json \
  runs/bang_league_ppo
```

### Continuous deployment

Every successful push to `main` deploys the trained static webapp to
Cloudflare Pages through `.github/workflows/ci.yml`. Configure these GitHub
repository settings before the first deployment:

- Secrets: `CLOUDFLARE_API_TOKEN`, `CLOUDFLARE_ACCOUNT_ID`
- Variables: `CLOUDFLARE_PAGES_PROJECT`, `CLOUDFLARE_PAGES_DOMAIN`

The Cloudflare token needs permission to deploy the configured Pages project.
The browser policy committed at `policy/static-policy.json` makes the build
self-contained; update it from a complete checkpoint when promoting a newer
agent:

```sh
python3 tools/export_static_policy.py \
  runs/bang_league_ppo/main/latest.pt \
  policy/static-policy.json
```

Cloudflare Pages serves the generated files as static assets without invoking
Pages Functions.

The simulator currently resolves the core hit/avoid flow and every base card,
including ordered start-of-turn hazards, multi-target effects, public drafts,
private draw choices, and random draws that preserve visible card backs.
The first character layer covers passive draw/BANG limits, Annie and Toco card
transformations, Jack Ransome target overrides, and the Babe Leroy, Cattle
Katie, and Soundance Kid once-per-turn abilities. Tex Killer's two-card BANG!
defense and Tom Thorn's swap-triggered hits preserve explicit continuations.
Alan Pinkertoon's non-last-life counter-hits also work across ordinary hits,
multi-target cards, and start-of-turn damage. Dalon Ranger and Slim Poet expose
optional per-life reactions, while Jango participates in ordered movement
triggers. Pat Garret exposes his second draw and may draw from the opposing
deck on a Horseshoe; publicly known hand cards remain visible until played or
discarded. Sid Curry's reveal-based avoid and delayed counter integrates with
Bart, Barrel, Tex Killer, and multi-target continuations. Wyatt Ear adds
separate optional draws after missed BANGs, including Remington and deferred
Fanning reactions. Pearl Hat captures a discard-top Action, pays a selected
hand-card cost, and resolves the copied effect through the normal card engine.
Bull Anderson now chooses his cards before elimination cleanup and the normal
reward; Bill Tightman receives his separate optional draw afterward.
Buffalo Bell can distribute multi-point damage one point at a time from either
position without bypassing the original hit response. Wild Bill's automatic
hits cover every opponent-driven hand and Equipment removal path, including
nested Bull Anderson eliminations. A rulebook audit additionally covers
Annie/Toco Duel and Indians! payments, Scope with non-BANG hits, visible card
backs for every random selection, and the ordering of deferred Wyatt/Sid
consequences. The detailed evidence is tracked in
[the rulebook coverage document](docs/rulebook_coverage.md).

## Engine contract

`bang::GameState` follows the same useful interface shape as the reference
Dune project:

- `LegalActions()` returns the complete legal decision set;
- `ApplyAction()` rejects anything outside that set;
- `ObservationTensor(observer)` exposes public state plus that observer's
  private hand, without exposing the opponent's private cards;
- `TrainingObservationTensor(observer)` adds exact observer-owned and
  legitimately known card identities without leaking hidden state;
- `LegalActionFeatures()` maps stable legal IDs to semantic features so
  equivalent physical card copies share statistical strength;
- `Returns()` provides terminal zero-sum results.

All randomness is owned by the state and seeded through `GameConfig`, making a
game reproducible from its seed and action sequence.
