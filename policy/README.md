# Browser policy

`static-policy.json` is the browser-native actor exported from a
`bang_structured_v2` checkpoint at iteration 2861 (run `mmd_v2_a030_s2`). It was
trained with the standard v2 League PPO recipe plus a Magnetic Mirror Descent
term (`--mmd-alpha 0.3 --magnet-interval 50`) and beats the previous
iteration-2995 PPO champion 55.98% over 2048 paired seeds (seed 1300001).
Keeping the inference artifact in the repository makes the static web build
reproducible without access to local training runs.

Regenerate it from a complete checkpoint with:

```sh
python3 tools/export_static_policy.py \
  runs/mmd_v2_a030_s2/champion/policy.pt \
  policy/static-policy.json
```
