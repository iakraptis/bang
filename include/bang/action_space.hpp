#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "bang/input_layout.hpp"
#include "bang/static_data.hpp"

namespace bang {

enum class ActionKind : std::uint8_t {
  kChooseActive,
  kChooseReveal,
  kResolveOptional,
  kChooseSwap,
  kChooseCardBack,
  kEndPlay,
  kSwap,
  kTakeHit,
  kPlayCard,
  kReloadCard,
  kDiscardCard,
  kAvoidHit,
  kSelectCard,
  kPlayAsColt,
  kUseAbility,
  // Discard a Resource from your active character for its effect (Renegades).
  kUseResource,
};

enum class Target : std::uint8_t {
  kOwnActive = 0,
  kOwnRearguard = 1,
  kOpponentActive = 2,
  kOpponentRearguard = 3,
};

struct ActionKey {
  int id = -1;
  ActionKind kind = ActionKind::kEndPlay;
  int card_instance = -1;
  int target = -1;
};

inline constexpr int kActionKindCount = 16;
inline constexpr int kCardKindCount = 4;
inline constexpr int kPhaseCapacity = 32;
// Barrel, Dynamite, Rattlesnake, Whiskey (Sid Curry has a slot of its own).
inline constexpr int kRevealReasonInputs = 4;
static_assert(static_cast<int>(ActionKind::kUseResource) + 1 == kActionKindCount);
static_assert(static_cast<int>(CardKind::kResource) + 1 == kCardKindCount);

// The counts every policy input is laid out from (see input_layout.hpp).
inline constexpr LayoutDims kLayoutDims{
    kCardNameCount, kCharacterCount, kCardInstanceCount, kSideCount,
    kPhaseCapacity, kCardKindCount, kActionKindCount, kRevealReasonInputs};

constexpr int ActionFeatureOffset(ActionFeatureBlock block) {
  return ActionFeatureStart(kLayoutDims, block);
}
inline constexpr int kActionFeatureKindOffset = ActionFeatureOffset(ActionFeatureBlock::kKind);
inline constexpr int kActionFeatureCardNameOffset = ActionFeatureOffset(ActionFeatureBlock::kCardName);
inline constexpr int kActionFeatureRevealOffset = ActionFeatureOffset(ActionFeatureBlock::kReveal);
inline constexpr int kActionFeatureOriginOffset = ActionFeatureOffset(ActionFeatureBlock::kOrigin);
inline constexpr int kActionFeatureCardKindOffset = ActionFeatureOffset(ActionFeatureBlock::kCardKind);
inline constexpr int kActionFeatureCardPropertyOffset = ActionFeatureOffset(ActionFeatureBlock::kCardProperty);
inline constexpr int kActionFeatureTargetOffset = ActionFeatureOffset(ActionFeatureBlock::kTarget);
inline constexpr int kActionFeatureCharacterOffset = ActionFeatureOffset(ActionFeatureBlock::kCharacter);
inline constexpr int kActionFeatureOptionOffset = ActionFeatureOffset(ActionFeatureBlock::kOption);
inline constexpr int kActionFeatureSize = BlockTotal(ActionFeatureBlockSizes(kLayoutDims));

struct ActionFeature {
  std::array<float, kActionFeatureSize> values{};
};

inline constexpr int kEndPlayAction = 0;
inline constexpr int kSwapAction = 1;
inline constexpr int kTakeHitAction = 2;
inline constexpr int kPlayCardOffset = 3;
inline constexpr int kPlayTargetCount = 4;
inline constexpr int kReloadCardOffset =
    kPlayCardOffset + kCardInstanceCount * kPlayTargetCount;
inline constexpr int kDiscardCardOffset = kReloadCardOffset + kCardInstanceCount;
inline constexpr int kAvoidHitOffset = kDiscardCardOffset + kCardInstanceCount;
inline constexpr int kChooseActiveLeftAction = kAvoidHitOffset + kCardInstanceCount;
inline constexpr int kChooseActiveRightAction = kChooseActiveLeftAction + 1;
inline constexpr int kChooseRevealFirstAction = kChooseActiveRightAction + 1;
inline constexpr int kChooseRevealSecondAction = kChooseRevealFirstAction + 1;
inline constexpr int kAcceptOptionalAction = kChooseRevealSecondAction + 1;
inline constexpr int kSkipOptionalAction = kAcceptOptionalAction + 1;
inline constexpr int kChooseOwnSwapAction = kSkipOptionalAction + 1;
inline constexpr int kChooseOpponentSwapAction = kChooseOwnSwapAction + 1;
inline constexpr int kSelectCardOffset = kChooseOpponentSwapAction + 1;
inline constexpr int kChooseLawBackAction = kSelectCardOffset + kCardInstanceCount;
inline constexpr int kChooseOutlawBackAction = kChooseLawBackAction + 1;
inline constexpr int kPlayAsColtOffset = kChooseOutlawBackAction + 1;
inline constexpr int kUseAbilityOffset =
    kPlayAsColtOffset + kCardInstanceCount * kPlayTargetCount;
inline constexpr int kUseResourceOffset = kUseAbilityOffset + kCharacterCount;
inline constexpr int kActionCount =
    kUseResourceOffset + kCardInstanceCount * kPlayTargetCount;

int PlayCardAction(int card_instance, Target target = Target::kOpponentActive);
int ReloadCardAction(int card_instance);
int DiscardCardAction(int card_instance);
int AvoidHitAction(int card_instance);
int UseResourceAction(int card_instance, Target target = Target::kOpponentActive);
int SelectCardAction(int card_instance);
int PlayAsColtAction(int card_instance,
                     Target target = Target::kOpponentActive);
int UseAbilityAction(CharacterName character);
ActionKey DecodeAction(int action_id);
ActionFeature EncodeAction(int action_id);
std::string ActionToString(int action_id);
std::string TargetName(Target target);

}  // namespace bang
