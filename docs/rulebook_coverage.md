# Base rulebook coverage

Authoritative rules reference: `Bang_TheDuel_Rules_ENG.pdf` (16 pages).

This is a live correctness checklist. **Partial does not mean rules-complete.**

| Rule area | Status | Evidence / remaining work |
|---|---|---|
| 40-card Law and Outlaw deck compositions | Implemented | All 80 physical instances and the rulebook's 31 card names are catalogued; native tests verify key copy counts and 40 cards per side. |
| Reveal symbols | Implemented | Every physical instance retains its printed symbol; aggregate 20 Barrel / 20 Snake / 10 Dynamite / 30 Horseshoe distribution is tested. Barrel, Dynamite, Rattlesnake, and Sid Curry resolve through explicit continuations, including Bart Masterson's two-card choice. |
| 24 characters and starting life | Implemented data | Both 12-character sides, printed life values, and always-active marker are catalogued. |
| Select 4 characters and form reserve | Implemented | Configurable from 2–12 for game length; default is 4. Selected groups are shuffled and two enter play. |
| Choose AC, reveal AC/RC, starting hands | Implemented | Both choices are explicit decisions; opponent candidates remain hidden until both commit. Law receives 4, Outlaw 5. |
| Law begins; draw 2 each turn | Implemented | First turn and later turns use the same automatic draw path. |
| Personal decks, shared discard, common deck | Implemented | Transition and repeated reshuffle are covered by conservation tests. |
| Voluntary AC/RC swap once per play phase | Implemented | Legal only with two living in-play characters; a second voluntary swap is rejected. Water, Shotgun, and Stampede swaps do not consume the voluntary swap. Moves trigger Tom Thorn, Jango, and an opposing active Clock in current-player AC order. |
| End-turn hand limit and 1-life exception | Implemented | Mandatory card-by-card discard; limit is active life, or 2 when active life is 1. |
| Character elimination and reserve replacement | Implemented | Life loss, same-position replacement, the normal 2-card reward, single-survivor handling, and terminal victory work. Multi-target cards defer cleanup until all required targets resolve. Bull Anderson chooses up to two Equipment/random hand cards before remaining Equipment is discarded; Bill Tightman then receives a separate optional draw. Wild Bill counters can interrupt Bull safely, including a counter that causes a nested elimination. |
| BANG! limit | Implemented | The normal limit, active Gun Belt semantics across swaps, The Stranger's always-active second BANG!, West Harding's unlimited BANG! cards, and Annie/Toco transformed Colts work. |
| Hit / avoid response | Implemented | Missed, Hat, Barrel and Sid Curry reveals, Bart's two-card reveal choice, chained Return Fire, Annie Oakey's any-hit-card avoid conversion, Tex Killer's avoid-plus-inert-additional-card rule, and Wyatt Ear's optional response to every printed hit are explicit decisions. Remington and Wyatt remain separate choices, while Fanning defers every Wyatt reaction until all hits finish. |
| Reload | Implemented | Reloadable hand cards and reloadable Equipment on AC are supported once either roster is down to one; RC Equipment is excluded. |
| Equipment placement and activity | Implemented for base cards | Relative target restrictions, persistent ownership, duplicate replacement, AC/RC activation, targeted-RC wakeup, transfer, and elimination cleanup work. Bank, Barrel, Clock, Dynamite, Gun Belt, Hat, Rattlesnake, Scope, and Stampede all have their base effects. Character-specific interactions remain. |
| Beginning-of-turn effects | Implemented for base cards | Dynamite, Rattlesnake, and Stampede resolve in the mandated order before Bank and the normal draw. The sequence pauses safely for Bart Masterson's reveal choice and stops on terminal damage. |
| Action and BANG! card effects | Implemented for base cards | All eight BANG! cards and all fourteen Action cards work. Duel alternates hit-card discards; General Store drafts four public cards; Wagon keeps its choice private; Schoolmarm and Stealing enforce the opponent-RC restriction; Stealing exposes card backs but randomizes the selected face. Character-specific modifications remain. |
| Character abilities | Implemented and audited | All 24 character abilities have focused native coverage. The audit includes marked RC abilities, Annie/Toco hit payments in Duel and Indians!, Scope with non-BANG hits, Slim Poet's visible-back choice, and Wild Bill reactions to Schoolmarm, Stealing, Peacemaker, Carbine, Slim Poet, Bull Anderson, and duplicate replacement. Activated usage, damage distribution, elimination choices, multi-card defense progress, nested reactions, and triggered continuations are policy-visible. |
| Simultaneous effects | Implemented | Fanning, Gatling, and Indians! resolve explicit responses in AC/RC order, defer elimination/rewards, and preserve triggered-effect ordering. Alan, Dalon, Slim, Wyatt, Sid, and Return Fire consequences wait until the multi-target card completes in character order; Tex modifies every Fanning hit independently; simultaneous Tom Thorn/Clock/Jango effects follow current-player AC order. |
| Observation API | Foundation implemented | Fixed tensor masks opponent private identities, setup candidates, reserves, and Wagon/Soundance draws while encoding the observer's known reserve group, public Equipment, the top discard, reveal reasons, pending/queued hits, response progress, character-trigger sources and continuations, public drafts, visible hand-back counts, persistently known public hand cards, and private hand data. |
| Web application and AI opponents | Implemented | The native server owns sessions, exposes player-scoped state and engine legal actions, masks setup/hand/reserve secrets, and advances either a strict-ABI trained League PPO checkpoint or the tactical heuristic. The responsive browser table supports complete games, side/roster/seed selection, card-target filtering, and an action log. |
| Training | Implemented foundation | The optional LibTorch build provides semantic legal-action features, privacy-tested training and privileged observation contracts, batched native League PPO, checkpoint/optimizer commit validation, PFSP historical opponents, paired common-seed evaluation, final champion selection, and trained browser inference. Policy strength still depends on training time and empirical evaluation. |

## Next correctness milestone

Preserve the rule and information-boundary suite while collecting long-run
training evidence. Any recurrent/history model must remain observer-scoped, and
all promoted checkpoints should be evaluated from both seats on held-out seeds.
