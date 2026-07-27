## What this changes

<!-- One or two sentences. What and why. -->

## Contribution checkpoint

Run `scripts/check.sh` (see [CONTRIBUTING.md](../CONTRIBUTING.md)) and tick what
you ran:

- [ ] `scripts/check.sh` — engine build, all tests, simulation and policy smoke
- [ ] `scripts/check.sh --with-web` — needed if you touched `web/` or the WASM build
- [ ] `scripts/check.sh --with-training` — needed if you touched the trainer or network

<details>
<summary>Check output (paste the last few lines)</summary>

```
```

</details>

## Engine invariants

Tick if your change touches the engine; delete this section otherwise.

- [ ] No rules logic added to JavaScript — the frontend still renders `LegalActions()`
- [ ] No hidden-information leak in observations or the player-scoped JSON view
- [ ] Games are still reproducible from seed + action sequence
- [ ] Checkpoint ABI changes fail loudly rather than silently

## Trained policy submission

Only if this PR updates `policy/static-policy.json`; delete otherwise.
Full workflow: [CONTRIBUTING.md#checkpoint-contributions](../CONTRIBUTING.md#checkpoint-contributions).

- **vs current champion:** `___ / ___ paired games (___%)`, seed `______`, ≥512 pairs
- **vs heuristic baseline:** `___ / ___ (___%)`
- **Exact command run:**
  ```sh
  ```
- **Training config:** model `v_`, `__` hours, `__` batch games, host `______`
- **What changed and why you think it helped:**
- [ ] Played it in the browser and it behaves sensibly
- [ ] Updated the iteration/source noted in `policy/README.md`

## Tests

<!-- Which test did you add or extend? If none, why not? -->
