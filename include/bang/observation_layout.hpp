#pragma once

#include "bang/game.hpp"

namespace bang::obs_layout {

inline constexpr int kPhaseOffset = 0;
inline constexpr int kPhaseCapacity = 16;
inline constexpr int kDecisionPlayerOffset = 16;
inline constexpr int kTurnPlayerOffset = 17;
inline constexpr int kTurnNumberOffset = 18;
inline constexpr int kDiscardCountOffset = 19;
inline constexpr int kCommonDeckCountOffset = 20;
inline constexpr int kCommonDeckCreatedOffset = 21;
inline constexpr int kSwappedOffset = 22;
inline constexpr int kBangLimitUsedOffset = 23;
inline constexpr int kPendingHitOffset = 24;
inline constexpr int kPendingTargetObserverOffset = 25;
inline constexpr int kBarrelAttemptedOffset = 26;
inline constexpr int kPendingTargetPositionOffset = 27;
inline constexpr int kOwnActiveChosenOffset = 29;
inline constexpr int kSetupRevealedOffset = 30;
inline constexpr int kOptionalEffectPlayerOffset = 31;

inline constexpr int kPlayerBlocksOffset = 32;
inline constexpr int kPlayerBlockCount = 2;
inline constexpr int kPlayerBlockSize = 126;
inline constexpr int kActiveCharacterOffset = 0;
inline constexpr int kRearguardCharacterOffset = 24;
inline constexpr int kActiveLifeOffset = 48;
inline constexpr int kActiveMaxLifeOffset = 49;
inline constexpr int kRearguardLifeOffset = 50;
inline constexpr int kRearguardMaxLifeOffset = 51;
inline constexpr int kReserveCountOffset = 52;
inline constexpr int kHandCountOffset = 53;
inline constexpr int kPersonalDeckCountOffset = 54;
inline constexpr int kPersonalDeckExhaustedOffset = 55;
inline constexpr int kActiveEquipmentOffset = 56;
inline constexpr int kRearguardEquipmentOffset = 87;
inline constexpr int kActiveEquipmentRevealOffset = 118;
inline constexpr int kRearguardEquipmentRevealOffset = 122;

inline constexpr int kPrivateHandOffset = 284;
inline constexpr int kPrivateHandRevealOffset = 315;
inline constexpr int kDiscardCardsOffset = 319;
inline constexpr int kDiscardRevealOffset = 350;
inline constexpr int kFirstRevealedCardOffset = 354;
inline constexpr int kSecondRevealedCardOffset = 389;
inline constexpr int kRevealedCardSlotSize = 35;
inline constexpr int kPendingHitSourceOffset = 424;
inline constexpr int kPendingAttackerObserverOffset = 455;
inline constexpr int kRevealReasonOffset = 456;
inline constexpr int kRevealReasonCount = 3;
inline constexpr int kStartTurnPendingOffset = 459;
inline constexpr int kQueuedHitSourceOffset = 460;
inline constexpr int kQueuedHitTargetObserverOffset = 491;
inline constexpr int kQueuedHitAttackerObserverOffset = 492;
inline constexpr int kPendingResponseKindOffset = 493;
inline constexpr int kPendingResponseKindCount = 3;
inline constexpr int kHitSequenceActiveOffset = 496;
inline constexpr int kDeferredEliminationOffset = 497;
inline constexpr int kRemainingSequenceHitsOffset = 498;
inline constexpr int kDeferredHitCountOffset = 499;
inline constexpr int kChoiceCardNamesOffset = 500;
inline constexpr int kChoiceCardRevealsOffset = 531;
inline constexpr int kChoiceCardCountOffset = 535;
inline constexpr int kChoiceCardsPublicOffset = 536;
inline constexpr int kEffectActorObserverOffset = 537;
inline constexpr int kOwnLawBackCountOffset = 538;
inline constexpr int kOwnOutlawBackCountOffset = 539;
inline constexpr int kOpponentLawBackCountOffset = 540;
inline constexpr int kOpponentOutlawBackCountOffset = 541;
inline constexpr int kAbilityUsedOffset = 542;
inline constexpr int kPendingAbilityOffset = 566;
inline constexpr int kPendingAbilityStageOffset = 590;
inline constexpr int kPendingAvoidsRequiredOffset = 591;
inline constexpr int kPendingAvoidsPlayedOffset = 592;
inline constexpr int kPendingReturnFireOffset = 593;
inline constexpr int kPendingSourceCharacterOffset = 594;
inline constexpr int kPendingResumeKindOffset = 618;
inline constexpr int kResumeKindCount = 7;
inline constexpr int kMovementActiveOffset = 625;
inline constexpr int kContinuationHitCountOffset = 626;
inline constexpr int kMovementMoverObserverOffset = 627;
inline constexpr int kMovementTriggersOffset = 628;
inline constexpr int kMovementResumeKindOffset = 631;
inline constexpr int kDamageReactionResumeOffset = 634;
inline constexpr int kDamageReactionResumePlayerOffset = 641;
inline constexpr int kPublicHandCardsOffset = 642;
inline constexpr int kPublicHandBlockSize = 35;
inline constexpr int kPendingCounterCharacterOffset = 712;
inline constexpr int kPendingSidAttemptedOffset = 736;
inline constexpr int kSidRevealOffset = 737;
inline constexpr int kPendingReplicatedCardOffset = 738;
inline constexpr int kPendingEliminationOffset = 769;
inline constexpr int kEliminationOwnerObserverOffset = 770;
inline constexpr int kEliminationPositionOffset = 771;
inline constexpr int kEliminationBullPicksOffset = 773;
inline constexpr int kEliminationResumeKindOffset = 774;
inline constexpr int kEliminationResumePlayerOffset = 781;
inline constexpr int kPendingDamageOffset = 782;
inline constexpr int kDamageOwnerObserverOffset = 783;
inline constexpr int kDamagePositionOffset = 784;
inline constexpr int kDamageRemainingOffset = 786;
inline constexpr int kDamageSourceObserverOffset = 787;
inline constexpr int kDamageResumeKindOffset = 788;
inline constexpr int kDamageReactionDepthOffset = 795;
inline constexpr int kEliminationDepthOffset = 796;
inline constexpr int kDamageReactionChoosingOriginOffset = 797;
inline constexpr int kPrivateReserveOffset = 798;
inline constexpr int kTopDiscardOffset = 822;
inline constexpr int kTopDiscardNameOffset = kTopDiscardOffset;
inline constexpr int kTopDiscardRevealOffset =
    kTopDiscardNameOffset + kCardNameCount;
inline constexpr int kTopDiscardOriginOffset =
    kTopDiscardRevealOffset + 4;

static_assert(static_cast<int>(Phase::kTerminal) < kPhaseCapacity);
static_assert(kPlayerBlocksOffset + kPlayerBlockCount * kPlayerBlockSize ==
              kPrivateHandOffset);
static_assert(kTopDiscardOriginOffset + kSideCount == kObservationSize);

}  // namespace bang::obs_layout
