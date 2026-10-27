#pragma once

#include "bang/game.hpp"
#include "bang/input_layout.hpp"

// Offsets into GameState::ObservationTensor, derived from the block table in
// input_layout.hpp for the engine's current counts (kLayoutDims).
namespace bang::obs_layout {

constexpr int Start(ObsBlock block) { return ObsStart(kLayoutDims, block); }
// Offsets inside one player's block, relative to its start.
constexpr int InPlayer(ObsBlock block) {
  return Start(block) - Start(ObsBlock::kPlayerActiveCharacter);
}

inline constexpr int kPhaseOffset = Start(ObsBlock::kPhase);
inline constexpr int kPhaseCapacity = kLayoutDims.phases;
inline constexpr int kDecisionPlayerOffset = Start(ObsBlock::kDecisionPlayer);
inline constexpr int kTurnPlayerOffset = Start(ObsBlock::kTurnPlayer);
inline constexpr int kTurnNumberOffset = Start(ObsBlock::kTurnNumber);
inline constexpr int kDiscardCountOffset = Start(ObsBlock::kDiscardCount);
inline constexpr int kCommonDeckCountOffset = Start(ObsBlock::kCommonDeckCount);
inline constexpr int kCommonDeckCreatedOffset = Start(ObsBlock::kCommonDeckCreated);
inline constexpr int kSwappedOffset = Start(ObsBlock::kSwapped);
inline constexpr int kBangLimitUsedOffset = Start(ObsBlock::kBangLimitUsed);
inline constexpr int kPendingHitOffset = Start(ObsBlock::kPendingHit);
inline constexpr int kPendingTargetObserverOffset = Start(ObsBlock::kPendingTargetObserver);
inline constexpr int kBarrelAttemptedOffset = Start(ObsBlock::kBarrelAttempted);
inline constexpr int kPendingTargetPositionOffset = Start(ObsBlock::kPendingTargetPosition);
inline constexpr int kOwnActiveChosenOffset = Start(ObsBlock::kOwnActiveChosen);
inline constexpr int kSetupRevealedOffset = Start(ObsBlock::kSetupRevealed);
inline constexpr int kOptionalEffectPlayerOffset = Start(ObsBlock::kOptionalEffectPlayer);

inline constexpr int kPlayerBlocksOffset = Start(ObsBlock::kPlayerActiveCharacter);
inline constexpr int kPlayerBlockCount = 2;
inline constexpr int kPlayerBlockSize =
    Start(ObsBlock::kOpponentActiveCharacter) - Start(ObsBlock::kPlayerActiveCharacter);
inline constexpr int kActiveCharacterOffset = InPlayer(ObsBlock::kPlayerActiveCharacter);
inline constexpr int kRearguardCharacterOffset = InPlayer(ObsBlock::kPlayerRearguardCharacter);
inline constexpr int kActiveLifeOffset = InPlayer(ObsBlock::kPlayerActiveLife);
inline constexpr int kActiveMaxLifeOffset = InPlayer(ObsBlock::kPlayerActiveMaxLife);
inline constexpr int kRearguardLifeOffset = InPlayer(ObsBlock::kPlayerRearguardLife);
inline constexpr int kRearguardMaxLifeOffset = InPlayer(ObsBlock::kPlayerRearguardMaxLife);
inline constexpr int kReserveCountOffset = InPlayer(ObsBlock::kPlayerReserveCount);
inline constexpr int kHandCountOffset = InPlayer(ObsBlock::kPlayerHandCount);
inline constexpr int kPersonalDeckCountOffset = InPlayer(ObsBlock::kPlayerPersonalDeckCount);
inline constexpr int kPersonalDeckExhaustedOffset = InPlayer(ObsBlock::kPlayerPersonalDeckExhausted);
inline constexpr int kActiveEquipmentOffset = InPlayer(ObsBlock::kPlayerActiveEquipment);
inline constexpr int kRearguardEquipmentOffset = InPlayer(ObsBlock::kPlayerRearguardEquipment);
inline constexpr int kActiveEquipmentRevealOffset = InPlayer(ObsBlock::kPlayerActiveEquipmentReveal);
inline constexpr int kRearguardEquipmentRevealOffset = InPlayer(ObsBlock::kPlayerRearguardEquipmentReveal);

inline constexpr int kPrivateHandOffset = Start(ObsBlock::kPrivateHand);
inline constexpr int kPrivateHandRevealOffset = Start(ObsBlock::kPrivateHandReveal);
inline constexpr int kDiscardCardsOffset = Start(ObsBlock::kDiscardCards);
inline constexpr int kDiscardRevealOffset = Start(ObsBlock::kDiscardReveal);
// A revealed-card slot is a card-name block followed by its reveal symbols.
inline constexpr int kFirstRevealedCardOffset = Start(ObsBlock::kFirstRevealedNames);
inline constexpr int kSecondRevealedCardOffset = Start(ObsBlock::kSecondRevealedNames);
inline constexpr int kRevealedCardSlotSize = kLayoutDims.card_names + kRevealSymbolCount;
inline constexpr int kPendingHitSourceOffset = Start(ObsBlock::kPendingHitSource);
inline constexpr int kPendingAttackerObserverOffset = Start(ObsBlock::kPendingAttackerObserver);
inline constexpr int kRevealReasonOffset = Start(ObsBlock::kRevealReason);
inline constexpr int kRevealReasonCount = kLayoutDims.reveal_reasons;
inline constexpr int kStartTurnPendingOffset = Start(ObsBlock::kStartTurnPending);
inline constexpr int kQueuedHitSourceOffset = Start(ObsBlock::kQueuedHitSource);
inline constexpr int kQueuedHitTargetObserverOffset = Start(ObsBlock::kQueuedHitTargetObserver);
inline constexpr int kQueuedHitAttackerObserverOffset = Start(ObsBlock::kQueuedHitAttackerObserver);
inline constexpr int kPendingResponseKindOffset = Start(ObsBlock::kPendingResponseKind);
inline constexpr int kPendingResponseKindCount = 3;
inline constexpr int kHitSequenceActiveOffset = Start(ObsBlock::kHitSequenceActive);
inline constexpr int kDeferredEliminationOffset = Start(ObsBlock::kDeferredElimination);
inline constexpr int kRemainingSequenceHitsOffset = Start(ObsBlock::kRemainingSequenceHits);
inline constexpr int kDeferredHitCountOffset = Start(ObsBlock::kDeferredHitCount);
inline constexpr int kChoiceCardNamesOffset = Start(ObsBlock::kChoiceCardNames);
inline constexpr int kChoiceCardRevealsOffset = Start(ObsBlock::kChoiceCardReveals);
inline constexpr int kChoiceCardCountOffset = Start(ObsBlock::kChoiceCardCount);
inline constexpr int kChoiceCardsPublicOffset = Start(ObsBlock::kChoiceCardsPublic);
inline constexpr int kEffectActorObserverOffset = Start(ObsBlock::kEffectActorObserver);
inline constexpr int kOwnLawBackCountOffset = Start(ObsBlock::kOwnLawBackCount);
inline constexpr int kOwnOutlawBackCountOffset = Start(ObsBlock::kOwnOutlawBackCount);
inline constexpr int kOpponentLawBackCountOffset = Start(ObsBlock::kOpponentLawBackCount);
inline constexpr int kOpponentOutlawBackCountOffset = Start(ObsBlock::kOpponentOutlawBackCount);
inline constexpr int kAbilityUsedOffset = Start(ObsBlock::kAbilityUsed);
inline constexpr int kPendingAbilityOffset = Start(ObsBlock::kPendingAbility);
inline constexpr int kPendingAbilityStageOffset = Start(ObsBlock::kPendingAbilityStage);
inline constexpr int kPendingAvoidsRequiredOffset = Start(ObsBlock::kPendingAvoidsRequired);
inline constexpr int kPendingAvoidsPlayedOffset = Start(ObsBlock::kPendingAvoidsPlayed);
inline constexpr int kPendingReturnFireOffset = Start(ObsBlock::kPendingReturnFire);
inline constexpr int kPendingSourceCharacterOffset = Start(ObsBlock::kPendingSourceCharacter);
inline constexpr int kPendingResumeKindOffset = Start(ObsBlock::kPendingResumeKind);
inline constexpr int kResumeKindCount = kResumeKindCapacity;
inline constexpr int kMovementActiveOffset = Start(ObsBlock::kMovementActive);
inline constexpr int kContinuationHitCountOffset = Start(ObsBlock::kContinuationHitCount);
inline constexpr int kMovementMoverObserverOffset = Start(ObsBlock::kMovementMoverObserver);
inline constexpr int kMovementTriggersOffset = Start(ObsBlock::kMovementTriggers);
inline constexpr int kMovementResumeKindOffset = Start(ObsBlock::kMovementResumeKind);
inline constexpr int kDamageReactionResumeOffset = Start(ObsBlock::kDamageReactionResume);
inline constexpr int kDamageReactionResumePlayerOffset = Start(ObsBlock::kDamageReactionResumePlayer);
// A public-hand block is a card-name block followed by its reveal symbols.
inline constexpr int kPublicHandCardsOffset = Start(ObsBlock::kPublicHandObserverNames);
inline constexpr int kPublicHandBlockSize = kLayoutDims.card_names + kRevealSymbolCount;
inline constexpr int kPendingCounterCharacterOffset = Start(ObsBlock::kPendingCounterCharacter);
inline constexpr int kPendingSidAttemptedOffset = Start(ObsBlock::kPendingSidAttempted);
inline constexpr int kSidRevealOffset = Start(ObsBlock::kSidReveal);
inline constexpr int kPendingReplicatedCardOffset = Start(ObsBlock::kPendingReplicatedCard);
inline constexpr int kPendingEliminationOffset = Start(ObsBlock::kPendingElimination);
inline constexpr int kEliminationOwnerObserverOffset = Start(ObsBlock::kEliminationOwnerObserver);
inline constexpr int kEliminationPositionOffset = Start(ObsBlock::kEliminationPosition);
inline constexpr int kEliminationBullPicksOffset = Start(ObsBlock::kEliminationBullPicks);
inline constexpr int kEliminationResumeKindOffset = Start(ObsBlock::kEliminationResumeKind);
inline constexpr int kEliminationResumePlayerOffset = Start(ObsBlock::kEliminationResumePlayer);
inline constexpr int kPendingDamageOffset = Start(ObsBlock::kPendingDamage);
inline constexpr int kDamageOwnerObserverOffset = Start(ObsBlock::kDamageOwnerObserver);
inline constexpr int kDamagePositionOffset = Start(ObsBlock::kDamagePosition);
inline constexpr int kDamageRemainingOffset = Start(ObsBlock::kDamageRemaining);
inline constexpr int kDamageSourceObserverOffset = Start(ObsBlock::kDamageSourceObserver);
inline constexpr int kDamageResumeKindOffset = Start(ObsBlock::kDamageResumeKind);
inline constexpr int kDamageReactionDepthOffset = Start(ObsBlock::kDamageReactionDepth);
inline constexpr int kEliminationDepthOffset = Start(ObsBlock::kEliminationDepth);
inline constexpr int kDamageReactionChoosingOriginOffset = Start(ObsBlock::kDamageReactionChoosingOrigin);
inline constexpr int kPrivateReserveOffset = Start(ObsBlock::kPrivateReserve);
inline constexpr int kTopDiscardOffset = Start(ObsBlock::kTopDiscardNames);
inline constexpr int kTopDiscardNameOffset = kTopDiscardOffset;
inline constexpr int kTopDiscardRevealOffset = Start(ObsBlock::kTopDiscardReveal);
inline constexpr int kTopDiscardOriginOffset = Start(ObsBlock::kTopDiscardOrigin);

static_assert(static_cast<int>(Phase::kTerminal) < kPhaseCapacity);
static_assert(kPlayerBlocksOffset + kPlayerBlockCount * kPlayerBlockSize ==
              kPrivateHandOffset);
static_assert(kTopDiscardOriginOffset + kSideCount == kObservationSize);

}  // namespace bang::obs_layout
