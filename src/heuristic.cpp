#include "bang/heuristic.hpp"

#include <limits>
#include <stdexcept>
#include <vector>

namespace bang {
namespace {

int Life(const GameState& state, int player, int position) {
  return state.Players()[static_cast<std::size_t>(player)]
      .positions[static_cast<std::size_t>(position)]
      .life;
}

int ScoreAction(const GameState& state, int action_id) {
  const ActionKey action = DecodeAction(action_id);
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
