#pragma once

// The policy's inputs (observation, privileged observation, action features)
// described as ordered blocks whose sizes depend only on a few counts.
//
// Every offset the encoders use is derived from these tables, so growing a
// count (more card names, characters, card copies, phases) re-lays the inputs
// without hand-edited numbers. Blocks only ever grow at their end and new ids
// are appended, so an index inside a block keeps its meaning across layouts:
// that is what lets a policy trained on one layout be moved onto a larger one
// by copying each block's old prefix (see LayoutIndexMap).

#include <array>
#include <cstddef>
#include <vector>

#include "bang/json.hpp"

namespace bang {

struct LayoutDims {
  int card_names = 0;
  int characters = 0;
  int card_instances = 0;
  int sides = 0;
  int phases = 0;      // phase one-hot capacity, not the phase count
  int card_kinds = 0;
  int action_kinds = 0;

  friend constexpr bool operator==(const LayoutDims&, const LayoutDims&) = default;
};

// Whether every count of `from` fits within `to`, so a policy laid out for
// `from` can be moved onto `to`.
constexpr bool LayoutFits(const LayoutDims& from, const LayoutDims& to) {
  return from.card_names <= to.card_names && from.characters <= to.characters &&
         from.card_instances <= to.card_instances && from.sides <= to.sides &&
         from.phases <= to.phases && from.card_kinds <= to.card_kinds &&
         from.action_kinds <= to.action_kinds;
}

// The layout every checkpoint before the Renegades expansion was trained on.
inline constexpr LayoutDims kBaseLayoutDims{31, 24, 80, 2, 16, 3, 15};

inline constexpr int kRevealSymbolCount = 4;
inline constexpr int kResumeKindCapacity = 7;

/* ---------------- observation ---------------- */

enum class ObsBlock : int {
  kPhase, kDecisionPlayer, kTurnPlayer, kTurnNumber, kDiscardCount,
  kCommonDeckCount, kCommonDeckCreated, kSwapped, kBangLimitUsed, kPendingHit,
  kPendingTargetObserver, kBarrelAttempted, kPendingTargetPosition,
  kOwnActiveChosen, kSetupRevealed, kOptionalEffectPlayer,
  // Per player (observer first, then opponent), in this order.
  kPlayerActiveCharacter, kPlayerRearguardCharacter, kPlayerActiveLife,
  kPlayerActiveMaxLife, kPlayerRearguardLife, kPlayerRearguardMaxLife,
  kPlayerReserveCount, kPlayerHandCount, kPlayerPersonalDeckCount,
  kPlayerPersonalDeckExhausted, kPlayerActiveEquipment,
  kPlayerRearguardEquipment, kPlayerActiveEquipmentReveal,
  kPlayerRearguardEquipmentReveal,
  kOpponentActiveCharacter, kOpponentRearguardCharacter, kOpponentActiveLife,
  kOpponentActiveMaxLife, kOpponentRearguardLife, kOpponentRearguardMaxLife,
  kOpponentReserveCount, kOpponentHandCount, kOpponentPersonalDeckCount,
  kOpponentPersonalDeckExhausted, kOpponentActiveEquipment,
  kOpponentRearguardEquipment, kOpponentActiveEquipmentReveal,
  kOpponentRearguardEquipmentReveal,
  kPrivateHand, kPrivateHandReveal, kDiscardCards, kDiscardReveal,
  kFirstRevealedNames, kFirstRevealedReveal, kSecondRevealedNames,
  kSecondRevealedReveal, kPendingHitSource, kPendingAttackerObserver,
  kRevealReason, kStartTurnPending, kQueuedHitSource,
  kQueuedHitTargetObserver, kQueuedHitAttackerObserver, kPendingResponseKind,
  kHitSequenceActive, kDeferredElimination, kRemainingSequenceHits,
  kDeferredHitCount, kChoiceCardNames, kChoiceCardReveals, kChoiceCardCount,
  kChoiceCardsPublic, kEffectActorObserver, kOwnLawBackCount,
  kOwnOutlawBackCount, kOpponentLawBackCount, kOpponentOutlawBackCount,
  kAbilityUsed, kPendingAbility, kPendingAbilityStage, kPendingAvoidsRequired,
  kPendingAvoidsPlayed, kPendingReturnFire, kPendingSourceCharacter,
  kPendingResumeKind, kMovementActive, kContinuationHitCount,
  kMovementMoverObserver, kMovementTriggers, kMovementResumeKind,
  kDamageReactionResume, kDamageReactionResumePlayer,
  kPublicHandObserverNames, kPublicHandObserverReveal,
  kPublicHandOpponentNames, kPublicHandOpponentReveal,
  kPendingCounterCharacter, kPendingSidAttempted, kSidReveal,
  kPendingReplicatedCard, kPendingElimination, kEliminationOwnerObserver,
  kEliminationPosition, kEliminationBullPicks, kEliminationResumeKind,
  kEliminationResumePlayer, kPendingDamage, kDamageOwnerObserver,
  kDamagePosition, kDamageRemaining, kDamageSourceObserver,
  kDamageResumeKind, kDamageReactionDepth, kEliminationDepth,
  kDamageReactionChoosingOrigin, kPrivateReserve, kTopDiscardNames,
  kTopDiscardReveal, kTopDiscardOrigin,
  kCount,
};

inline constexpr int kObsBlockCount = static_cast<int>(ObsBlock::kCount);
// One player's sub-blocks, kPlayerActiveCharacter .. kPlayerRearguardEquipmentReveal.
inline constexpr int kObsPlayerBlockParts =
    static_cast<int>(ObsBlock::kOpponentActiveCharacter) -
    static_cast<int>(ObsBlock::kPlayerActiveCharacter);

constexpr std::array<int, kObsBlockCount> ObservationBlockSizes(const LayoutDims& d) {
  const int n = d.card_names;
  const int c = d.characters;
  const int r = kRevealSymbolCount;
  const int k = kResumeKindCapacity;
  return {{
      d.phases, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1,
      c, c, 1, 1, 1, 1, 1, 1, 1, 1, n, n, r, r,
      c, c, 1, 1, 1, 1, 1, 1, 1, 1, n, n, r, r,
      n, r, n, r, n, r, n, r, n, 1, 3, 1, n, 1, 1, 3, 1, 1, 1, 1,
      n, r, 1, 1, 1, 1, 1, 1, 1, c, c, 1, 1, 1, 1, c, k, 1, 1, 1, 3, 3,
      k, 1, n, r, n, r, c, 1, 1, n, 1, 1, 2, 1, k, 1, 1, 1, 2, 1, 1, k,
      1, 1, 1, c, n, r, d.sides,
  }};
}

/* ---------------- training observation ----------------
   The base observation followed by what only the observing player knows
   in full. The policy splits it at the own-hand block: everything before
   is public, everything from there on is the player's identity. */

enum class TrainingBlock : int {
  kBase, kSide, kOwnHand, kDiscard, kKnownOwnHand, kKnownOpponentHand,
  kSeenOwnCharacters, kSeenOpponentCharacters,
  kCount,
};

inline constexpr int kTrainingBlockCount = static_cast<int>(TrainingBlock::kCount);

/* ---------------- privileged observation (training critic only) ---------------- */

enum class PrivilegedBlock : int {
  kObserverHand, kObserverDeck, kObserverReserve,
  kOpponentHand, kOpponentDeck, kOpponentReserve,
  kCommonDeck,
  kCount,
};

inline constexpr int kPrivilegedBlockCount = static_cast<int>(PrivilegedBlock::kCount);

constexpr std::array<int, kPrivilegedBlockCount> PrivilegedBlockSizes(const LayoutDims& d) {
  const int i = d.card_instances;
  return {{i, i, d.characters, i, i, d.characters, i}};
}

/* ---------------- action features ---------------- */

enum class ActionFeatureBlock : int {
  kKind, kCardName, kReveal, kOrigin, kCardKind, kCardProperty, kTarget,
  kCharacter, kOption,
  kCount,
};

inline constexpr int kActionFeatureBlockCount = static_cast<int>(ActionFeatureBlock::kCount);

constexpr std::array<int, kActionFeatureBlockCount> ActionFeatureBlockSizes(const LayoutDims& d) {
  return {{d.action_kinds, d.card_names, kRevealSymbolCount, d.sides,
           d.card_kinds, 4, 4, d.characters, 4}};
}

/* ---------------- offsets ---------------- */

template <std::size_t N>
constexpr int BlockStart(const std::array<int, N>& sizes, int block) {
  int start = 0;
  for (int index = 0; index < block; ++index) start += sizes[static_cast<std::size_t>(index)];
  return start;
}

template <std::size_t N>
constexpr int BlockTotal(const std::array<int, N>& sizes) {
  return BlockStart(sizes, static_cast<int>(N));
}

constexpr int ObservationSizeFor(const LayoutDims& d) {
  return BlockTotal(ObservationBlockSizes(d));
}

constexpr std::array<int, kTrainingBlockCount> TrainingBlockSizes(const LayoutDims& d) {
  const int i = d.card_instances;
  return {{ObservationSizeFor(d), d.sides, i, i, i, i, d.characters, d.characters}};
}

constexpr int ObsStart(const LayoutDims& d, ObsBlock block) {
  return BlockStart(ObservationBlockSizes(d), static_cast<int>(block));
}

constexpr int TrainingStart(const LayoutDims& d, TrainingBlock block) {
  return BlockStart(TrainingBlockSizes(d), static_cast<int>(block));
}

constexpr int PrivilegedStart(const LayoutDims& d, PrivilegedBlock block) {
  return BlockStart(PrivilegedBlockSizes(d), static_cast<int>(block));
}

constexpr int ActionFeatureStart(const LayoutDims& d, ActionFeatureBlock block) {
  return BlockStart(ActionFeatureBlockSizes(d), static_cast<int>(block));
}

// A short initializer would zero-fill the trailing blocks silently.
template <std::size_t N>
constexpr bool AllBlocksPresent(const std::array<int, N>& sizes) {
  for (const int size : sizes) {
    if (size <= 0) return false;
  }
  return true;
}
static_assert(AllBlocksPresent(ObservationBlockSizes(kBaseLayoutDims)));
static_assert(AllBlocksPresent(PrivilegedBlockSizes(kBaseLayoutDims)));
static_assert(AllBlocksPresent(ActionFeatureBlockSizes(kBaseLayoutDims)));

/* The base layout must reproduce, number for number, the hand-written offsets
   every pre-expansion checkpoint was trained on. */
static_assert(ObservationSizeFor(kBaseLayoutDims) == 859);
static_assert(ObsStart(kBaseLayoutDims, ObsBlock::kPlayerActiveCharacter) == 32);
static_assert(ObsStart(kBaseLayoutDims, ObsBlock::kOpponentActiveCharacter) == 32 + 126);
static_assert(ObsStart(kBaseLayoutDims, ObsBlock::kPrivateHand) == 284);
static_assert(ObsStart(kBaseLayoutDims, ObsBlock::kFirstRevealedNames) == 354);
static_assert(ObsStart(kBaseLayoutDims, ObsBlock::kPendingHitSource) == 424);
static_assert(ObsStart(kBaseLayoutDims, ObsBlock::kChoiceCardNames) == 500);
static_assert(ObsStart(kBaseLayoutDims, ObsBlock::kAbilityUsed) == 542);
static_assert(ObsStart(kBaseLayoutDims, ObsBlock::kPendingResumeKind) == 618);
static_assert(ObsStart(kBaseLayoutDims, ObsBlock::kMovementResumeKind) == 631);
static_assert(ObsStart(kBaseLayoutDims, ObsBlock::kPublicHandObserverNames) == 642);
static_assert(ObsStart(kBaseLayoutDims, ObsBlock::kPendingCounterCharacter) == 712);
static_assert(ObsStart(kBaseLayoutDims, ObsBlock::kPendingReplicatedCard) == 738);
static_assert(ObsStart(kBaseLayoutDims, ObsBlock::kDamageResumeKind) == 788);
static_assert(ObsStart(kBaseLayoutDims, ObsBlock::kPrivateReserve) == 798);
static_assert(ObsStart(kBaseLayoutDims, ObsBlock::kTopDiscardNames) == 822);
static_assert(BlockTotal(TrainingBlockSizes(kBaseLayoutDims)) == 1229);
static_assert(TrainingStart(kBaseLayoutDims, TrainingBlock::kOwnHand) == 861);
static_assert(BlockTotal(PrivilegedBlockSizes(kBaseLayoutDims)) == 448);
static_assert(BlockTotal(ActionFeatureBlockSizes(kBaseLayoutDims)) == 91);

/* ---------------- moving a policy between layouts ----------------
   For each index of the `from` layout, its index in the `to` layout, block by
   block (each block keeps its old prefix). `to` must be at least as large in
   every block. Used to move a trained policy's input weights onto a larger
   layout: inputs that did not exist before get zero weight. */

enum class LayoutKind { kObservation, kTraining, kPrivileged, kActionFeature };

std::vector<int> LayoutIndexMap(LayoutKind kind, const LayoutDims& from, const LayoutDims& to);

// Column maps for the four network layers that read raw inputs: for each
// input column of a layer trained on `from`, its column on `to`.
struct PolicyInputColumnMaps {
  std::vector<int> public_encoder;      // training observation, public part
  std::vector<int> identity_encoder;    // training observation, identity part
  std::vector<int> action_encoder;      // action features
  std::vector<int> privileged_encoder;  // privileged observation
};
PolicyInputColumnMaps PolicyInputColumns(const LayoutDims& from, const LayoutDims& to);

// Policy metadata records the layout it was trained on. Metadata written
// before layouts were recorded (ABI v1) implies kBaseLayoutDims.
inline constexpr const char* kObservationAbiV1 = "bang_training_observation_v1";
inline constexpr const char* kObservationAbi = "bang_training_observation_v2";
inline constexpr const char* kActionFeatureAbiV1 = "bang_action_feature_v1";
inline constexpr const char* kActionFeatureAbi = "bang_action_feature_v2";
JsonValue LayoutDimsJson(const LayoutDims& dims);
LayoutDims LayoutDimsFromJson(const JsonValue& value);
int TrainingObservationSizeFor(const LayoutDims& dims);
int ActionFeatureSizeFor(const LayoutDims& dims);
int PrivilegedSizeFor(const LayoutDims& dims);
int PublicObservationSizeFor(const LayoutDims& dims);

}  // namespace bang
