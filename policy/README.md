# Browser policy

`static-policy.json` is the browser-native actor exported from the validated
`bang_structured_v2` checkpoint at iteration 2995. Keeping the inference
artifact in the repository makes the production build reproducible without
access to local training runs.

Regenerate it from a complete checkpoint with:

```sh
python3 tools/export_static_policy.py \
  runs/bang_league_ppo/main/latest.pt \
  policy/static-policy.json
```
