// Turn the balance experiment outputs (runs/renegades_balance/*.json, or the
// directory given) into markdown tables, printed and saved as tables.md there.
import { readFileSync, existsSync, writeFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const out = process.argv[2] ?? join(here, "..", "..", "runs", "renegades_balance");
const BOTS = ["random", "heuristic", "agent"];
const load = (name, bot) => {
  const file = join(out, `${name}.${bot}.json`);
  return existsSync(file) ? JSON.parse(readFileSync(file, "utf8")) : null;
};
const pct = x => (100 * x).toFixed(1) + "%";
const pts = x => (x >= 0 ? "+" : "−") + Math.abs(100 * x).toFixed(1);
const first = s => s.seats[0].winRate;   // seat 0 plays first
const se = s => Math.sqrt(first(s) * (1 - first(s)) / s.seats[0].games);
const lines = [];
const table = (header, rows) => {
  lines.push(`| ${header.join(" | ")} |`, `|${header.map(() => "---").join("|")}|`);
  for (const row of rows) lines.push(`| ${row.join(" | ")} |`);
  lines.push("");
};

// 1. Each format as it stands.
const MODES = ["base", "reinforcements", "renegades_vs_law", "renegades_vs_outlaw"];
lines.push("## T1 First-seat win rate in mirrors", "");
table(["Format (first seat)", ...BOTS.map(b => `${b} mirror`)], MODES.map(mode => {
  const runs = BOTS.map(b => load(`base.${mode}`, b));
  const faction = runs.find(Boolean)?.seats[0].faction ?? "?";
  return [`${mode} (${faction})`, ...runs.map(s => s ? `${pct(first(s))} ±${(100 * se(s)).toFixed(1)}` : "–")];
}));

// 2. Per-seat mechanics, per bot.
const METRICS = [
  ["drawnPerGame", "drawn"], ["takenPerGame", "taken"], ["recoveredPerGame", "from discard"],
  ["damageDealtPerGame", "damage dealt"], ["eliminationsSufferedPerGame", "characters lost"],
  ["cardsPlayedPerGame", "cards played"], ["resourcesUsedPerGame", "Resources used"],
  ["avoidResponsesPerGame", "avoid responses"],
];
for (const bot of BOTS) {
  lines.push(`## T2 Per-game mechanics, ${bot} mirror (first seat / second seat)`, "");
  table(["Format", "turns", ...METRICS.map(m => m[1])], MODES.map(mode => {
    const s = load(`base.${mode}`, bot);
    if (!s) return [mode, "–", ...METRICS.map(() => "–")];
    return [mode, s.turnsPerGame.toFixed(1),
      ...METRICS.map(([key]) => `${s.seats[0][key].toFixed(1)} / ${s.seats[1][key].toFixed(1)}`)];
  }));
}

// 3. Packs removed from Reinforcements: Law (first seat) win rate.
const reinf = Object.fromEntries(BOTS.map(b => [b, load("base.reinforcements", b)]));
const PACKS = [
  ["base.base", "No Renegades at all (base game)"],
  ["pack.no_cards", "No Renegade cards (characters stay)"],
  ["pack.no_chars", "No Renegade characters (cards stay)"],
  ["pack.no_rl_cards", "Law loses its Renegade cards"],
  ["pack.no_ro_cards", "Outlaws lose their Renegade cards"],
  ["pack.no_rl_chars", "Law loses its Renegade characters"],
  ["pack.no_ro_chars", "Outlaws lose their Renegade characters"],
];
lines.push("## T3 Reinforcements with parts removed: Law win rate (change vs full Reinforcements)", "");
table(["Variant", ...BOTS], [
  ["Full Reinforcements", ...BOTS.map(b => reinf[b] ? pct(first(reinf[b])) : "–")],
  ...PACKS.map(([name, label]) => [label, ...BOTS.map(b => {
    const s = load(name, b);
    return s && reinf[b] ? `${pct(first(s))} (${pts(first(s) - first(reinf[b]))})` : "–";
  })]),
]);

// 4/5. Single cards and characters: value to their owner in points of win
// rate = how much the owner's win rate drops when they are removed.
const valueRows = (kind, seat, names) => names.map(name => {
  const values = BOTS.map(b => {
    const s = load(`${kind}.${seat}.${name}`, b);
    if (!s || !reinf[b]) return null;
    const drop = first(s) - first(reinf[b]);    // change in Law win rate
    return seat === "law" ? -drop : drop;       // owner's loss when removed
  });
  const known = values.filter(v => v !== null);
  const mean = known.length ? known.reduce((a, b) => a + b, 0) / known.length : 0;
  return { name, values, mean };
});
const RL_CARDS = "walker whitney carbine ambush agility feint ricochet switch missed spurs buffalo_rifle gold_pan howitzer pardner".split(" ");
const RO_CARDS = "lightning savage sharps spencer ambush agility feint union_pacific whiskey knife sidekick double_barrel indian_chief lucky_penny slap".split(" ");
const RL_CHARS = "charlie_basset hick_addams hottie_delou mystic_maid poker_alicia the_marshal".split(" ");
const RO_CHARS = "big_steve dutch_harry el_chindi jim_longhair king_fishers kitty_leroy".split(" ");
const valueTable = (title, rows) => {
  lines.push(title, "");
  table(["Removed", ...BOTS.map(b => `${b}`), "mean"], rows
    .sort((a, b) => b.mean - a.mean)
    .map(r => [r.name, ...r.values.map(v => v === null ? "–" : pts(v)), pts(r.mean)]));
};
lines.push("Positive = the owner wins that many points less without it (it was helping its owner).",
  "Each cell compares 10,000 games against 10,000; ±0.7 points is noise.", "");
valueTable("## T4 Renegade-Law cards: value to the Law", valueRows("card", "law", RL_CARDS));
valueTable("## T5 Renegade-Outlaw cards: value to the Outlaws", valueRows("card", "outlaw", RO_CARDS));
valueTable("## T6 Renegade-Law characters: value to the Law", valueRows("char", "law", RL_CHARS));
valueTable("## T7 Renegade-Outlaw characters: value to the Outlaws", valueRows("char", "outlaw", RO_CHARS));

// 8. Use in full Reinforcements (agent mirror): how often, and win rate when used.
for (const bot of ["agent", "heuristic"]) {
  const s = reinf[bot];
  if (!s) continue;
  lines.push(`## T8 Renegade card use in Reinforcements, ${bot} mirror`, "",
    "Win rate when the side used the card at least once, against that side's overall win rate. Correlation, not cause.", "");
  const rows = [];
  for (const [seat, names] of [[0, RL_CARDS], [1, RO_CARDS]]) {
    const side = s.seats[seat];
    for (const name of names) {
      const u = side.cards[name];
      if (!u) continue;
      rows.push([seat === 0 ? "Law" : "Outlaw", name, (u.uses / side.games).toFixed(2),
        pct(u.games / side.games), pct(u.wins / u.games), pts(u.wins / u.games - side.winRate)]);
    }
  }
  table(["Side", "Card", "uses/game", "games used", "win% when used", "vs side"], rows);
}

writeFileSync(join(out, "tables.md"), lines.join("\n"));
console.log(lines.join("\n"));
