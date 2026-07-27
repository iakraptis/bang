# Architecture

## Goals

The engine is designed around three consumers that must share one source of
rules truth:

1. native self-play workers that need cheap state copies and deterministic
   transitions;
2. policies that need stable action IDs and observations with no hidden
   information leakage;
3. a web application that needs human-readable actions and serialized
   public/player views.

The UI calls the engine; it does not reimplement rules in JavaScript.

## State and decisions

`GameState` is a deterministic state machine. Automatic rule steps such as the
normal two-card draw execute immediately. Every player choice is represented by
an action and appears in `LegalActions()`. Setup character selection, voluntary
swapping, ending the play phase, mandatory discards, reloads, and hit responses
already use this path.

Actions refer to one of the 80 physical card instances, not merely a card name.
Card-play actions also encode one of four relative targets (own/opponent and
active/rearguard). This preserves the printed reveal symbol and original card
back after the two decks mix while keeping target semantics independent of the
physical left/right position. The integer action space is bounded and stable,
which is useful for training checkpoints and replay logs.

Character abilities that reinterpret a hand card use a separate action range
rather than overloading the printed card action. For example, Toco Ramirez can
choose between Water's printed swap and playing that same physical Water card
as a Colt. This keeps replays unambiguous and lets a policy compare both legal
meanings of the card directly.

Once-per-turn character abilities use stable character-indexed actions and a
public usage vector. Abilities that require a follow-up selection, such as
Cattle Katie choosing Equipment, enter a normal decision phase.

## Card zones

Each side begins with its own 40-card deck. Both sides discard to one public
pile. The first exhausted personal deck shuffles that pile into the common
deck; the second exhausted player joins the already-existing common deck.
Subsequent common-deck exhaustion reshuffles the current discard pile.

Vectors use their back as the top of a pile. That makes draw and replacement
constant-time operations. Removing a selected hand card is linear in hand
size, which is both simpler and insignificant at this game's scale.

## Hidden information

Observations include both face-up characters and Equipment, public counts, the
observer's own hand and known reserve group, the public discard pile and its
top card, and the source and target of a pending hit. They do not include the
opponent's hand or reserve identities or any deck order. During simultaneous
setup, an opponent's two candidates remain masked until both players commit.

The complete `GameState` remains available to the environment for rule
resolution. Deployed policy code consumes `TrainingObservationTensor(observer)`,
which extends the public observation only with that observer's private and
legitimately known card identities. A separate privileged tensor is restricted
to an auxiliary training critic.

General Store and Wagon share fixed physical-card selection actions. General
Store's display is public to both observations; Wagon's three faces are visible
only to its owner. Hand card origins are public because the physical deck backs
remain visible after the decks mix. Stealing therefore asks the player to pick
a Law or Outlaw back and samples uniformly among matching cards. Bull Anderson
and Slim Poet use the same visible-back decision. Once Stealing or Bull moves a
known card between players, persistent public-hand tracking retains information
that both humans would know.

## Resolution direction

Complex cards use explicit pending decisions rather than callbacks or UI
prompts. The current continuation state can pause start-of-turn hazard reveals
or post-avoid effects such as Remington, Peacemaker, Carbine, and Shotgun, then
resume a queued Return Fire against the character active at that point. The
observation records both the current choice and any public queued hit so a
policy does not lose decision-relevant state. Multi-target cards use an ordered
resolution sequence matching page 9 of the rules; triggered character abilities
use the same continuation mechanism. Tom Thorn can therefore interrupt a normal,
Stampede, or Shotgun move with a complete hit/Return Fire chain before the
original effect resumes. When Clock triggers at the same time, its deferred draw
follows the rulebook's current-player AC ordering and is visible in the
observation. This keeps cloning straightforward and lets training workers pause
at every meaningful choice.

