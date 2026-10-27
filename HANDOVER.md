# Handover: the Renegades expansion

This fork adds *BANG! The Duel – Renegades* to the engine and the local web
game. This note is for whoever continues the work.

The repository is a personal fork of
[hmate9/bang](https://github.com/hmate9/bang). It is not affiliated with the
original author, and nothing here is deployed: pushing to this fork publishes
no website.

## Where things stand

All work is on the **`expansion`** branch, pushed to `origin/expansion`. It
has **not** been merged into `main`.

| Phase | What | State |
|---|---|---|
| 1 | Transcription of all 52 Renegades cards and official card art | Done |
| 2 | Engine groundwork: expansion data, versioned policy input layouts | Done |
| 3 | All 23 new cards and all 12 characters, with tests | Done |
| 4 | The Third Group (Renegades as their own side) and a format picker | Done |

You can play four formats from the **Custom duel** dialog:

- **Classic:** the base game, still the default.
- **Reinforcements:** Renegade cards and characters join the Law and Outlaw decks.
- **Renegades vs Law** and **Renegades vs Outlaws** (The Third Group).

Mexican Standoff (3 players) is out of scope by decision: the engine assumes
two players everywhere.

## Build, test and run (Windows)

The project builds with MSVC 2022 and LibTorch, which comes from the
Python `.venv`.

- **First build:** `train.ps1` loads MSVC, configures `build-train/` with
  CMake/Ninja against the venv's Torch, builds it, runs the tests, then starts
  training. Run its CMake steps by hand, or stop the script once the tests
  pass.
- **Rebuild:** from a shell that has run `vcvars64.bat`, run
  `cmake --build build-train`.
- **Tests:** `ctest --test-dir build-train` (11 suites; it needs the same
  vcvars shell).
- **Play:** in PowerShell, put the Torch DLLs on PATH first. Otherwise the exe
  exits silently with `0xC0000135`.

  ```powershell
  $env:PATH = "$PWD\.venv\Lib\site-packages\torch\lib;$env:PATH"
  .\build-train\bang_duel.exe web --static-policy policy/static-policy.json
  ```

  Open <http://127.0.0.1:8765>. `--mode` sets the default format for duels
  started without one. Its values are `base`, `reinforcements`,
  `renegades_vs_law` and `renegades_vs_outlaw`.

## Where to read first

- **[docs/renegades_cards.md](docs/renegades_cards.md)** is the source of truth
  for card data. It covers:
  - the transcription;
  - the rulebook FAQ;
  - the discrepancies, where the printed card beats the rulebook;
  - the implementation status table;
  - every rules ruling the project owner made. The rulings fill gaps the card
    text leaves open, such as King Fishers, the Resources, Switch, Jim Longhair
    vs Switch, El Chindi's immunity and Kitty Leroy's timing.

  When card text leaves a real choice open, ask the owner rather than
  guessing. Several of my first guesses were wrong.
- **`src/game.cpp`** holds all rules. **`tests/native_tests.cpp`** has a test
  per card and character group.

## How the pieces fit

**Data:** `include/bang/static_data.hpp` and `src/static_data.cpp`.

- **Cards:** 54 card names and 120 physical copies. Copies 0–79 are the base
  game, 80–99 Renegade-Law, 100–119 Renegade-Outlaw.
- **Characters:** 36. Ids 24–29 are Renegade-Law and 30–35 Renegade-Outlaw.
- **Resources:** a new card kind (`CardKind::kResource`).

**Seats and factions:** the engine always has two seats.

- Seat 0 (`Side::kLaw`) plays first; seat 1 (`Side::kOutlaw`) plays second.
- A card's `Side` is its **back**. Every Renegade card carries a Law or Outlaw
  back, so the Renegades' deck mixes both.
- `GameMode` and `SeatFaction(seat, mode)` decide what each seat plays.
  `StartingDeck`, `CharacterIds` and `StartingHandSize` all take the seat and
  return that faction's deck, roster or hand size.
- Because the two seats are kept, the AI's inputs did not change for
  The Third Group.

**Resources:**

- They are played like blue Equipment and used later with
  `ActionKind::kUseResource`.
- They can't be used the turn they were played (`entered_play_turn_`); see
  `CanUseResource`.
- Avoid-type Resources answer hits through the normal avoid action.

**Pausing a draw or a repeat until the next decision:** two characters need a
choice in the middle of engine code that can't pause.

- `ApplyAction` calls `ApplyLegalAction`, then two hooks:
  1. `BeginKittyLeroyRepeat`: Kitty Leroy's repeated Resource effect is aimed
     after the first one fully resolves.
  2. `BeginDutchHarryLooks`: Dutch Harry's draws are queued in `dutch_draws_`.
     Before the next decision, the interrupted phase is saved, he looks at each
     card in `Phase::kChooseDutchHarry`, and then the phase is restored.
- `GameState::CardsSetAside()` lists cards held aside during those looks; the
  card-conservation test counts them.

**New phases** are appended after `kTerminal`: Switch, deck draws (Union
Pacific / The Marshal), Ambush, Slap, Dutch Harry. Phase capacity is 32.

**Movement triggers** (Tom Thorn, Clock, Jango, Sidekick, Jim Longhair) live in
`SwapCharacters` and `AdvanceMovementTriggers`. Jim Longhair resolves last and
starts a fresh swap of the opponent's side.

**Web:**

- **Server (`src/web.cpp`):**
  - The `/api/new` request takes a `mode` string.
  - In each player's JSON, `side` is the faction name and `handBacks` lists the
    backs of the cards in that hand.
  - Each session's saved file (its journal) records the mode.
- **Front end:** `web/app.js` (`FORMATS` table, dialog) and `web/styles.css`
  (`--renegades-light`). The front end is phase-agnostic, so new choices
  appear as generic buttons.

## Rules that must keep holding

- **The base game must play exactly as before.**
  `tests/policy_parity_tests.py` replays 400 base-game decisions against
  `tests/fixtures/policy_logits_base.json`. If it fails, base behaviour has
  changed.
- **Policy input layouts:** a new fixed-size input block must become a
  `LayoutDims` field, and the JSON loader must default it to the base value.
  New enum values (phases, action kinds, movement triggers, reveal reasons)
  must be **appended**. Older checkpoints are then widened on load and map
  exactly (checkpoint ABI v2).
- **Card conservation:** the random-play tests (Reinforcements and The Third
  Group) check after every move that no card is lost or duplicated.

## Benchmark of the shipped agent (2026-10-28)

`bang_duel benchmark` plays two players against each other in any format,
without Torch. A player can be a policy JSON, `heuristic` or `random`. Each
deal is played twice with the seats swapped, and any engine error is reported
with its seed and last moves. Example:

```
bang_duel benchmark --mode renegades_vs_outlaw --a policy/static-policy.json --b heuristic --pairs 2000
```

What it showed for `policy/static-policy.json`, with sampled moves and 4,000
games per row:

| Format | vs heuristic | vs random | Against itself: first seat wins |
|---|---|---|---|
| base | 99.8% | 100% | 46.1% |
| reinforcements | 99.7% | 99.8% | 37.5% |
| renegades_vs_law | 98.7% (97.3% as Renegades) | 99.7% | 73.0% |
| renegades_vs_outlaw | 96.2% (92.4% as Renegades) | 98.9% | 85.7% |

How to read it:

- **The heuristic and random play are too weak to measure skill.** The
  heuristic has no code for any Renegade card.
- **In The Third Group the agent plays the base side far better than the
  Renegades.** It has never seen Renegade cards.
- **These formats lean one way even without skill.** In mirrors of two random
  players, the side with the base deck wins about 65% in The Third Group, and
  the Law wins about 37.6% in Reinforcements. So part of the imbalance belongs
  to the formats themselves (or to an undiscovered rules issue), not to the
  agent.

The benchmark also found two engine bugs, both fixed:

- **Pearl Hat** copied Switch or Ambush even when they couldn't be played,
  which softlocked the game. She also ignored El Chindi's immunity and Spurs.
- **Gold Pan** crashed when Charlie Basset's draw reshuffled the discard pile.

### A Renegades-aware heuristic

The heuristic (`src/heuristic.cpp`) knows the Renegades. It scores:

- **Resources:** placing them on its own side and using them.
- **Action cards:** Switch, Union Pacific, Whiskey, Ricochet and Ambush, each
  only when it helps.
- **New choices:** Ambush, Slap and Poker Alicia picks; Dutch Harry's look; Jim
  Longhair's swap; Kitty Leroy's payment.

These rules apply **only outside the base game**. Base-game play is
byte-identical; the benchmark checks this on fixed seeds. In the expansion
formats it also stops gifting its helpful base Equipment (Barrel, Hat, Scope…)
to the opponent. The base scoring still does that, which is a flaw of the
original heuristic, left alone on purpose.

Against it, the shipped agent wins:

| Format | Agent's win rate | Agent's first-seat / second-seat win rate | Heuristic mirror: first seat wins |
|---|---|---|---|
| reinforcements | 70.2% | 58.3% / 82.2% | 34.2% |
| renegades_vs_law | 50.5% | 50.4% / 50.5% (as Renegades) | 22.7% |
| renegades_vs_outlaw | 45.6% | 64.4% / 26.9% (as Renegades) | 41.7% |

A simple scripted bot now matches or beats the trained agent in The Third
Group. That is the case for training on the expansion.

The balance between the formats and the cards is analysed in
[docs/renegades_balance.md](docs/renegades_balance.md): no rules bug was
found, and Reinforcements favours the Outlaws with all three players. The
experiments (`tools/renegades_balance/`) can be re-run with a newly trained
policy.

## Known gaps and next steps

1. **The AI was never trained on Renegade cards.** It plays them through the
   widened inputs, using what it learned from the base game. A training run
   would need to select the format. Training still only deals base games, so
   that means a `mode` option in the training code (`src/training.cpp`, the
   Python driver).
2. **The browser-only WASM build** (`bang_static_web_new_game`,
   `web/static-engine.js`) has no mode parameter, so it deals base games only.
   It was not rebuilt here; that needs Emscripten.
3. **The new choices have clear prompts, but no 3D-stage animations.**
   Each new choice was reached in a real browser duel and checked:
   - Dutch Harry offers "Keep <card>" / "Discard it, take the next".
   - Ricochet has its own text and a "Stop the Ricochet" button.
   - Poker Alicia's prompt explains that skipping means drawing as usual.
   - Mystic Maid's option is named in the tooltip of her Equipment.

   What's still missing is dedicated animations on the 3D stage for the new
   cards. Separately, at about 1400×900 the stage overlaps your own
   characters' Equipment row. That layout issue predates the expansion.
4. **Merge `expansion` into `main`** when the owner is happy with it.
5. **Separate upstream fixes**, candidates for PRs to hmate9/bang, each on its
   own branch from `upstream/main`:
   - Stampede reloadability (fork commit `8ccd2f3`);
   - the v3 static actor width fix (`4efda22`);
   - a log-sequence fix.

   The Renegades art comes from dV Giochi and overrides the upstream repo's
   "no further third-party assets" note for this fork only. Reconsider it
   before sending anything upstream.
6. **A `play.ps1` helper** that sets PATH and starts the server would avoid
   the silent DLL failure. It was offered but not written.
