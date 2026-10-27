#include "bang/heuristic.hpp"

#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace bang {
namespace {

int Life(const GameState& state, int player, int position) {
  return state.Players()[static_cast<std::size_t>(player)]
      .positions[static_cast<std::size_t>(position)]
      .life;
}

/* ---------------- Renegades ----------------
   Everything below scores only Renegade cards, characters and phases, so the
   heuristic plays the base game exactly as before. */

// Swapping this side helps its owner when the rearguard is healthier.
bool OwnSwapHelps(const GameState& state, int player) {
  const int active = state.Players()[static_cast<std::size_t>(player)].active_position;
  return Life(state, player, 1 - active) > Life(state, player, active);
}

// Swapping the opponent helps us when it brings their weaker character to the
// front, where hits can finish it.
bool OpponentSwapHelps(const GameState& state, int opponent) {
  const int active = state.Players()[static_cast<std::size_t>(opponent)].active_position;
  const int rear_life = Life(state, opponent, 1 - active);
  return rear_life > 0 && rear_life < Life(state, opponent, active);
}

bool SwapHelps(const GameState& state, int player, int side) {
  return side == player ? OwnSwapHelps(state, player) : OpponentSwapHelps(state, side);
}

// Which side's active character holds this card in play, or -1.
int ActiveEquipmentOwner(const GameState& state, int card_instance) {
  for (int side = 0; side < kSideCount; ++side) {
    const auto& player = state.Players()[static_cast<std::size_t>(side)];
    const auto& equipment =
        player.positions[static_cast<std::size_t>(player.active_position)].equipment;
    for (const int instance : equipment) {
      if (instance == card_instance) return side;
    }
  }
  return -1;
}

// Cards that hurt the character carrying them.
bool HarmfulEquipment(CardName name) {
  return name == CardName::kDynamite || name == CardName::kRattlesnake;
}

int HitScore(const GameState& state, int opponent, Target target) {
  const int position =
      target == Target::kOpponentActive
          ? state.Players()[static_cast<std::size_t>(opponent)].active_position
          : 1 - state.Players()[static_cast<std::size_t>(opponent)].active_position;
  return 210 + (Life(state, opponent, position) <= 1 ? 160 : 0);
}

// A score for a decision only the Renegades create, or nullopt to fall back
// to the base heuristic.
std::optional<int> ScoreRenegadeAction(const GameState& state, const ActionKey& action) {
  const int player = state.CurrentPlayer();
  const int opponent = 1 - player;
  const Phase phase = state.CurrentPhase();
  const int ability = phase == Phase::kChooseAbility ? state.PendingAbilityCharacter() : -1;

  if (action.kind == ActionKind::kUseResource) {
    const CardName name = GetCardInstance(action.card_instance).name;
    if (name == CardName::kGoldPan) return 150;      // the top discard
    if (name == CardName::kIndianChief) return 220;  // Indians! on both characters
    if (name == CardName::kSlap) return 160;
    return HitScore(state, opponent, static_cast<Target>(action.target));
  }

  if (phase == Phase::kChooseSwitch) {
    if (action.kind == ActionKind::kChooseSwap) {
      const int side = action.card_instance == 1 ? opponent : player;
      return SwapHelps(state, player, side) ? 60 : -10;
    }
    if (action.kind == ActionKind::kResolveOptional) {
      if (action.card_instance == 0) return 0;
      const int side = state.SwitchSecondPlayer();
      return side >= 0 && SwapHelps(state, player, side) ? 60 : -10;
    }
  }
  if (phase == Phase::kChooseDutchHarry && action.kind == ActionKind::kResolveOptional) {
    if (action.card_instance == 0) return 0;  // discard it, keep the next
    const auto& looked = GetCard(GetCardInstance(state.ChoiceCards().front()).name);
    const bool useful = looked.has_hit || looked.has_avoid ||
                        looked.kind == CardKind::kEquipment ||
                        looked.kind == CardKind::kResource;
    return useful ? 60 : -10;
  }
  if ((phase == Phase::kChooseAmbush || phase == Phase::kChooseSlap ||
       ability == static_cast<int>(CharacterName::kPokerAlicia)) &&
      action.kind == ActionKind::kSelectCard) {
    // Strip the opponent; drop our own card only when it hurts us.
    const int owner = ActiveEquipmentOwner(state, action.card_instance);
    if (owner == opponent) return 90;
    return HarmfulEquipment(GetCardInstance(action.card_instance).name) ? 50 : -40;
  }
  if (ability == static_cast<int>(CharacterName::kPokerAlicia) &&
      action.kind == ActionKind::kResolveOptional) {
    return 50;  // drawing as usual beats taking a card we would rather keep
  }
  if (ability == static_cast<int>(CharacterName::kJimLonghair) &&
      action.kind == ActionKind::kResolveOptional) {
    return action.card_instance == 1 ? (OpponentSwapHelps(state, opponent) ? 60 : -10) : 0;
  }
  if (ability == static_cast<int>(CharacterName::kKittyLeroy) &&
      action.kind == ActionKind::kDiscardCard) {
    // Pay for a repeat with a card we can spare.
    return GetCard(GetCardInstance(action.card_instance).name).has_avoid ? -30 : 30;
  }
  if (phase == Phase::kDuelResponse && state.RicochetActive() &&
      action.kind == ActionKind::kDiscardCard) {
    return 60;  // keep the Ricochet going rather than lose life
  }

  if (action.kind != ActionKind::kPlayCard) return std::nullopt;
  const CardName name = GetCardInstance(action.card_instance).name;
  const auto& card = GetCard(name);
  const bool at_opponent = action.target == static_cast<int>(Target::kOpponentActive) ||
                           action.target == static_cast<int>(Target::kOpponentRearguard);
  if (card.kind == CardKind::kResource ||
      name == CardName::kSidekick || name == CardName::kSpurs) {
    // These help whoever carries them: never hand them to the opponent.
    return at_opponent ? -200 : 130;
  }
  if (card.kind == CardKind::kEquipment && at_opponent && !HarmfulEquipment(name)) {
    // The base scoring would gift a Barrel or a Scope to the opponent; with
    // the Renegades in play both sides should play their Equipment sensibly,
    // or the comparison between the decks is skewed.
    return -200;
  }
  const auto& own = state.Players()[static_cast<std::size_t>(player)];
  switch (name) {
    case CardName::kSwitch:
      return OwnSwapHelps(state, player) || OpponentSwapHelps(state, opponent) ? 100 : -150;
    case CardName::kUnionPacific:
      return 130;
    case CardName::kWhiskey: {
      const auto& active = own.positions[static_cast<std::size_t>(own.active_position)];
      return active.life < GetCharacter(active.character_id).max_life ? 180 : -120;
    }
    case CardName::kRicochet: {
      // Worth starting only with an avoid card to answer the first return.
      int avoids = 0;
      for (const int instance : own.hand) {
        if (instance != action.card_instance &&
            GetCard(GetCardInstance(instance).name).has_avoid) {
          ++avoids;
        }
      }
      return avoids > 0 ? 100 : -150;
    }
    case CardName::kAmbush: {
      const auto& foe = state.Players()[static_cast<std::size_t>(opponent)];
      return foe.positions[static_cast<std::size_t>(foe.active_position)].equipment.empty() ? -150
                                                                                          : 100;
    }
    default:
      return std::nullopt;
  }
}

int ScoreAction(const GameState& state, int action_id) {
  const ActionKey action = DecodeAction(action_id);
  if (state.Mode() != GameMode::kBase) {
    if (const auto score = ScoreRenegadeAction(state, action)) return *score;
  }
  const int player = state.CurrentPlayer();
  const int opponent = 1 - player;
  switch (action.kind) {
    case ActionKind::kTakeHit:
      return -500 -
          100 * (Life(
                     state, player,
                     state.Players()[static_cast<std::size_t>(player)]
                         .active_position) <= 1);
    case ActionKind::kAvoidHit: {
      const auto& card = GetCardInstance(action.card_instance);
      if (card.name == CardName::kBarrel) return 460;
      if (card.name == CardName::kHat) return 450;
      if (card.name == CardName::kReturnFire) return 440;
      return 430;
    }
    case ActionKind::kChooseActive: {
      const auto& character =
          state.Players()[static_cast<std::size_t>(player)]
              .positions[static_cast<std::size_t>(action.card_instance)];
      return character.life * 20 +
          (GetCharacter(character.character_id).always_active ? 8 : 0);
    }
    case ActionKind::kResolveOptional:
      return action.card_instance == 1 ? 80 : 0;
    case ActionKind::kChooseReveal:
    case ActionKind::kChooseSwap:
    case ActionKind::kChooseCardBack:
    case ActionKind::kSelectCard:
      return 20;
    case ActionKind::kSwap: {
      const auto& own = state.Players()[static_cast<std::size_t>(player)];
      const int active = own.active_position;
      return Life(state, player, 1 - active) >
              Life(state, player, active)
          ? 90
          : 5;
    }
    case ActionKind::kEndPlay:
      return -100;
    case ActionKind::kReloadCard:
      return 15;
    case ActionKind::kDiscardCard:
      return GetCard(GetCardInstance(action.card_instance).name).has_avoid
          ? -30
          : 0;
    case ActionKind::kUseAbility:
      return 110;
    case ActionKind::kPlayAsColt:
      return 210;
    case ActionKind::kPlayCard:
    case ActionKind::kUseResource:
      break;
  }

  const auto& instance = GetCardInstance(action.card_instance);
  const auto& card = GetCard(instance.name);
  int score = 40;
  if (card.has_hit) score += 170;
  if (card.kind == CardKind::kEquipment) score += 80;
  if (card.kind == CardKind::kBang) score += 50;
  if (action.target == static_cast<int>(Target::kOpponentActive) ||
      action.target == static_cast<int>(Target::kOpponentRearguard)) {
    score += 20;
    const int position =
        action.target == static_cast<int>(Target::kOpponentActive)
            ? state.Players()[static_cast<std::size_t>(opponent)]
                  .active_position
            : 1 -
                  state.Players()[static_cast<std::size_t>(opponent)]
                      .active_position;
    if (Life(state, opponent, position) <= 1 && card.has_hit) score += 160;
  }
  if (instance.name == CardName::kBeer ||
      instance.name == CardName::kStrongAle) {
    const auto& own = state.Players()[static_cast<std::size_t>(player)];
    const auto& active =
        own.positions[static_cast<std::size_t>(own.active_position)];
    score += active.life < GetCharacter(active.character_id).max_life
        ? 140
        : -120;
  }
  if (instance.name == CardName::kGeneralStore ||
      instance.name == CardName::kWagon) {
    score += 70;
  }
  return score;
}

}  // namespace

int ChooseHeuristicAction(const GameState& state, FastRng& rng) {
  const auto legal = state.LegalActions();
  if (legal.empty()) {
    throw std::logic_error("heuristic cannot act without legal actions");
  }
  int best_score = std::numeric_limits<int>::min();
  std::vector<int> best;
  for (const int action : legal) {
    const int score = ScoreAction(state, action);
    if (score > best_score) {
      best_score = score;
      best = {action};
    } else if (score == best_score) {
      best.push_back(action);
    }
  }
  return best[rng.USize(best.size())];
}

}  // namespace bang
