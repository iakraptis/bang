#include "bang/action_space.hpp"

#include <stdexcept>

namespace bang {
namespace {

int CardAction(int offset, int card_instance) {
  if (card_instance < 0 || card_instance >= kCardInstanceCount) {
    throw std::out_of_range("invalid card instance in action");
  }
  return offset + card_instance;
}

}  // namespace

int PlayCardAction(int card_instance, Target target) {
  if (card_instance < 0 || card_instance >= kCardInstanceCount) {
    throw std::out_of_range("invalid card instance in action");
  }
  const int target_index = static_cast<int>(target);
  if (target_index < 0 || target_index >= kPlayTargetCount) {
    throw std::out_of_range("invalid target in action");
  }
  return kPlayCardOffset + card_instance * kPlayTargetCount + target_index;
}
int ReloadCardAction(int card_instance) { return CardAction(kReloadCardOffset, card_instance); }
int DiscardCardAction(int card_instance) { return CardAction(kDiscardCardOffset, card_instance); }
int AvoidHitAction(int card_instance) { return CardAction(kAvoidHitOffset, card_instance); }
int SelectCardAction(int card_instance) { return CardAction(kSelectCardOffset, card_instance); }
int PlayAsColtAction(int card_instance, Target target) {
  if (card_instance < 0 || card_instance >= kCardInstanceCount) {
    throw std::out_of_range("invalid card instance in transformed action");
  }
  const int target_index = static_cast<int>(target);
  if (target_index < 0 || target_index >= kPlayTargetCount) {
    throw std::out_of_range("invalid target in transformed action");
  }
  return kPlayAsColtOffset + card_instance * kPlayTargetCount + target_index;
}
int UseResourceAction(int card_instance, Target target) {
  if (card_instance < 0 || card_instance >= kCardInstanceCount) {
    throw std::out_of_range("invalid card instance in resource action");
  }
  const int target_index = static_cast<int>(target);
  if (target_index < 0 || target_index >= kPlayTargetCount) {
    throw std::out_of_range("invalid target in resource action");
  }
  return kUseResourceOffset + card_instance * kPlayTargetCount + target_index;
}
int UseAbilityAction(CharacterName character) {
  const int index = static_cast<int>(character);
  if (index < 0 || index >= kCharacterCount) {
    throw std::out_of_range("invalid character in ability action");
  }
  return kUseAbilityOffset + index;
}

ActionKey DecodeAction(int action_id) {
  if (action_id < 0 || action_id >= kActionCount) {
    throw std::out_of_range("invalid action id");
  }
  if (action_id == kEndPlayAction) return {action_id, ActionKind::kEndPlay, -1};
  if (action_id == kSwapAction) return {action_id, ActionKind::kSwap, -1};
  if (action_id == kTakeHitAction) return {action_id, ActionKind::kTakeHit, -1};
  if (action_id == kChooseActiveLeftAction) {
    return {action_id, ActionKind::kChooseActive, 0, -1};
  }
  if (action_id == kChooseActiveRightAction) {
    return {action_id, ActionKind::kChooseActive, 1, -1};
  }
  if (action_id == kChooseRevealFirstAction) {
    return {action_id, ActionKind::kChooseReveal, 0, -1};
  }
  if (action_id == kChooseRevealSecondAction) {
    return {action_id, ActionKind::kChooseReveal, 1, -1};
  }
  if (action_id == kAcceptOptionalAction) {
    return {action_id, ActionKind::kResolveOptional, 1, -1};
  }
  if (action_id == kSkipOptionalAction) {
    return {action_id, ActionKind::kResolveOptional, 0, -1};
  }
  if (action_id == kChooseOwnSwapAction) {
    return {action_id, ActionKind::kChooseSwap, 0, -1};
  }
  if (action_id == kChooseOpponentSwapAction) {
    return {action_id, ActionKind::kChooseSwap, 1, -1};
  }
  if (action_id == kChooseLawBackAction) {
    return {action_id, ActionKind::kChooseCardBack, 0, -1};
  }
  if (action_id == kChooseOutlawBackAction) {
    return {action_id, ActionKind::kChooseCardBack, 1, -1};
  }
  if (action_id >= kUseResourceOffset) {
    const int encoded = action_id - kUseResourceOffset;
    return {action_id, ActionKind::kUseResource, encoded / kPlayTargetCount,
            encoded % kPlayTargetCount};
  }
  if (action_id >= kUseAbilityOffset) {
    return {action_id, ActionKind::kUseAbility, action_id - kUseAbilityOffset, -1};
  }
  if (action_id >= kPlayAsColtOffset) {
    const int encoded = action_id - kPlayAsColtOffset;
    return {action_id, ActionKind::kPlayAsColt, encoded / kPlayTargetCount,
            encoded % kPlayTargetCount};
  }
  if (action_id >= kSelectCardOffset) {
    return {action_id, ActionKind::kSelectCard, action_id - kSelectCardOffset, -1};
  }
  if (action_id < kReloadCardOffset) {
    const int encoded = action_id - kPlayCardOffset;
    return {action_id, ActionKind::kPlayCard, encoded / kPlayTargetCount,
            encoded % kPlayTargetCount};
  }
  if (action_id < kDiscardCardOffset) {
    return {action_id, ActionKind::kReloadCard, action_id - kReloadCardOffset};
  }
  if (action_id < kAvoidHitOffset) {
    return {action_id, ActionKind::kDiscardCard, action_id - kDiscardCardOffset};
  }
  return {action_id, ActionKind::kAvoidHit, action_id - kAvoidHitOffset};
}

ActionFeature EncodeAction(int action_id) {
  const ActionKey action = DecodeAction(action_id);
  ActionFeature result;
  result.values[kActionFeatureKindOffset +
                static_cast<std::size_t>(action.kind)] = 1.0F;

  const bool has_card =
      action.kind == ActionKind::kPlayCard ||
      action.kind == ActionKind::kReloadCard ||
      action.kind == ActionKind::kDiscardCard ||
      action.kind == ActionKind::kAvoidHit ||
      action.kind == ActionKind::kSelectCard ||
      action.kind == ActionKind::kPlayAsColt ||
      action.kind == ActionKind::kUseResource;
  if (has_card) {
    const auto& instance = GetCardInstance(action.card_instance);
    const auto& card = GetCard(instance.name);
    result.values[kActionFeatureCardNameOffset +
                  static_cast<std::size_t>(instance.name)] = 1.0F;
    result.values[kActionFeatureRevealOffset +
                  static_cast<std::size_t>(instance.reveal)] = 1.0F;
    result.values[kActionFeatureOriginOffset +
                  static_cast<std::size_t>(instance.origin)] = 1.0F;
    result.values[kActionFeatureCardKindOffset +
                  static_cast<std::size_t>(card.kind)] = 1.0F;
    result.values[kActionFeatureCardPropertyOffset] =
        card.has_hit ? 1.0F : 0.0F;
    result.values[kActionFeatureCardPropertyOffset + 1] =
        card.has_avoid ? 1.0F : 0.0F;
    result.values[kActionFeatureCardPropertyOffset + 2] =
        card.reloadable ? 1.0F : 0.0F;
    result.values[kActionFeatureCardPropertyOffset + 3] =
        card.always_active ? 1.0F : 0.0F;
  }
  if (action.target >= 0) {
    result.values[kActionFeatureTargetOffset +
                  static_cast<std::size_t>(action.target)] = 1.0F;
  }
  if (action.kind == ActionKind::kUseAbility) {
    result.values[kActionFeatureCharacterOffset +
                  static_cast<std::size_t>(action.card_instance)] = 1.0F;
  }
  if (!has_card && action.card_instance >= 0 && action.card_instance < 4) {
    result.values[kActionFeatureOptionOffset +
                  static_cast<std::size_t>(action.card_instance)] = 1.0F;
  }
  return result;
}

std::string ActionToString(int action_id) {
  const auto action = DecodeAction(action_id);
  switch (action.kind) {
    case ActionKind::kChooseActive:
      return action.card_instance == 0 ? "choose_active(left)" : "choose_active(right)";
    case ActionKind::kChooseReveal:
      return action.card_instance == 0 ? "choose_reveal(first)" : "choose_reveal(second)";
    case ActionKind::kResolveOptional:
      return action.card_instance == 0 ? "skip_optional" : "accept_optional";
    case ActionKind::kChooseSwap:
      return action.card_instance == 0 ? "choose_swap(own)" : "choose_swap(opponent)";
    case ActionKind::kChooseCardBack:
      return action.card_instance == 0 ? "choose_back(law)" : "choose_back(outlaw)";
    case ActionKind::kEndPlay: return "end_play";
    case ActionKind::kSwap: return "swap";
    case ActionKind::kTakeHit: return "take_hit";
    case ActionKind::kPlayCard:
      return "play(" + std::string(GetCard(GetCardInstance(action.card_instance).name).key) +
             "," + TargetName(static_cast<Target>(action.target)) + ")";
    case ActionKind::kReloadCard:
      return "reload(" + std::string(GetCard(GetCardInstance(action.card_instance).name).key) + ")";
    case ActionKind::kDiscardCard:
      return "discard(" + std::string(GetCard(GetCardInstance(action.card_instance).name).key) + ")";
    case ActionKind::kAvoidHit:
      return "avoid(" + std::string(GetCard(GetCardInstance(action.card_instance).name).key) + ")";
    case ActionKind::kSelectCard:
      return "select(" + std::string(GetCard(GetCardInstance(action.card_instance).name).key) + ")";
    case ActionKind::kPlayAsColt:
      return "play_as_colt(" +
             std::string(GetCard(GetCardInstance(action.card_instance).name).key) +
             "," + TargetName(static_cast<Target>(action.target)) + ")";
    case ActionKind::kUseAbility:
      return "use_ability(" +
             std::string(GetCharacter(action.card_instance).key) + ")";
    case ActionKind::kUseResource:
      return "use_resource(" +
             std::string(GetCard(GetCardInstance(action.card_instance).name).key) +
             "," + TargetName(static_cast<Target>(action.target)) + ")";
  }
  throw std::logic_error("unknown action kind");
}

std::string TargetName(Target target) {
  switch (target) {
    case Target::kOwnActive: return "own_active";
    case Target::kOwnRearguard: return "own_rearguard";
    case Target::kOpponentActive: return "opponent_active";
    case Target::kOpponentRearguard: return "opponent_rearguard";
  }
  throw std::out_of_range("invalid target");
}

}  // namespace bang
