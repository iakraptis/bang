# Renegades balance report

**Date:** 2026-10-28
**Question:** in our implementation, the Renegades formats favour one side.
Even with the same player on both sides, the Law wins only about 37% in
Reinforcements. Is that a rules bug, a property of the cards, or an artifact
of how our bots play?

## Summary

1. **No rules bug was found.** The suspicious cards were checked against
   their printed text and the rulebook FAQ, and nothing is wrong. The two
   bugs this benchmarking did find (Pearl Hat copying an unplayable Switch,
   and a Gold Pan crash) were fixed earlier and played no part in these
   numbers.
2. **Reinforcements favours the Outlaws, whoever plays.** The Law's win rate
   falls from the base game's level to 34–38% with all three test players:
   random moves, the scripted heuristic and the trained agent. Card draws are
   even. The Law loses more characters per game.
3. **Why it happens depends on who plays.** It can't be pinned on one card or
   character:
   - with the **heuristic**, the Renegade-Outlaw cards are the big factor;
   - with the **trained agent**, every Renegade card and character makes
     *its owner* weaker, whichever side that is. The agent has never been
     trained on them.
   - Life totals add a structural part. Renegade-Law characters average 3.8
     life against 4.25 for base Law characters, but abilities matter as much
     as life, and our players use the new abilities poorly.
4. **No single card or character dominates.** Removing any one moves the
   balance by at most about 4 points. Switch is the weakest card for every
   player. The Marshal, Walker, Agility and Feint are consistently among the
   most useful.
5. **The Third Group can't be judged yet.** The three players disagree on
   which side is stronger, because each plays one deck much better than the
   other.
6. **Nothing has been published on Renegades balance:** no reviews, forum
   threads or designer notes.

**Recommendation:** don't change any rules. Train the agent on the expansion
(see "Next steps"), then run this analysis again. A player that is competent
with every card is the only fair judge of these formats.

## Method

- **Tool:** `bang_duel benchmark` plays the same player on both seats (a
  *mirror*). In a mirror, any win-rate difference between the seats comes
  from the format, not from skill.
- **Deals:** each deal is played twice with the seats swapped.
- **Size:** every experiment is 10,000 games, which gives ±0.5 points on a
  win rate and ±0.7 on a difference between two experiments.
- **Three players**, because each has different blind spots:
  - **random**: uniformly random legal moves. It ignores abilities and
    timing, so a character is worth about its life.
  - **heuristic**: the scripted bot. It knows the Renegade cards since commit
    `5001f9a`, but plays the base game crudely.
  - **agent**: the shipped trained policy. It is strong at the base game, but
    the Renegade cards and characters are invisible to it (zero input
    weights).
- **Per-game statistics:** cards drawn, cards taken from the opponent, cards
  taken back from the discard pile, damage, characters lost, cards played,
  Resources used, and avoid responses, per seat.
- **Removal experiments:** one Renegade card (every Renegade copy of it) or
  one Renegade character is left out of one side, using the new opt-in
  `GameConfig.excluded_cards` / `excluded_characters`. The change in win rate
  is that item's value to its owner.

Rosters are 4 characters per side, as in the standard game. The full tables
are in `runs/renegades_balance/tables.md` (not in the repository);
`tools/renegades_balance/` regenerates them.

## 1. Each format as it stands

The first seat's win rate in mirrors. The first seat is the Law, except in
Renegades vs Outlaws, where it is the Outlaws.

| Format | random | heuristic | agent |
|---|---|---|---|
| base (Law) | 53.2% | 50.4% | 45.4% |
| reinforcements (Law) | 37.6% | 33.5% | 37.7% |
| renegades_vs_law (Law) | 65.3% | 23.0% | 73.5% |
| renegades_vs_outlaw (Outlaws) | 64.9% | 41.6% | 85.5% |

- **Reinforcements:** all three players agree, with the Law 8–17 points worse
  off than in the base game.
- **The Third Group:** the three players disagree on direction (section 4).
- **Base game:** for reference, the agent's Law win rate (45.4%) matches the
  converged base meta, where the Outlaws were already favoured by about
  11 points (`docs/meta_analysis.md`).

## 2. Reinforcements: where the Outlaws' edge comes from

### What changes in a game (agent mirror, Law / Outlaws, per game)

| | base | Reinforcements |
|---|---|---|
| turns (both sides) | 32.5 | 36.6 |
| cards drawn | 57.3 / 54.8 | 58.8 / 58.7 |
| damage dealt | 16.1 / 17.3 | 15.3 / 16.5 |
| characters lost | 3.4 / 3.1 | 3.5 / 2.9 |
| cards taken back from the discard pile | 1.1 / 0.4 | 2.3 / 1.1 |
| Resources used | – | 4.6 / 3.0 |

- **No draw engine runs away:** both sides draw nearly the same number of
  cards.
- **The Outlaws simply finish more characters.** Fewer of the Law's cards
  turn into damage, and the Law loses about 0.6 more characters per game.
  The random and heuristic mirrors show the same pattern.