Damage continuations use the same representation. Alan Pinkertoon creates one
ordinary hit for every non-last life point lost, queues repeated hits without
recursion, defers them until a multi-target card finishes, and then resumes the
originating play phase or start-of-turn hazard sequence. The remaining queued
count is part of the observation. Deferred effects created by lethal damage are
owned by that elimination until Bull Anderson, Wild Bill interruptions, cleanup,
and Bill Tightman finish, preventing nested reactions from resuming the turn
early.

Optional damage reactions use the generic ability-choice phase. Dalon Ranger
and Slim Poet receive one accept/skip decision per qualifying life point; the
continuation and remaining count are encoded for the policy. During a
multi-target card, hits and optional reactions share one ordered deferred-effect
queue so consequences retain trigger order rather than being grouped by type.
Movement similarly queues Tom Thorn, Clock, and Jango effects in character
order and resumes Water, Stampede, Shotgun, or voluntary play exactly once.

Public hand knowledge is stored by physical card instance rather than inferred
from current zones. Pat Garret's shown second draw, General Store picks, and
Schoolmarm acquisitions remain visible to both policies until the card leaves
hand; private draws and Wagon/Soundance choices remain hidden. This prevents a
stateless policy observation from forgetting information a human player knows.

Sid Curry uses the standard reveal pipeline and records whether his attempt was
spent on the pending hit. A successful reveal records a character-sourced
counter rather than resolving it immediately, so Tex Killer can still require
the extra card and weapon effects or a multi-target card can finish in the
mandated order before the counter begins.

Fanning, Gatling, and Indians! now use that ordered sequence. Targets are fixed
when the card is played, every response is a normal action, and zero-life
characters remain in place until the entire card finishes. Elimination cleanup,
two-card rewards, replacements, and any queued retaliatory hits then run in the
rulebook order. The response mode and remaining sequence length are public
observation features, which lets a policy value scarce response cards without
inspecting engine-private continuation data.

## Web boundary

The native `web` command runs a small single-process HTTP server and serves the
static files in `web/`. Each in-memory session owns one `GameState`; API action
requests are accepted only when the engine reports the human as the current
decision player and the supplied stable action ID is legal. After each human
action, the server advances the other side until the next human decision.

Serialization is player-scoped rather than a dump of `GameState`. It exposes
the human hand and unordered known reserve group, public characters,
Equipment, discard history, visible choices, and persistently known opponent
cards. It masks opponent hands, opponent reserve identities, and face-down
setup candidates. The HTML/JavaScript layer only renders this view and submits
engine action IDs; it does not determine targets, costs, timing, or legality.

The server can load a strict-ABI LibTorch checkpoint selected by the training
campaign. It otherwise uses a seeded tactical legal-action heuristic. Policy
selection does not change the API or frontend, and required-checkpoint mode
fails closed rather than silently serving the heuristic.

## Training boundary

Training is an optional build feature, so the rules engine and default browser
binary keep no LibTorch dependency. The training build adds a structured
actor-critic, PPO optimizer, checkpoint ABI validation, paired evaluation, and
the Python league controller.

The actor receives a player-scoped training observation plus semantic features
for only the current legal actions. Equivalent physical copies therefore share
features, while the selected stable action ID remains unambiguous to the game.
Variable legal sets use a segmented softmax: this avoids a 997-way dense output,
invalid-action masking work, and padding every decision to the largest branch.

Rollouts advance hundreds of native games together and batch policy decisions.
Only learner decisions enter PPO; forced actions remain environment steps.
Historical opponents are immutable checkpoint snapshots, preventing the moving
target from changing inside an update. The training-only auxiliary critic sees
full card-zone masks to improve representation learning, but gradients from
that critic do not add privileged inputs to the actor.

Each frontier checkpoint has policy weights, ABI metadata, Adam state, and a
commit marker written last. Campaign state and reports use atomic replacement.
Final comparison plays both seats for each seed and publishes the complete
round-robin evidence alongside the chosen champion.
