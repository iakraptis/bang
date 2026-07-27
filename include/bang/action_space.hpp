#pragma once

#include <array>
#include <cstdint>
#include <string>

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

inline constexpr int kActionKindCount = 15;
inline constexpr int kActionFeatureKindOffset = 0;
inline constexpr int kActionFeatureCardNameOffset =
    kActionFeatureKindOffset + kActionKindCount;
inline constexpr int kActionFeatureRevealOffset =
    kActionFeatureCardNameOffset + kCardNameCount;
inline constexpr int kActionFeatureOriginOffset =
    kActionFeatureRevealOffset + 4;
inline constexpr int kActionFeatureCardKindOffset =
    kActionFeatureOriginOffset + kSideCount;
inline constexpr int kActionFeatureCardPropertyOffset =
    kActionFeatureCardKindOffset + 3;
inline constexpr int kActionFeatureTargetOffset =
    kActionFeatureCardPropertyOffset + 4;
inline constexpr int kActionFeatureCharacterOffset =
    kActionFeatureTargetOffset + 4;
inline constexpr int kActionFeatureOptionOffset =
    kActionFeatureCharacterOffset + kCharacterCount;
inline constexpr int kActionFeatureSize = kActionFeatureOptionOffset + 4;

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
inline constexpr int kActionCount = kUseAbilityOffset + kCharacterCount;

int PlayCardAction(int card_instance, Target target = Target::kOpponentActive);
int ReloadCardAction(int card_instance);
int DiscardCardAction(int card_instance);
int AvoidHitAction(int card_instance);
int SelectCardAction(int card_instance);
int PlayAsColtAction(int card_instance,
                     Target target = Target::kOpponentActive);
int UseAbilityAction(CharacterName character);
ActionKey DecodeAction(int action_id);
ActionFeature EncodeAction(int action_id);
std::string ActionToString(int action_id);
std::string TargetName(Target target);

}  // namespace bang