### Removing whole parts of the expansion

The Law's win rate in Reinforcements is 37.6% (random), 33.5% (heuristic) and
37.7% (agent). Each row shows the Law's win rate with that part removed, and
in brackets the change from full Reinforcements.

| Removed | random | heuristic | agent |
|---|---|---|---|
| All Renegade cards and characters (= base) | 53.2% (+15.6) | 50.4% (+16.9) | 45.4% (+7.7) |
| All Renegade cards | 45.3% (+7.7) | 42.7% (+9.2) | 43.8% (+6.1) |
| All Renegade characters | 44.3% (+6.8) | 32.6% (−0.9) | 42.8% (+5.1) |
| The Law's Renegade cards | 44.9% (+7.3) | 26.7% (−6.9) | 45.6% (+7.9) |
| The Outlaws' Renegade cards | 37.5% (−0.0) | 50.5% (+17.0) | 32.3% (−5.4) |
| The Law's Renegade characters | 45.0% (+7.4) | 37.3% (+3.8) | 46.4% (+8.7) |
| The Outlaws' Renegade characters | 37.8% (+0.2) | 26.8% (−6.7) | 33.6% (−4.1) |

How to read it:

- **Random:** the Law suffers from its own Renegade characters, which have
  less life. The cards barely matter, because random play doesn't use them
  well.
- **Heuristic:** the Outlaws' Renegade cards are worth **17 points** to them,
  against 7 for the Law's pack. Their pack hits harder: Lightning, Sharps,
  Spencer, Double Barrel and Savage, against the Law's 3 Missed!, 2 Switch
  and Spurs.
- **Agent:** removing either side's Renegade cards or characters *helps that
  side*. The agent plays unfamiliar cards badly, so for it any Renegade
  content is dead weight. These rows describe the agent, not the game.
- **One caveat:** removing a pack also shrinks that side's deck from 60 to
  40 cards, so the deck runs out sooner. This muddies the pack rows, but not
  the single-card rows below.

All three players agree on one thing: **the Law's half of the expansion helps
the Law less than the Outlaws' half helps the Outlaws.** How big the gap is
depends on skill.

## 3. Single cards and characters

**Value to the owner** = how many points the owner's win rate drops when the
item is removed. Removing a card also thins the deck, so a **negative value
means "weaker than the average card in that deck"**, not "harmful". ±0.7 is
noise.

### Renegade-Law cards (value to the Law)

| Card | random | heuristic | agent | mean |
|---|---|---|---|---|
| Walker | +2.4 | +3.5 | +1.7 | **+2.5** |
| Buffalo Rifle | −1.7 | +4.5 | +2.5 | +1.8 |
| Agility | +0.8 | +2.7 | +1.7 | +1.7 |
| Feint | +0.9 | +2.4 | +1.2 | +1.5 |
| Ricochet | +1.1 | +1.4 | +0.8 | +1.1 |
| Howitzer | −0.8 | +2.9 | +1.0 | +1.0 |
| Carbine | +0.4 | +0.8 | +0.7 | +0.6 |
| Whitney | +0.1 | +0.6 | +0.6 | +0.4 |
| Pardner | −1.0 | +0.7 | −0.2 | −0.2 |
| Spurs | −1.2 | +1.0 | −0.8 | −0.4 |
| Gold Pan | −1.1 | −0.4 | −0.1 | −0.5 |
| Missed! (3 copies) | −0.2 | −1.1 | −1.9 | −1.0 |
| Ambush | −2.1 | +0.8 | −3.5 | −1.6 |
| Switch (2 copies) | −2.7 | −4.5 | −3.9 | **−3.7** |

### Renegade-Outlaw cards (value to the Outlaws)

| Card | random | heuristic | agent | mean |
|---|---|---|---|---|
| Whiskey | +2.5 | +2.0 | +0.6 | **+1.7** |
| Sharps | +1.8 | +1.2 | +0.4 | +1.1 |
| Agility | +1.4 | +0.7 | +1.3 | +1.1 |
| Savage | +1.4 | +1.6 | +0.2 | +1.1 |
| Spencer | +2.5 | +0.3 | +0.2 | +1.0 |
| Sidekick | −0.4 | +2.7 | +0.4 | +0.9 |
| Union Pacific | +0.2 | +1.6 | +0.8 | +0.9 |
| Knife | +1.9 | −0.1 | +0.1 | +0.6 |
| Feint | +1.6 | +0.7 | −0.5 | +0.6 |
| Lucky Penny | −0.7 | +1.3 | −1.5 | −0.3 |
| Lightning | +0.7 | −0.5 | −1.3 | −0.3 |
| Indian Chief | −1.0 | +1.2 | −1.6 | −0.4 |
| Ambush | −0.5 | −0.9 | −2.2 | −1.2 |
| Double Barrel | −2.1 | +0.3 | −2.3 | −1.4 |
| Slap (2 copies) | −1.7 | −2.3 | −3.5 | **−2.5** |

### Renegade characters

