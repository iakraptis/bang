# Learned Meta Analysis — `bang_league_ppo`

**Generated:** 2026-07-24
**Source:** `runs/bang_league_ppo/evaluations/iteration_{001495,001545,001595,001645,001695}.json`
**Method:** `greedy_checkpoint_self_play_v1`, 2048 games per evaluation, ruleset `base_four_character`
**Sample:** 5 successive checkpoints × 2048 games, all on seed `4470001` (~2048 distinct game setups replayed by each checkpoint)
**Play-style telemetry:** pooled over the last 100 PPO iterations of `main/metrics.jsonl`

This describes the meta as the converged policy plays it, at iterations 1495–1695.

---

## 1. The game is outlaw-favoured by ~11 points

Law 44.5% / Outlaw 55.5%, and it never wobbles: 53.8–57.1% outlaw across all five evaluations.

| Iteration | Law | Outlaw | Mean turns |
|---|---|---|---|
| 1495 | 44.6% | 55.4% | 32.9 |
| 1545 | 45.0% | 55.0% | 33.2 |
| 1595 | 46.2% | 53.8% | 35.1 |
| 1645 | 42.9% | 57.1% | 34.7 |
| 1695 | 43.9% | 56.1% | 32.2 |

This is the single biggest structural fact in the meta, and it matches the training-side seat gap (the learner wins ~60% as outlaw vs ~51% as law). It is consistent enough across independent checkpoints to read as genuine `base_four_character` ruleset imbalance rather than a policy artifact.

## 2. Character power spread is wide — wider than the side imbalance

Side-adjusted uplift = character win rate minus that side's baseline.

### Law (baseline 44.5%) — spread 38.5% → 51.8%

| Character | Win% | Uplift | Spread across checkpoints | Elim% |
|---|---|---|---|---|
| Dalon Ranger | 51.8% | **+7.3** | +5.6 / +8.2 | 78.8% |
| Wyatt Ear | 50.5% | **+6.0** | +4.4 / +7.4 | 85.1% |
| Wild Bill | 48.8% | +4.3 | +2.9 / +7.1 | 85.8% |
| Alan Pinkertoon | 48.4% | +3.8 | +2.6 / +4.5 | 83.0% |
| Bill Tightman | 48.1% | +3.6 | +1.2 / +6.8 | 77.6% |
| Pat Garret | 46.7% | +2.2 | −0.2 / +5.0 | 81.9% |
| Jango | 41.4% | −3.1 | −4.0 / −1.4 | 88.1% |
| The Stranger | 40.6% | −4.0 | −4.8 / −2.0 | 77.7% |
| Annie Oakey | 40.5% | −4.0 | −4.9 / −3.1 | 73.5% |
| Tex Killer | 40.4% | −4.1 | −6.6 / −2.2 | 86.9% |
| Buffalo Bell | 38.8% | −5.7 | −7.3 / −4.5 | **91.2%** |
| Bart Masterson | 38.5% | **−6.0** | −7.5 / −5.4 | 83.1% |

### Outlaw (baseline 55.5%) — spread 46.3% → 64.8%

| Character | Win% | Uplift | Spread across checkpoints | Elim% |
|---|---|---|---|---|
| Soundance Kid | 64.8% | **+9.3** | +6.4 / +11.8 | **63.6%** |
| Tom Thorn | 64.1% | **+8.7** | +7.2 / +10.4 | 74.4% |
| Sid Curry | 59.3% | +3.8 | +2.0 / +5.5 | 68.9% |
| Bull Anderson | 57.4% | +2.0 | +1.2 / +3.3 | 76.2% |
| Cattle Katie | 55.7% | +0.3 | −0.9 / +1.6 | 73.7% |
| Slim Poet | 55.4% | −0.0 | −2.8 / +1.9 | 76.6% |
| Babe Leroy | 54.2% | −1.3 | −3.0 / +0.1 | 78.4% |
| Toco Ramirez | 53.9% | −1.6 | −2.6 / −0.5 | 77.2% |
| Dalton Bros | 53.4% | −2.1 | −2.8 / −1.0 | 80.5% |
| West Harding | 52.5% | −3.0 | −5.8 / −1.6 | 80.7% |
| Jack Ransome | 49.1% | −6.3 | −7.8 / −4.0 | 81.4% |
| Pearl Hat | 46.3% | **−9.2** | −10.1 / −8.3 | 87.0% |

Uplift signs almost never flip across checkpoints — the tier list is stable.

Two clear outliers on each end: **Soundance Kid and Tom Thorn are the format's best characters** (Soundance also survives most often, 63.6% elimination vs a ~79% field average), while **Pearl Hat is the worst by a wide margin** — a nearly 19-point gap within the same side. Elimination rate tracks win rate closely, so power here is mostly "does this character stay alive."

## 3. Pairs are near-additive — this is a power meta, not a combo meta

| Pair | Win% | Uplift | Sum of parts | Synergy |
|---|---|---|---|---|
| Soundance Kid + Tom Thorn | 72.2% | +16.7 | +18.0 | −1.3 |
| Sid Curry + Tom Thorn | 70.6% | +15.1 | +12.5 | +2.6 |
| Sid Curry + Soundance Kid | 69.9% | +14.5 | +13.1 | +1.4 |
| Dalon Ranger + Wild Bill | 58.9% | +14.4 | +11.6 | +2.8 |
| Dalon Ranger + Wyatt Ear | 58.2% | +13.7 | +13.3 | +0.4 |
| Alan Pinkertoon + Dalon Ranger | 57.0% | +12.5 | +11.1 | +1.4 |
| Bart Masterson + The Stranger | 31.5% | −13.0 | −10.0 | −3.0 |
| Bart Masterson + Buffalo Bell | 31.5% | −13.0 | −11.7 | −1.3 |
| Jack Ransome + Pearl Hat | 38.0% | **−17.5** | −15.5 | −2.0 |

