#!/usr/bin/env bash
# Balance experiments for the Renegades formats. Each run is a mirror (the
# same player on both seats), 5000 paired deals = 10,000 games, with per-seat
# statistics written to $OUT/<name>.<player>.json (default runs/renegades_balance).
# Then: node tools/renegades_balance/analyze.mjs [$OUT]
set -u
cd "$(dirname "$0")/../.."
# On Windows the Torch DLLs must be on PATH if the exe was built with Torch.
[ -d .venv/Lib/site-packages/torch/lib ] && export PATH="$PWD/.venv/Lib/site-packages/torch/lib:$PATH"
OUT="${OUT:-runs/renegades_balance}"
mkdir -p "$OUT"
PAIRS=5000
AGENT="${AGENT:-policy/static-policy.json}"
declare -A BOT=([random]=random [heuristic]=heuristic [agent]=$AGENT)

run() {  # name mode bot [extra args...]
  local name=$1 mode=$2 bot=$3
  shift 3
  local file="$OUT/$name.$bot.json"
  [ -s "$file" ] && return
  ./build-train/bang_duel.exe benchmark --mode "$mode" --a "${BOT[$bot]}" --b "${BOT[$bot]}" \
    --pairs $PAIRS --stats "$file" "$@" > /dev/null || echo "FAILED $name $bot" >&2
}

RL_CARDS="walker whitney carbine ambush agility feint ricochet switch missed spurs buffalo_rifle gold_pan howitzer pardner"
RO_CARDS="lightning savage sharps spencer ambush agility feint union_pacific whiskey knife sidekick double_barrel indian_chief lucky_penny slap"
RL_CHARS="charlie_basset hick_addams hottie_delou mystic_maid poker_alicia the_marshal"
RO_CHARS="big_steve dutch_harry el_chindi jim_longhair king_fishers kitty_leroy"

for bot in random heuristic agent; do
  # E1: every format as it stands.
  for mode in base reinforcements renegades_vs_law renegades_vs_outlaw; do
    run "base.$mode" "$mode" "$bot"
  done

  # E2: whole Renegade packs removed from Reinforcements.
  rl_cards=(); for c in $RL_CARDS; do rl_cards+=(--exclude-card "law:$c"); done
  ro_cards=(); for c in $RO_CARDS; do ro_cards+=(--exclude-card "outlaw:$c"); done
  rl_chars=(); for c in $RL_CHARS; do rl_chars+=(--exclude-character "law:$c"); done
  ro_chars=(); for c in $RO_CHARS; do ro_chars+=(--exclude-character "outlaw:$c"); done
  run "pack.no_rl_cards" reinforcements "$bot" "${rl_cards[@]}"
  run "pack.no_ro_cards" reinforcements "$bot" "${ro_cards[@]}"
  run "pack.no_cards" reinforcements "$bot" "${rl_cards[@]}" "${ro_cards[@]}"
  run "pack.no_rl_chars" reinforcements "$bot" "${rl_chars[@]}"
  run "pack.no_ro_chars" reinforcements "$bot" "${ro_chars[@]}"
  run "pack.no_chars" reinforcements "$bot" "${rl_chars[@]}" "${ro_chars[@]}"

  # E3/E4: one Renegade card name or character removed at a time.
  for c in $RL_CARDS; do run "card.law.$c" reinforcements "$bot" --exclude-card "law:$c"; done
  for c in $RO_CARDS; do run "card.outlaw.$c" reinforcements "$bot" --exclude-card "outlaw:$c"; done
  for c in $RL_CHARS; do run "char.law.$c" reinforcements "$bot" --exclude-character "law:$c"; done
  for c in $RO_CHARS; do run "char.outlaw.$c" reinforcements "$bot" --exclude-character "outlaw:$c"; done
done
echo "all done"
