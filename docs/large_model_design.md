# A 10x policy network for the 48-hour comparison run

## Summary

`bang_structured_v3` is a 2.34 M-parameter actor-critic (10.5x the current
222 k deployable network, 10.3x counting training-only heads). It keeps the
current structure — split public/identity encoders, fused residual trunk,
semantic action encoder, segmented-softmax actor — and changes only widths and
trunk depth, so a champion-versus-champion result is attributable to capacity
rather than to a redesign.

The measured obstacle is not the model. It is that this host's PyTorch links
Debian's reference BLAS (`BLAS_INFO=generic`), which runs sgemm at 17 GFLOPS
single-threaded and does not scale with threads. Switching to OpenBLAS makes
the 10x model **four times cheaper per update than the current 222 k model is
today**. Without that switch the 10x model costs ~9.5x more per update and
would get roughly one eighth as many PPO updates in 48 hours.

## Current network (measured)

| Module | Shape | Parameters |
|---|---|---:|
| `public_encoder` | 861 → 128 | 110,336 |
| `identity_encoder` | 368 → 64 | 23,616 |
| `state_fusion` | 192 → 128 | 24,704 |
| `residuals` (2 blocks) | 128 → 128 | 33,024 |
| `state_policy` | 128 → 64 | 8,256 |
| `action_encoder` | 91 → 64 | 5,888 |
| `actor` | 256 → 1 | 257 |
| `critic` | 128 → 128 → 1 | 16,641 |
| **Deployable subtotal** | | **222,722** |
| `privileged_encoder` | 448 → 64 | 28,736 |
| `auxiliary_critic` | 192 → 128 → 1 | 24,961 |
| **Total** | | **276,419** |

## Proposed `bang_structured_v3`

| Module | Shape | Parameters | vs current |
|---|---|---:|---:|
| `public_encoder` | 861 → 512 | 441,344 | 4.0x |
| `identity_encoder` | 368 → 256 | 94,464 | 4.0x |
| `state_fusion` | 768 → 512 | 393,728 | 15.9x |
| `residuals` (4 blocks) | 512 → 512 | 1,050,624 | 31.8x |
| `state_policy` | 512 → 160 | 82,080 | 9.9x |
| `action_encoder` | 91 → 160 | 14,720 | 2.5x |
| `actor` | 640 → 1 | 641 | 2.5x |
| `critic` | 512 → 512 → 1 | 263,169 | 15.8x |
| **Deployable subtotal** | | **2,340,770** | **10.5x** |
| `privileged_encoder` | 448 → 256 | 114,944 | 4.0x |
| `auxiliary_critic` | 768 → 512 → 1 | 394,241 | 15.8x |
| **Total** | | **2,849,955** | **10.3x** |

Constants to change in `BangActorCriticImpl`:

```
kPublicHidden   128 -> 512
kIdentityHidden  64 -> 256
kStateHidden    128 -> 512
kActionHidden    64 -> 160
kPolicyHidden   128 -> 512
residual blocks   2 -> 4
```

Everything else is unchanged: `Tanh` activations, orthogonal initialisation
with the same gains (1.0 for encoders, 0.5 for residual blocks, 0.01 for the
actor), the `[s, a, s*a, s-a]` interaction, and the tanh-residual update
`s <- tanh(s + W s)`.

### Why capacity is placed here

**The trunk is cheap, the action head is expensive.** The trunk runs once per
decision; the action encoder and actor run once per *legal action*, and rollout
telemetry implies about eight legal actions per learner decision (normalised
entropy 61% of `log(legal)`). Parameters added to the action path therefore
cost roughly eight times as much compute as parameters added to the trunk.

This was measured rather than assumed. A variant with a two-layer MLP actor
head (1.36 M parameters) was *slower* than a wide-trunk variant with 2.06 M
parameters — 8,079 ms versus 9,250 ms per minibatch for 51% more capacity. The
wide-trunk allocation buys more capacity per unit of wall clock.

**The actor head still needed widening.** The actor is a linear score over the
interaction block, so its expressiveness is capped by `kActionHidden`. Raising
64 → 160 gives 640 interaction features instead of 256 for 641 head parameters.
This is the cheapest genuine increase in policy expressiveness available.