Synergies exist but are small (±1–3 points on top of the individual uplifts). There is a mild rich-get-richer effect at both tails — strong pairs slightly exceed the sum, weak pairs slightly undershoot — but no pair is greater than the sum of its parts in the way a real combo would be. **Draft value is dominated by raw character strength.**

## 4. Matchups are brutally polarized

Across the 144-cell law × outlaw grid, law win rate ranges from 27.8% to 60.8% — a 33-point spread.

| Law-favoured | Law win% |
|---|---|
| Wyatt Ear vs Pearl Hat | 60.8% |
| Dalon Ranger vs Pearl Hat | 60.4% |
| Alan Pinkertoon vs Pearl Hat | 59.3% |
| Wyatt Ear vs Jack Ransome | 59.0% |
| Bill Tightman vs Pearl Hat | 57.7% |
| Wild Bill vs Pearl Hat | 57.3% |

| Outlaw-crushed | Law win% |
|---|---|
| Bart Masterson vs Soundance Kid | 27.8% |
| The Stranger vs Soundance Kid | 28.3% |
| Buffalo Bell vs Tom Thorn | 28.5% |
| Tex Killer vs Soundance Kid | 29.6% |
| Annie Oakey vs Soundance Kid | 30.2% |
| Buffalo Bell vs Soundance Kid | 30.6% |

**Every one of the top-6 law matchups is against Pearl Hat or Jack Ransome**, and 5 of the 6 worst are against Soundance Kid. The whole law-side matchup table is essentially "did you dodge Soundance Kid."

## 5. Playstyle: defensive attrition and card denial

| Behaviour | Rate |
|---|---|
| Defends when able | **88.4%** |
| Attacks when able | 45.1% |
| Uses character ability | 65.6% |
| Swap / exchange | 19.4% |
| Targets non-adjacent (rear) | **9.2%** |
| Share of plays that are equipment | 20.0% |
| Cards played per turn | 2.55 |
| Top-action confidence | 81.0% |

### Card pick rates

How often a card was chosen when it was a legal option, pooled over the last 100 iterations.

| Card | Pick rate | Times offered | Times used |
|---|---|---|---|
| stealing | **61.6%** | 36,805 | 91,318 |
| wagon | 56.4% | 30,483 | 68,690 |
| barrel | 55.3% | 62,198 | 136,869 |
| rattlesnake | 52.4% | 16,186 | 33,965 |
| missed | 45.7% | 130,416 | 238,844 |
| duel | 43.7% | 26,417 | 46,207 |
| indians | 42.6% | 26,623 | 46,410 |
| peacemaker | 41.7% | 27,850 | 46,432 |
| remington | 41.4% | 27,486 | 45,066 |
| return_fire | 40.3% | 13,556 | 21,502 |
| schofield | 40.3% | 28,144 | 44,910 |
| bank | 40.3% | 19,798 | 31,754 |
| hat | 38.9% | 47,472 | 73,867 |
| gun_belt | 31.6% | 39,080 | 49,868 |
| clock | 31.4% | 43,467 | 54,361 |
| schoolmarm | 31.2% | 35,454 | 44,146 |
| colt | 31.0% | 104,616 | 129,659 |
| knife | 30.2% | 55,483 | 66,831 |
| winchester | 29.8% | 37,404 | 44,536 |
| shotgun | 29.7% | 74,393 | 87,693 |
| gatling | 29.5% | 19,656 | 23,627 |
| general_store | 29.2% | 35,636 | 41,992 |
| beer | 29.0% | 19,201 | 22,340 |
| derringer | 27.2% | 83,572 | 90,778 |
| carbine | 27.2% | 60,446 | 65,641 |
| scope | 24.6% | 47,853 | 46,908 |
| water | 23.7% | 112,111 | 105,926 |
| stampede | 21.1% | 24,565 | 20,856 |
| fanning | 19.3% | 53,612 | 42,428 |
| strong_ale | 16.5% | 31,772 | 21,280 |
| dynamite | **14.3%** | 32,763 | 18,918 |

The policy converged on a **control/attrition game**: strip the opponent's resources (stealing is the single most-picked card), draw ahead (wagon), and don't die (barrel, missed). It systematically avoids high-variance and self-damaging cards — dynamite is dead last at 14.3% despite being offered 33k times. Among guns it prefers the mid-range peacemaker/remington (~41%) over colt (31%), derringer and carbine (27%): reach matters less than tempo, consistent with the 9.2% rear-targeting rate — it plays almost exclusively against adjacent seats.

---

## Caveats

- These are **greedy/argmax** evaluations. Deterministic play tends to exaggerate imbalances relative to stochastic play, so treat the magnitudes as an upper bound.
- All five runs share seed `4470001`, so the ~10k games rest on ~2048 distinct deals. The stability cited above is stability *across checkpoints*, not across fresh randomness. A fresh-seed evaluation would be the honest confirmation.
- This is the **policy's** meta, and that policy had been plateaued for ~400 iterations at the time of measurement (win rate vs frozen self 49.8%, vs history pool 51.4%). A converged-but-stuck self-play agent can have shared blind spots, and character rankings partly reflect what this particular strategy profile exploits. "Pearl Hat is unplayable" and "dynamite is worthless" are claims about *this* equilibrium, not proofs about the game.