| Law character | mean value | | Outlaw character | mean value |
|---|---|---|---|---|
| The Marshal | **+2.2** | | Big Steve | **+2.0** |
| Mystic Maid | −0.2 | | King Fishers | +0.4 |
| Hick Addams | −0.6 | | Jim Longhair | −0.9 |
| Hottie Delou | −0.7 | | Dutch Harry | −1.3 |
| Charlie Basset | −1.6 | | El Chindi | −1.6 |
| Poker Alicia | **−2.1** | | Kitty Leroy | **−2.4** |

Per-player values are in the full tables. A character's value is relative to
the rest of its roster: 4 characters are dealt from 18.

What stands out:

- **Switch** is the weakest card for every player. When the Law swaps the
  Outlaws with it, it can trigger *their* effects: Tom Thorn's shot,
  Sidekick's Derringer, Jim Longhair. This follows the card texts ("whenever
  he becomes active", whoever moved him) and matches how the base game's
  Water works. So it is a weak card, not a bug.
- **Slap** and **Ambush** rank low because they trade a card for one of the
  opponent's cards and deal no damage. Our players also use them clumsily.
- **The Marshal** (3 extra cards) and **Big Steve** (hand limit 6) help the
  most. These are passive, so even an unskilled player benefits.
- **The characters that need judgement rank lowest:** Poker Alicia, Kitty
  Leroy, Charlie Basset, El Chindi and Dutch Harry. That fits your point that
  balance rests on abilities: our players don't yet use these abilities well,
  so their values are understated, not their true worth.

## 4. The Third Group

| | random | heuristic | agent |
|---|---|---|---|
| Renegades vs Law: Renegades win | 34.7% | 77.0% | 26.5% |
| Renegades vs Outlaws: Renegades win | 35.1% | 58.4% | 14.5% |

The direction depends entirely on the player:

- **The heuristic** knows the Renegade cards well, but misplays parts of the
  base game. It uses 10.5 Resources per game as the Renegades.
- **The agent** knows only the base cards.
- **Random play** can't value Resources, half of which only work the turn
  after they are played.

None of these players is equally good with both decks, so these numbers
mostly measure skill. **This format can only be judged after training.**

## 5. Rules audit

The cards and characters with the most influence were checked against
their printed text (`docs/renegades_cards.md`), the rulebook FAQ, and the
owner's rulings:

| Item | Checked | Result |
|---|---|---|
| Switch, Tom Thorn, Sidekick, Jim Longhair | Swap effects fire on a swap the opponent causes ("whenever he becomes active") | Correct; same as base Water |
| Lightning | One additional BANG! this turn | Correct |
| Sharps | Ignores the target's ability and Equipment; hand ⊘ cards still work | Correct |
| Spencer | 2 damage against 4+ life, otherwise 1 | Correct |
| Double Barrel, Howitzer | Second target only if avoided (DB) / both characters if not avoided (Howitzer) | Correct |
| Slap, Ambush | Either half or both; Wild Bill reacts to each removal | Correct |
| Resources | Usable from the turn after they are played, from the AC only | Correct |
| Starting hands | Law 4, Outlaws 5, Renegades the other count | Correct |
| Card draw | Even between the sides in every mirror | No sign of a runaway draw engine |

No discrepancy was found. The two bugs this benchmarking turned up were
fixed earlier and are not in these results:

- Pearl Hat could copy a Switch or Ambush that couldn't be played, which
  softlocked the game.
- Gold Pan crashed after Charlie Basset's draw reshuffled the discard pile.

## 6. Outside references

Searches for published balance discussion of BANG! The Duel – Renegades
found none: no BoardGameGeek threads, reviews or designer notes on how the
factions compare. Only the official rules (formats and hand sizes) and store
listings came up:

- [Official rules (dV Giochi, PDF)](https://www.dvgiochi.com/giochi/bang-theduel-renegades/download/Bang_ItaEng_TheDuelRenegades_Rules-ENG.pdf)
- [BANG! The Duel on Dice Tower](https://dicetower.com/board-game/182094)

## 7. Limits of this analysis

- **None of the three players is strong with every card.** The agent can't
  see Renegade content, the heuristic is crude, and random play is random.
  Every number here mixes the format's balance with the player's skill.
- **Removing a card or character also changes deck size or roster
  composition.** Values are relative to an average card or character, not
  absolute.
- **Only 4-character rosters were tested.**

## Next steps

1. **Keep the rules as they are.** Nothing points to a bug.
2. **Train the agent on the expansion formats,** with format and faction
   inputs, playing both seats.
3. **Re-run this analysis with the trained agent:**
   `AGENT=<new policy.json> OUT=runs/renegades_balance_trained tools/renegades_balance/run.sh`,
   then `node tools/renegades_balance/analyze.mjs runs/renegades_balance_trained`.
   If Reinforcements still sits near 37% for the Law with a player that is
   competent with every card, the imbalance belongs to the game itself.
4. **Optional:** repeat the single-character removals in The Third Group
   once the agent is trained.