**Depth 2 → 4.** BANG! decisions chain — a hit creates a response, which
creates a queued retaliation. Two residual blocks over a fused state is shallow
for that. Four blocks at width 512 stays well inside what plain tanh residuals
train reliably.

**Unchanged activations and initialisation.** A larger model that also swaps in
LayerNorm/GELU would confound the comparison. If the deeper trunk shows
gradient-norm growth or persistent target-KL early stops, pre-block LayerNorm
is the first fallback — but only as a second experiment.

## Measured cost

Benchmarked with a PyTorch mirror of the C++ module, at the production
minibatch shape (3,500 decisions x 8 legal actions, 6 threads). The mirror is
calibrated: it predicts 1,097 ms per minibatch for the current network against
~1,125 ms observed in production (`updateSeconds` 36 s / 4 epochs / 8
minibatches).

| | update (ms/minibatch) | rollout inference (ms/256-decision batch) |
|---|---:|---:|
| 222 k, reference BLAS (**today**) | 1,078 | 46.6 |
| 2.34 M, reference BLAS | 10,299 | 392.5 |
| 222 k, OpenBLAS | 67 | 1.3 |
| **2.34 M, OpenBLAS** | **270** | **5.7** |

Raw sgemm (3500x861x512) on this host: 17 GFLOPS with reference BLAS at any
thread count, 303 GFLOPS with OpenBLAS. The reference BLAS is single-threaded
and unvectorised, which is why `--torch-threads` has no measurable effect on
training time today (4, 6, 8, 10 and 12 threads were all within noise).

### Confirmed on the real trainer

OpenBLAS was installed and made the system default; the native trainer was then
re-run from the existing iteration-2995 checkpoint against a 90% frontier-
checkpoint / 10% heuristic opponent mixture, writing to a scratch checkpoint
directory:

| Configuration | rollout | update | iteration | env/s | train/s |
|---|---:|---:|---:|---:|---:|
| 222 k, reference BLAS | 11.5 s | 36.0 s | 47.8 s | 5,800 | 3,100 |
| 222 k, OpenBLAS, `OPENBLAS_NUM_THREADS=6` | 0.9 s | 2.6 s | **3.6 s** | 75,752 | 43,514 |
| 222 k, OpenBLAS, threads unset | 60.5 s | 18.3 s | 78.8 s | 1,114 | 6,182 |

**A 13x end-to-end speedup — and a regression if the thread count is left
unset.** OpenBLAS defaults to one thread per logical core (12 here), which
oversubscribes against Torch's OpenMP pool and collapses rollout throughput to
below the unoptimised reference BLAS. `train.sh` now exports
`OPENBLAS_NUM_THREADS=6`, and `bang-web.service` sets it to 2 to match its
`--torch-threads 2`.

The rollout speedup (11.5 s to 0.9 s) shows rollout was almost entirely
policy-inference bound, not simulation bound.

v3 was then measured the same way, at production batch settings against a
checkpoint opponent of its own architecture:

| Configuration | rollout | update | iteration | updates in 48 h |
|---|---:|---:|---:|---:|
| 222 k, reference BLAS (the existing champion's run) | 11.5 s | 36.0 s | 47.8 s | ~3,000 |
| 2.34 M, reference BLAS (projected) | — | ~344 s | ~360 s | ~470 |
| **2.34 M, OpenBLAS** | **2.2 s** | **6.7 s** | **8.9 s** | **~15,000** |

So v3 carries 10.5x the parameters and still gets several times more updates
than the existing champion received. The 8.9 s figure is from an untrained
network, whose games are shorter than a converged policy's; budget 10-12 s per
iteration once games lengthen and league evaluation overhead is included.

## OpenBLAS (done)

`libtorch_cpu.so` links `libblas.so.3`, a Debian alternatives symlink, so this
needed no rebuild of PyTorch or of the training binary:

```sh
apt-get install -y libopenblas-dev
update-alternatives --set libblas.so.3-x86_64-linux-gnu \
  /usr/lib/x86_64-linux-gnu/openblas-pthread/libblas.so.3
update-alternatives --set liblapack.so.3-x86_64-linux-gnu \
  /usr/lib/x86_64-linux-gnu/openblas-pthread/liblapack.so.3
```

sgemm on this host went from 17 GFLOPS to 530 GFLOPS. To revert, point both
alternatives back at `/usr/lib/x86_64-linux-gnu/{blas,lapack}/`.

`OPENBLAS_NUM_THREADS` is not optional — see the measured regression above. It
is now set in `train.sh` (6) and `bang-web.service` (2).

### Effect on the comparison

v3 will have both more parameters and more updates than the existing champion
received. That is accepted: the question being answered is "is the new
champion stronger?", which paired-seed match play settles regardless of how
each was trained. It is *not* a controlled test of capacity alone; that would
need a second 48-hour run of the 222 k model under OpenBLAS.

## Implementation (done)

The module is parameterised rather than forked, so one binary loads both sizes.

1. **`include/bang/policy_architecture.hpp`** (new) — `PolicyArchitecture`,
   the `PolicyArchitectureV2()`/`PolicyArchitectureV3()` presets,
   `ArchitectureForModelType`, `IsSupportedModelType` and `ResolveModelType`.
   Deliberately LibTorch-free so the browser static-policy path shares it.
2. **`policy_network.hpp/.cpp`** — `BangActorCriticImpl` takes a
   `PolicyArchitecture` (defaulting to v2) and builds its residual blocks in a
   loop. New `LoadPolicyFromCheckpoint(path)` reads the checkpoint's own
   metadata, builds the matching architecture and loads into it; the two-
   argument `LoadPolicyCheckpoint` now rejects a checkpoint whose architecture
   does not match the module it was handed.
3. **All load sites build from metadata** — `web.cpp`, league opponents,
   `evaluate`, `self-evaluate` and `checkpoint-canary` all go through
   `LoadPolicyFromCheckpoint`. `checkpoint.cpp` accepts either model type;
   ruleset, observation ABI, action-feature ABI and tensor sizes are still
   rejected on mismatch.
4. **`--model v2|v3`** — on the native trainer and `tools/train.py`, recorded
   in `settings()` so `manifest.json` carries it and a resume with a different
   value is rejected. Resuming a v3 checkpoint as v2 fails with an explicit
   message rather than a tensor-shape error.
5. **Static web export** — `export_static_policy.py` derives the parameter list
   from the checkpoint's model type, and `static_policy.cpp` reads every layer
   width from the architecture preset instead of hardcoded 128/64/192/256.

Tests: `tests/native_tests.cpp` covers the presets, unknown-model rejection and
a four-block static forward pass; `tests/train_cli_tests.py` trains a v3
checkpoint and drives it through the canary, `web-smoke`, the static export,
`static-policy-smoke`, and the cross-architecture resume guard.

### Still open before deploying a v3 champion to the static site

`export_static_policy.py` writes every weight as decimal text inside a JSON
document, and `build_static_web.sh` bakes that file into the Emscripten
preload blob. The v2 export measures 4,260,901 bytes, which is the whole of
`web/dist-static/bang_static_web.data`. The v3 actor path holds 2.08 M floats,
so the same format produces roughly 43 MB.

That breaks two things:

- **Cloudflare Pages rejects files above 25 MiB**, and the deployment workflow
  ships through `wrangler pages deploy`.
- **Startup latency.** The preload blob is fetched and parsed in full before
  the first move. Parsing the 4.3 MB v2 file takes about 0.5 s natively, and
  WASM is slower.

The fix is to stop encoding floats as text. A float32 binary payload is 8.3 MB
and loads by copy rather than by parse; float16 halves that to roughly today's
size, at a precision loss that should be checked against the native policy
before it is trusted. Neither blocks training — only publishing a v3 champion.

## Hyperparameters

Keep them identical to the v2 campaign — learning rate 3e-4, 4 epochs, 8
minibatches, 256 games, entropy 0.01, target KL 0.015 — so the runs stay
comparable. Watch the first chunk for two failure signs specific to the larger
model:

- target-KL early stopping firing on most updates (`KL *` in telemetry) — drop
  the learning rate to 2e-4;
- gradient norm climbing well past the ~0.38 seen in the v2 run — that is the
  signal to try pre-block LayerNorm.

If OpenBLAS lands, the extra throughput is better spent on `--batch-games 512`
than on more epochs: PPO's sample efficiency saturates in epochs, and larger
rollout batches also reduce the variance of the league opponent mixture.
