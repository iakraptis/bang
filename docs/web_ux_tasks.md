# Web table UX improvements — task tracker

Selected from the UX brainstorming list (items 1, 2, 3, 9, 10, 11, 12, 16).
All work is frontend-only (`web/app.js`, `web/styles.css`); the engine and the
session API stay untouched.

- [x] **1. Auto-pass dead phases** — when the human's only legal action is
  `end_turn` (or a lone "Skip the effect"), apply it automatically after a
  short, visible countdown. Any pointer interaction cancels the countdown.
- [x] **2. Pulsing end-turn CTA** — when no card or character actions remain
  (only dock decisions are left), the end-turn button pulses to signal
  "you're done here".
- [x] **3. Proactive target highlighting on hover** — hovering a playable
  card softly outlines its legal drop zones before the player even selects
  or drags it. Selected-card marking keeps the stronger style.
- [x] **9. Sound design** — synthesized WebAudio effects (no assets):
  gunshot, hit thud, miss whoosh, heal chime, draw swoosh, reveal ticks,
  dynamite boom, elimination dirge, turn chime, victory/defeat stings.
  Mute toggle in the topbar, persisted in localStorage.
- [x] **10. BANG! / MISSED! callout banners** — big western display text
  flashed over the felt when a BANG!-kind card is played or a hit is
  answered with an avoid card. Detected from new log entries
  (`play(...)` / `avoid(...)` engine text), not from guesswork.
- [x] **11. Death ceremony** — when a character's life hits zero: the card
  falls (rotate/sink/grayscale), a skull pops over the slot, the table
  shakes, and the death sound plays. Board removals get the skull pop too.
- [x] **12. Match stats on the winner banner** — turns taken, damage dealt,
  damage taken, cards played, shots avoided; accumulated client-side from
  the per-state diffs (life deltas + log entries), reset per session.
- [x] **16. "Why can't I play this?"** — dimmed hand cards (existing
  playability hint) get a tooltip line explaining why they can't be played
  right now, phase-aware: waiting-for-response phases vs. no legal
  target / spent BANG! limit during the play phase.
- [x] **17. "What am I deciding?"** — every decision carries a plain-English
  hint under the phase pill (`phaseHint`). The ability phase, which used to
  read "Resolve a character ability" for a dozen different decisions, now
  names the character, explains the situation, spells out each option on its
  own button, and marks the asking character on the table. Server-side, so
  the wording follows the engine's own view of the pending ability.

## Second pass — reach

Aimed at a player who arrives on a phone, has never played BANG!, and will
leave inside ten seconds if the game asks them for anything before it shows
them something. The rules are untouched; every item below is presentation.

- [x] **18. Portrait phone layout** — the old `max-width: 900px` block only
  turned scrolling on and unstacked the grids, which is a desktop board
  spilling down a phone. The narrow layout is now designed: four fixed bands
  (opponent strip · battlefield · your characters and the dock · the hand),
  no page scroll, and the hand in the bottom third where a thumb reaches.
  The hand is a snap-scrolling rail rather than a fan — a fan needs a cursor
  to pull one card out of an overlap — and it keeps its scroll position
  across the board's full re-render. Dock buttons are 48px. The dock drops to
  its own row so a spelled-out yes/no fits, and collapses when empty.
- [x] **19. Touch is not a small cursor** — drag-to-play is desktop-only
  (`pointer: coarse`), because on the hand rail the same gesture is a scroll;
  touch plays tap-to-select then tap-the-target, and a tap that travelled
  more than 12px is treated as a scroll rather than a click. Hover tooltips
  have no equivalent on a phone, so a long press opens a bottom sheet with
  the same `data-tip` payload — art, name, rules text.
- [x] **20. One button in** — the first screen was a form with a roster
  dropdown and a numeric seed. It is now a splash with `Play`, which deals
  instantly using the last setup you used; side, roster and seed live behind
  "Custom duel". "Ride again" on the winner banner re-deals rather than
  re-opening the form.
- [x] **21. Guided first duel** — for one duel, a second line under the
  phase text says what to physically do, read off the same legal actions the
  board is already highlighting (never a script, so it cannot contradict the
  game). Retires itself after two turns, or on "Got it", persisted.
- [x] **22. Combo and takedown callouts** — several points of damage landing
  on one card, or a character going down, gets a display-type stamp sized to
  what happened. Runs of scoring turns are called out on milestones only, so
  the reward does not become wallpaper.
- [x] **23. The wanted poster** — a finished duel draws a 1080×1350 poster on
  a canvas: verdict, the tally, and the last few lines of the trail. Handed
  to the system share sheet where there is one, saved as a PNG where there is
  not. Drawn rather than screenshotted — nothing in a browser can photograph
  a live WebGL canvas inside a DOM layout.
- [x] **24. The battlefield gets the room** — the phase text moved out of the
  felt into its own track above it, so it stops standing in front of the
  duel; the cards around the edge gave up some height; and the felt claims
  up to 34vh. The camera is now a rig rather than a fixed transform: idle
  drift, a lean toward whatever card is resolving, an impact kick, and a
  kill-cam that dollies in and holds while a character goes down (the 2D fall
  was slowed to match, and the replacement now waits for the whole fall).
  Figures shift their weight, the active seats stand in a ring of light, and
  there is dust in the air. Framing is fitted per corner against a per-shot
  box, which is what lets a phone use a completely different shot — down the
  line of the duel from your own end, wide lens — instead of the letterbox
  strip a desktop board leaves.
