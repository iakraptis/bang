# BANG! The Duel – Renegades: card transcription

Phase 1 of adding the Renegades expansion: every card as printed, before any
engine code. Sources, in order of authority:

1. **The printed cards**, from dV Giochi's official card list
   ([set 19](https://bang.dvgiochi.com/cardslist.php?id=19)); the faces are in
   `web/cards/` under the keys below.
2. **The Renegades rulebook**
   ([PDF](https://www.dvgiochi.com/giochi/bang-theduel-renegades/download/Bang_ItaEng_TheDuelRenegades_Rules-ENG.pdf)),
   for clarifications. Where the rulebook and a card disagree, the card wins
   (see [Discrepancies](#discrepancies)).

The set is 52 cards: **40 playing cards** (20 Renegade-Law, 20 Renegade-Outlaw)
and **12 characters** (6 + 6). Per-side totals below add up to exactly 20.

## Symbols

| Code | Meaning | Where printed |
|---|---|---|
| ⊛ hit | the sun symbol: deals a hit (BANG!-type) | bottom centre |
| ⊘ avoid | the hat symbol: avoids a hit | bottom centre |
| ⟲ reload | revolver cylinder: may be discarded to draw once a roster is down to one | bottom centre |
| ⇄ always active | double arrow: works on the rearguard too | bottom centre |
| b / s / h / d | reveal symbol Barrel / Snake / Horseshoe / Dynamite | top right |
| RL / RO | origin: Renegade-Law (white wheel badge) / Renegade-Outlaw (bandit badge) | top left |

Card types follow the border colour: **red** BANG!, **brown** Action, **blue**
Equipment, and the new **green Resource**.

## Playing cards

Copies are listed per origin with each copy's reveal symbol.

| Key | Card (Italian) | Type | Symbols | RL copies | RO copies | Printed text |
|---|---|---|---|---|---|---|
| `lightning` | Lightning (Lighting) | BANG! | ⊛ | — | 2 × d | You may play 1 additional BANG! card after this one. |
| `savage` | Savage | BANG! | ⊛ | — | 1 × s | Draw 1 card. |
| `sharps` | Sharps | BANG! | ⊛ | — | 2 × h | Target's ability and Equipment have no effect. |
| `spencer` | Spencer | BANG! | ⊛ | — | 2 × h | If it hits a character with 4+ life points, he loses 2 life points. |
| `walker` | Walker | BANG! | ⊛ | 2 × b | — | If ⊘, take this card back into your hand. |
| `whitney` | Whitney | BANG! | ⊛ | 1 × d | — | May only be ⊘ with a ⊛ card. |
| `carbine` | Carabina *(reprint)* | BANG! | ⊛ | 1 × h | — | If ⊘ you may discard 1 Equipment in play from any AC. |
| `ambush` | Agguato | Action | — | 1 × h | 1 × s | Discard 1 card in play from your AC and/or 1 card from any AC. |
| `agility` | Agilità | Action | ⊘ | 1 × s | 1 × s | Draw 1 card. |
| `feint` | Finta | Action | ⊘ | 1 × s | 1 × s | If this ⊘ a BANG! card, take it. |
| `ricochet` | Rimbalzo | Action | — | 1 × h | — | Opponent may discard a ⊘ card, then you, etc. The AC of the first who fails loses 1 life point. |
| `switch` | Scambio | Action | ⟲ | 2 × b | — | Swap up to two AC with their RC (move the tokens). You choose the order. |
| `union_pacific` | Union Pacific | Action | — | — | 1 × h | Draw a total of 2 cards from your and/or opponent's deck. |
| `whiskey` | Whiskey | Action | — | — | 1 × b | Reveal. Horseshoe = your AC regains 2 life points, else 1. |
| `knife` | Pugnale *(reprint)* | Action | ⊛ | — | 1 × h | *(no text)* |
| `missed` | Mancato! *(reprint)* | Action | ⊘ | 2 × s, 1 × d | — | *(no text)* |
| `sidekick` | Spalla | Equipment | ⟲ | — | 1 × s | Each time this character becomes AC, opponent's AC is the target of a ⊛. Any card from his hand counts as ⊘. |
| `spurs` | Speroni | Equipment | — | 1 × b | — | If you have no cards in hand, this character may not be the target of ⊛. |
| `buffalo_rifle` | Fucile da Caccia | Resource | ⊛ | 2 × h | — | You may target opponent's RC with this card. Draw 1 card. |
| `double_barrel` | Doppietta | Resource | ⊛ | — | 2 × h | If ⊘ it targets the other character too. |
| `gold_pan` | Setaccio | Resource | — | 1 × s | — | Draw the top card from the discard pile. |
| `howitzer` | Howitzer | Resource | ⊛ | 1 × d | — | If not ⊘ opponent's AC and RC each lose 1 life point. |
| `indian_chief` | Capo Indiano | Resource | — | — | 1 × b | For each of his characters, opponent chooses: discard 1 ⊛ card or −1 life point. |
| `lucky_penny` | Monetina | Resource | ⊘ | — | 1 × b | Draw 1 card. |
| `pardner` | Socio | Resource | ⊛ / ⊘ | 2 × h | — | *(no text: either symbol)* |
| `slap` | Manrovescio | Resource | — | — | 2 × b | Discard 1 card in play from any AC and/or 1 card from any player's hand. |

**Totals:** RL 20 (BANG! 4, Action 9, Equipment 1, Resource 6), RO 20 (BANG! 7,
Action 6, Equipment 1, Resource 6). 23 new card names plus reprints of Carbine,
Knife, and Missed!. Reprints keep their base rules and base `web/cards` faces;
only their origin badge differs.

## Characters

| Key | Character | Origin | Life | ⇄ | Printed ability |
|---|---|---|---:|:---:|---|
| `charlie_basset` | Charlie Basset | RL | 3 | ✓ | Once per turn, after you've used a Resource, draw 1 card. |
| `hick_addams` | Hick Addams | RL | 4 | | When another player makes you discard 1 card from your hand or from Hick, draw 1 card. |
| `hottie_delou` | Hottie Delou | RL | 4 | | Once per turn, you may use 1 of her Resources in the same turn it was put into play. |
| `mystic_maid` | Mystic Maid | RL | 3 | | Discard 1 of her Equipment cards from play (not ⇄) for a ⊘ or a COLT, then draw 1 card. |
| `poker_alicia` | Poker Alicia | RL | 4 | | Instead of drawing 2 cards from the deck at the start of your turn, you may draw 1 Equipment card from any AC. |
| `the_marshal` | The Marshal | RL | 5 | ✓ | When he enters play, draw a total of 3 cards from the decks of your choice. |
| `big_steve` | Big Steve | RO | 5 | ✓ | At the end of your turn, you may hold up to 6 cards in your hand. |
| `dutch_harry` | Dutch Harry | RO | 4 | ✓ | When you draw a card, you may look at the top card of the deck and keep it or discard it. |
| `el_chindi` | El Chindi | RO | 3 | | If you have any cards in your hand, he is immune to Action cards played by other players. |
| `jim_longhair` | Jim Longhair | RO | 4 | ✓ | Each time your AC and RC swap, you may swap opponent's AC and RC also. |
| `king_fishers` | King Fishers | RO | 4 | | Each time a character receives his first Resource for the turn, draw 1 card. |
| `kitty_leroy` | Kitty Leroy | RO | 3 | | Once per turn, if you use 1 of her Resources, you may discard 1 card from your hand to repeat its effect. |

## Rulebook clarifications

General rules for **Resources** (green):

- Placed like Equipment: on your own RC or the opponent's AC only.
- Cannot be used on the turn they are put into play (Hottie Delou excepted).
  To use one, discard it, then apply its effect. Any number may be in play and
  used per turn; the same-name restriction still applies.
- No effect while in hand.
- Cards and abilities that affect Equipment (e.g. Carbine) also affect
  Resources; those naming Resources (e.g. Kitty Leroy) do not affect blue cards.
- Resources with ⊛ (Buffalo Rifle, Howitzer, Double Barrel) may be discarded
  from play to answer Duel or Indians! even on the turn they were played: that
  is discarding, not using.

Per card:

- **Ambush / Slap**: resolve either half or both; Ambush may even take 2 cards
  from your own AC. Slap against Wild Bill doing both makes your AC the target
  of two ⊛.
- **Buffalo Rifle, Savage, Lucky Penny**: the card is drawn immediately, before
  the opponent reacts. Lucky Penny's card can answer later Fanning hits.
- **Double Barrel**: if avoided, the other character becomes a target; full
  cancellation needs one ⊘ for the AC and another for the RC.
- **Feint**: take the avoided red card into your hand, except Walker, which goes
  back to its owner.
- **Lightning**: the extra red card need not be played immediately.
- **Ricochet**: discarding is optional. Only ⊘ cards count (Hat allowed),
  including Resources already in play such as Lucky Penny; not Barrel or
  abilities like Annie Oakey's. Card side effects are ignored.
- **Sharps**: e.g. Spurs and the abilities of Alan Pinkertoon, Slim Poet,
  Dalon Ranger, Sid Curry have no effect.
- **Sidekick**: acts as an automatic Derringer at the opponent's AC. If that AC
  has both Barrel and Clock, check the Clock first.
- **Spencer**: one ⊘ still avoids it; below 4 life it deals 1.
- **Spurs**: still targetable by Indians!, Indian Chief, Duel, Ricochet, etc.
- **Union Pacific**: may require drawing from the common deck as usual.
- **Whitney**: Missed!, Barrel, Sid Curry's ability, etc. do not work.
- **Dutch Harry**: if the looked-at card is discarded, the next card must be
  kept; applies to every draw (Wagon, Union Pacific, phase 1).
- **El Chindi**: not immune to BANG! cards, Resources, or Annie Oakey's attacks;
  must react as RC to his own AC's Indians!/Gatling; immune to Water, the ⊛ of
  Return Fire, etc.; loses a Duel he played himself.
- **Hick Addams**: no draw for voluntarily discarding a Resource to react
  (e.g. Lucky Penny against Gatling).
- **Kitty Leroy**: repeating Gold Pan draws the top two discards from before
  this discard.
- **Mystic Maid**: may use a Resource put into play this turn; needs Equipment
  in play; the COLT counts as the turn's BANG!; usable against Indians!,
  Indian Chief, etc.
- **Poker Alicia**: replaces phase 1 (Draw).
- **Jim Longhair** (implementation): his swap of the opponent happens after
  the other swap triggers and fires that side's own triggers. During a
  Switch it counts as the Switch's swap of that side, so no side swaps twice.
- **Mystic Maid** (implementation): any of her Equipment, Resources included,
  answers a hit as ⊘ or ⊛ (not Sharps), a Duel or a Ricochet, or is played as
  the turn's Colt; she draws 1 card each time.
- **The Marshal**: in the starting pair, the 3 cards come on top of the starting
  hand; each draw may come from any deck, chosen after seeing the previous one.

## Modes in scope

- **Reinforcements** (first): Renegade-Law cards and characters are shuffled
  into the Law deck and roster, Renegade-Outlaw into the Outlaw ones. Otherwise
  the base rules.
- **The Third Group** (second): all 40 cards and 12 characters form the
  Renegades. Renegades vs. Law: Law starts with 4 cards and plays first,
  Renegades start with 5. Renegades vs. Outlaw: Outlaw starts with 5 and plays
  first, Renegades with 4.
- **Mexican Standoff** (3 players) is out of scope.

The base game remains the default and must play exactly as before.

## Discrepancies

1. **King Fishers**: the rulebook repeats Jim Longhair's text ("Each time your
   AC and RC swap…"). The printed card reads "Each time a character receives
   his first Resource for the turn, draw 1 card." The card is used.
2. **Feint (RO copy)** prints its English subtitle as "FINTA"; cosmetic.
3. **"Lighting"**: the official card list's spelling; the card says Lightning.
4. **Backhander / Handover**: the rulebook's Slap entry calls the card
   "Backhander", and El Chindi's note says he is immune to "Handover", a name
   no Renegades card carries. Read as Slap until confirmed.

## Implementation status

Engine support, in the order it lands. All 23 new cards are playable; the
Renegade characters are dealt in Reinforcements but have no abilities yet.

| Area | Status |
|---|---|
| Data: cards, copies, characters, Resource kind, game modes | Done |
| Resources: placement, one-turn wait, AC only, used by the character's owner | Done |
| `use_resource` move (play phase) and avoid Resources answering hits | Done |
| Gold Pan, Lucky Penny, Buffalo Rifle, Pardner | Done, with tests |
| BANG!: Lightning, Savage, Spencer, Walker, Whitney | Done, with tests |
| Avoid Actions: Agility, Feint | Done, with tests |
| Resources: Double Barrel, Howitzer, Indian Chief | Done, with tests |
| Resource: Slap (with Wild Bill reacting per removal) | Done, with tests |
| Actions: Ambush, Switch, Union Pacific, Whiskey | Done, with tests |
| Sharps; Equipment Sidekick, Spurs | Done, with tests |
| Ricochet; hit Resources answering Duel and Indians! | Done, with tests |
| Characters: Big Steve, Charlie Basset, Hottie Delou, King Fishers, The Marshal, Poker Alicia | Done, with tests |
| Characters: Jim Longhair, Mystic Maid | Done, with tests |
| Other characters (El Chindi, Hick Addams, Dutch Harry, Kitty Leroy) | To do |
| Renegades as a side (The Third Group) and format selection | Phase 4 |

## Rules decisions

Interpretations settled for this implementation:

- **Sidekick** triggers only when its character *becomes* AC, i.e. the AC
  token moves onto it. Placing it on a character that is already AC does not
  trigger it. A replacement from the reserve enters with no Equipment and does
  not move the token, so it never triggers there.
- **Spurs** blocks Sidekick's automatic ⊛ when the target's owner has no cards
  in hand.
- **Switch** swaps each side at most once: up to one swap on your side and one
  on the opponent's.
- **Pardner** used as ⊛ is not a BANG! card (it is green, not red) and does not
  count toward the BANG! limit.
- **King Fishers** (printed text confirmed against a physical copy; the
  rulebook entry is a copy of Jim Longhair's): whenever **any** character, on
  either side, receives its first Resource of the turn, however it arrived
  (including one the opponent places on that character), King Fishers' owner
  draws 1 card. This counts once per character per turn and works only while
  King Fishers is AC (the card has no ⇄).

- **Resources** work only on the AC, like blue Equipment: one on the RC
  cannot be used until its character becomes AC. A Resource is used by the
  owner of the character it sits on, so one placed on the opponent's AC is
  theirs. Avoid Resources (Lucky Penny, Pardner as ⊘) answer any hit on either
  player's turn. Hit Resources (Buffalo Rifle, Double Barrel, Howitzer, Pardner
  as ⊛) are used only in your own play phase and never count as BANG! cards.
  None may be used on the turn it was put into play.

No open rules questions remain for the two-player modes.
