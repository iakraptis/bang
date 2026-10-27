#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "bang/action_space.hpp"

namespace bang {

// Sizes and offsets of the policy inputs, from the block tables in
// input_layout.hpp for the engine's current counts.
inline constexpr int kObservationSize = ObservationSizeFor(kLayoutDims);
inline constexpr int kTrainingObservationBaseSize = kObservationSize;
inline constexpr int kTrainingObservationSideOffset =
    TrainingStart(kLayoutDims, TrainingBlock::kSide);
inline constexpr int kTrainingObservationOwnHandOffset =
    TrainingStart(kLayoutDims, TrainingBlock::kOwnHand);
inline constexpr int kTrainingObservationDiscardOffset =
    TrainingStart(kLayoutDims, TrainingBlock::kDiscard);
inline constexpr int kTrainingObservationKnownOwnHandOffset =
    TrainingStart(kLayoutDims, TrainingBlock::kKnownOwnHand);
inline constexpr int kTrainingObservationKnownOpponentHandOffset =
    TrainingStart(kLayoutDims, TrainingBlock::kKnownOpponentHand);
inline constexpr int kTrainingObservationSeenOwnCharactersOffset =
    TrainingStart(kLayoutDims, TrainingBlock::kSeenOwnCharacters);
inline constexpr int kTrainingObservationSeenOpponentCharactersOffset =
    TrainingStart(kLayoutDims, TrainingBlock::kSeenOpponentCharacters);
inline constexpr int kTrainingObservationSize =
    BlockTotal(TrainingBlockSizes(kLayoutDims));

// The privileged observation is one block per player (observer first), then
// the common deck. Player offsets are relative to that player's block.
inline constexpr int kPrivilegedPlayerHandOffset =
    PrivilegedStart(kLayoutDims, PrivilegedBlock::kObserverHand);
inline constexpr int kPrivilegedPlayerDeckOffset =
    PrivilegedStart(kLayoutDims, PrivilegedBlock::kObserverDeck);
inline constexpr int kPrivilegedPlayerReserveOffset =
    PrivilegedStart(kLayoutDims, PrivilegedBlock::kObserverReserve);
inline constexpr int kPrivilegedPlayerSize =
    PrivilegedStart(kLayoutDims, PrivilegedBlock::kOpponentHand);
inline constexpr int kPrivilegedCommonDeckOffset =
    PrivilegedStart(kLayoutDims, PrivilegedBlock::kCommonDeck);
inline constexpr int kPrivilegedObservationSize =
    BlockTotal(PrivilegedBlockSizes(kLayoutDims));
static_assert(kPrivilegedCommonDeckOffset == kPrivilegedPlayerSize * kSideCount);

enum class Phase : std::uint8_t {
  kChooseActive,
  kPlay,
  kDiscard,
  kRespondToHit,
  kChooseReveal,
  kChooseRemington,
  kForcedDiscard,
  kChooseCarbine,
  kChooseShotgun,
  kDuelResponse,
  kChooseWagonDiscard,
  kChooseGeneralStore,
  kChooseSchoolmarm,
  kChooseStealing,
  kChooseAbility,
  kTerminal,
  // Renegades (appended so earlier phases keep their input slots)
  kChooseSwitch,
  kChooseUnionPacific,
  kChooseAmbush,
  kChooseSlap,
  kChooseDutchHarry,
};

class FastRng {
 public:
  explicit FastRng(std::uint64_t seed);
  std::uint64_t NextU64();
  std::size_t USize(std::size_t upper);

  template <class T>
  void Shuffle(std::vector<T>& values) {
    for (std::size_t index = values.size(); index > 1; --index) {
      const std::size_t swap_with = USize(index);
      std::swap(values[index - 1], values[swap_with]);
    }
  }

 private:
  std::uint64_t state_;
};

struct CharacterState {
  int character_id = -1;
  int life = 0;
  std::vector<int> equipment;
};

struct PlayerState {
  Side side = Side::kLaw;
  std::array<CharacterState, 2> positions{};
  int active_position = 0;
  std::vector<int> reserve;
  std::vector<int> deck;
  std::vector<int> hand;
  bool personal_deck_exhausted = false;
};

struct GameConfig {
  std::uint64_t seed = 1;
  GameMode mode = GameMode::kBase;
  int characters_per_side = 4;
  bool shuffle_decks = true;
  bool shuffle_characters = true;
  std::array<std::vector<int>, kSideCount> character_rosters{};
  std::array<std::vector<int>, kSideCount> starting_decks{};
};

class GameState {
 public:
  explicit GameState(GameConfig config = {});

  // Which cards and characters this game deals.
  GameMode Mode() const { return config_.mode; }
  // The Duel phase is resolving a Ricochet (avoid cards instead of hit cards).
  bool RicochetActive() const { return ricochet_; }
  // The side a pending Switch offers to swap second, or -1.
  int SwitchSecondPlayer() const { return switch_second_player_; }

  int CurrentPlayer() const;
  int TurnPlayer() const;
  Phase CurrentPhase() const;
  bool IsTerminal() const;
  int Winner() const;
  int TurnNumber() const;
  const std::array<PlayerState, kSideCount>& Players() const;
  const std::vector<int>& DiscardPile() const;
  const std::vector<int>& CommonDeck() const;
  const std::vector<int>& ChoiceCards() const;
  // Cards held aside while Dutch Harry looks at his draws.
  std::vector<int> CardsSetAside() const;
  const std::vector<int>& RevealedCards() const;

  enum class RevealReason : std::uint8_t {
    kBarrel,
    kDynamite,
    kRattlesnake,
    kSidCurry,
    kWhiskey,
  };
  // One entry per resolved reveal (card flipped off a deck for Dynamite,
  // Rattlesnake, Barrel, …). Sequence numbers grow monotonically so a UI can
  // animate only the reveals it has not shown yet; the list keeps a bounded
  // tail of recent events.
  struct RevealEvent {
    int sequence = 0;
    RevealReason reason = RevealReason::kBarrel;
    int player = -1;
    int card_instance = -1;
    bool triggered = false;  // whether the reveal's effect fired
  };
  const std::vector<RevealEvent>& RevealEvents() const;

  bool CommonDeckCreated() const;
  bool SetupComplete() const;
  bool ChoiceCardsVisibleTo(int observer) const;
  std::vector<int> KnownHandCards(int observer, int player) const;

  std::vector<int> LegalActions() const;
  std::vector<std::pair<int, ActionFeature>> LegalActionFeatures() const;
  int ActingCharacterId(int action_id) const;
  // The character whose ability is waiting on a decision, or -1 when no
  // ability decision is pending. A UI needs it to explain what the choice is
  // about — the phase alone covers a dozen different abilities.
  int PendingAbilityCharacter() const;
  void ApplyAction(int action_id);
  std::array<float, kObservationSize> ObservationTensor(int observer) const;
  std::array<float, kTrainingObservationSize> TrainingObservationTensor(
      int observer) const;
  std::array<float, kPrivilegedObservationSize> PrivilegedObservationTensor(
      int observer) const;
  std::array<float, kSideCount> Returns() const;

 private:
  enum class ResponseKind : std::uint8_t {
    kAvoid,
    kAnyHandCard,
    kHitCard,
  };
  enum class StartTurnStep : std::uint8_t {
    kNone,
    kDynamite,
    kRattlesnake,
    kStampede,
    kDraw,
  };
  enum class ResumeKind : std::uint8_t {
    kPlay,
    kStartTurn,
    kPendingEffect,
    kMovement,
    kEffectChoice,
    kDamageReaction,
    kElimination,
  };
  enum class MovementTrigger : std::uint8_t {
    kTomThorn, kClock, kJango, kSidekick, kJimLonghair};
  // Where a run of deck draws (Union Pacific, The Marshal) resumes.
  enum class DeckDrawResume : std::uint8_t { kPlay, kStartTurn, kReplacement };

  struct PendingHit {
    int attacker = -1;
    int target = -1;
    int target_position = -1;
    int resume_player = -1;
    CardName source = CardName::kColt;
    bool barrel_attempted = false;
    ResponseKind response = ResponseKind::kAvoid;
    int avoids_required = 1;
    int avoids_played = 0;
    bool return_fire_pending = false;
    int counter_character = -1;
    bool sid_attempted = false;
    ResumeKind resume_kind = ResumeKind::kPlay;
    int source_character = -1;
    // The card copy that dealt the hit (Walker returns, Feint takes it).
    int source_instance = -1;
    // A Feint helped avoid this hit.
    bool feint = false;
    // The hit was not avoided (Howitzer then also hits the rearguard).
    bool landed = false;
    // A second hit a card already dealt (Double Barrel, Howitzer).
    bool follow_up = false;
  };

  struct PendingReveal {
    RevealReason reason = RevealReason::kBarrel;
    int player = -1;
    int position = -1;
    int equipment_instance = -1;
  };

  struct DamageReaction {
    int character_id = -1;
    int player = -1;
    int remaining = 0;
    PendingHit continuation{};
    bool choosing_origin = false;
  };

  struct DeferredEffect {
    std::optional<PendingHit> hit;
    std::optional<DamageReaction> reaction;
  };

  struct PendingElimination {
    int player = -1;
    int position = -1;
    int character_id = -1;
    int bull_player = -1;
    int bull_picks = 0;
    int bill_player = -1;
    PendingHit continuation{};
    std::vector<DeferredEffect> deferred_effects;
  };

  struct PendingDamage {
    int player = -1;
    int position = -1;
    int remaining = 0;
    int source_player = -1;
    PendingHit continuation{};
  };

  GameConfig config_;
  // The turn each card copy last entered play, for Resources' one-turn wait.
  std::array<int, kCardInstanceCount> entered_play_turn_{};
  // Additional BANG! cards allowed this turn (Lightning).
  int extra_bang_allowance_ = 0;
  // Switch: the side that may still swap once the first swap has resolved.
  int switch_second_player_ = -1;
  // Kitty Leroy: the Resource waiting for her choice, and the one whose
  // effect repeats once its first resolution is over.
  int kitty_resource_ = -1;
  Target kitty_target_ = Target::kOwnActive;
  int kitty_repeat_ = -1;
  // Dutch Harry: draws waiting for his look, resolved before the next
  // decision, with the interrupted decision saved meanwhile.
  struct DutchHarryDraw {
    int deck_owner = -1;
    bool to_wagon = false;
  };
  std::vector<DutchHarryDraw> dutch_draws_;
  int dutch_player_ = -1;
  bool dutch_wagon_ = false;
  std::vector<int> dutch_wagon_cards_;
  Phase dutch_saved_phase_ = Phase::kPlay;
  int dutch_saved_decision_ = -1;
  std::vector<int> dutch_saved_choice_;
  bool dutch_saved_choice_public_ = false;
  int deck_draw_player_ = -1;
  int deck_draws_remaining_ = 0;
  DeckDrawResume deck_draw_resume_ = DeckDrawResume::kPlay;
  // Ambush: 0 picks from your own AC, 1 from any AC.
  int ambush_stage_ = 0;
  bool ricochet_ = false;
  int slap_stage_ = 0;
  // Characters that received a Resource this turn (King Fishers).
  std::array<bool, kCharacterCount> received_resource_this_turn_{};
  // Wild Bill owners whose cards Slap removed; each reacts once Slap ends.
  std::vector<int> slap_wild_bill_owners_;
  std::array<PlayerState, kSideCount> players_{};
  std::vector<int> discard_;
  std::array<std::vector<int>, kSideCount> public_hand_cards_{};
  std::vector<int> common_deck_;
  bool common_deck_created_ = false;
  int turn_player_ = 0;
  int decision_player_ = 0;
  int turn_number_ = 1;
  Phase phase_ = Phase::kChooseActive;
  std::array<bool, kSideCount> active_chosen_{};
  std::array<std::array<bool, kCharacterCount>, kSideCount>
      seen_characters_{};
  int winner_ = -1;
  bool swapped_this_turn_ = false;
  int bang_cards_played_ = 0;
  std::array<bool, kCharacterCount> ability_used_this_turn_{};
  int pending_ability_ = -1;
  int ability_stage_ = 0;
  int pending_replicated_card_ = -1;
  std::optional<PendingHit> pending_hit_;
  std::optional<PendingHit> queued_hit_;
  std::vector<PendingHit> continuation_hits_;
  std::vector<PendingHit> hit_sequence_;
  std::vector<DeferredEffect> deferred_effects_;
  std::vector<DamageReaction> damage_reaction_stack_;
  std::optional<PendingDamage> pending_damage_;
  std::vector<PendingElimination> elimination_stack_;
  std::vector<std::pair<int, int>> deferred_eliminations_;
  bool hit_sequence_active_ = false;
  bool defer_eliminations_ = false;
  int hit_sequence_resume_player_ = -1;
  std::vector<int> choice_cards_;
  bool choice_cards_public_ = false;
  int effect_actor_ = -1;
  int effect_target_position_ = -1;
  std::optional<PendingReveal> pending_reveal_;
  std::vector<int> revealed_cards_;
  std::vector<RevealEvent> reveal_events_;
  int reveal_sequence_ = 0;
  int optional_effect_player_ = -1;
  int optional_effect_resume_player_ = -1;
  std::optional<PendingHit> optional_effect_hit_;
  int pending_draw_count_ = 0;
  std::vector<MovementTrigger> movement_triggers_;
  int movement_mover_ = -1;
  ResumeKind movement_resume_kind_ = ResumeKind::kPlay;
  StartTurnStep start_turn_step_ = StartTurnStep::kNone;
  FastRng rng_;

  void InitializePlayers();
  void StartTurn();
  void AdvanceStartTurn();
  void FinishTurn();
  void DrawCards(int player, int count);
  void DrawOne(int player);
  void DrawTurnCards(int count);
  void FinishPatGarret(bool draw);
  std::optional<int> TakeTopCard(int player);
  std::optional<int> TakeTopCommonCard();
  void RefillCommonDeck();
  void DiscardFromHand(int player, int card_instance);
  void MarkHandCardPublic(int player, int card_instance);
  void ForgetPublicHandCard(int player, int card_instance);
  bool SwapCharacters(int player, bool voluntary,
                      ResumeKind resume_kind = ResumeKind::kPlay);
  bool AdvanceMovementTriggers(bool resume_when_complete);
  void ResumeAfterMovement();
  int LivingCharacterCount(int player) const;
  int HandLimit(int player) const;
  bool HasCardInHand(int player, int card_instance) const;
  bool HasEquipment(int player, int position, CardName name) const;
  bool HasActiveEquipment(int player, CardName name) const;
  bool TargetHasAbility(int player, int position, CharacterName name) const;
  bool IsCharacterAbilityActive(int player, CharacterName name) const;
  int LivingCharacterPosition(int player, CharacterName name) const;
  bool CanProvideHit(int player, int position, int card_instance) const;
  int BangCardAllowance(int player) const;
  std::vector<int> CattleKatieEquipment(int player) const;
  std::optional<std::pair<int, int>> FindEquipment(int player, CardName name) const;
  std::pair<int, int> ResolveTarget(int actor, Target target) const;
  bool IsSupportedEquipment(CardName name) const;
  // Resources (Renegades): implemented, on `player`'s active character, and
  // not put into play this turn.
  bool IsSupportedResource(CardName name) const;
  bool CanUseResource(int player, int card_instance) const;
  // Walker goes back to its owner and Feint takes the BANG! card it avoided.
  void ApplyAvoidedCardMoves(const PendingHit& hit);
  bool CanSwapSide(int player) const;
  // Spurs: an active character whose owner holds no cards cannot be the
  // target of hits (Sharps ignores it).
  bool SpursShield(int player, int position) const;
  // Spurs stops hits, but not Indians!-style "discard or lose life" effects.
  bool BlockedBySpurs(const PendingHit& hit) const;
  // Hit Resources on `player`'s AC can be thrown away to answer Duel or
  // Indians!, even on the turn they entered play.
  std::vector<int> HitResourcesOnActive(int player) const;
  void BeginRicochet();
  // Slap: 0 picks a card in play on any AC, 1 a hand card of any player.
  void FinishSlapPick(std::optional<int> card_instance);
  void FinishSlapHandBack(Side origin);
  void FinishSlap();
  // A Resource on `player`'s side was used for its effect (Hottie Delou's
  // same-turn use, Charlie Basset's draw).
  void RecordResourceUse(int player, int card_instance);
  void FinishSwitchChoice(bool opponent);
  void ContinueSwitch();
  void FinishSwitchSecond(bool accept);
  // Draw `count` cards for `player`, choosing a deck for each (Union
  // Pacific, The Marshal), then resume where the draws interrupted.
  void BeginDeckDraws(int player, int count, DeckDrawResume resume);
  void FinishDeckDraw(bool opponent);
  // The rest of an elimination once its replacement has entered play.
  void FinishReplacement();
  bool PokerAliciaCanTake() const;
  void FinishPokerAlicia(std::optional<int> card_instance);
  void FinishAmbushPick(std::optional<int> card_instance);
  void UseResource(int card_instance, Target target);
  bool IsSupportedBang(CardName name) const;
  bool IsSupportedAction(CardName name) const;
  void PlaceEquipment(int actor, int card_instance, Target target);
  void RemoveEquipment(int player, int position, int card_instance);
  void ResolveBang(int card_instance, Target target);
  void ResolveAsColt(int card_instance, Target target);
  void ResolveActivatedAbility(CharacterName character);
  void FinishCattleKatie(int equipment_instance);
  void FinishSoundanceKid(int card_instance);
  void FinishPearlHatCost(int card_instance);
  void FinishPearlHatEffect(int source_instance, Target target);
  std::vector<int> PearlHatActions() const;
  std::vector<int> PearlHatActionsFor(int source_instance) const;
  bool CanUsePearlHat() const;
  void FinishJango(bool draw);
  void FinishJimLonghair(bool swap);
  // El Chindi at `player`'s `position`, immune to `actor`'s Action cards.
  bool ElChindiImmune(int player, int position, int actor) const;
  // A Switch may swap this side (El Chindi can refuse it).
  bool SwitchCanSwap(int player) const;
  void ResolveResource(int card_instance, Target target, bool repeat);
  void ResolveResourceEffect(int card_instance, Target target);
  void FinishKittyLeroy(std::optional<int> hand_card);
  std::vector<int> KittyLeroyRepeatActions() const;
  void BeginKittyLeroyRepeat();
  void FinishKittyLeroyRepeat(Target target);
  void ApplyLegalAction(const ActionKey& action);
  // Draw 1 card from `deck_owner`'s deck (or the common deck) into
  // `player`'s hand; Dutch Harry's draws wait for his look.
  void DrawFromDeck(int player, int deck_owner);
  void BeginDutchHarryLooks();
  void NextDutchHarryLook();
  void FinishDutchHarry(bool keep);
  void ContinueWagon();
  // Hick Addams draws when the opponent makes him discard.
  void HickAddamsDraw(int owner, int actor, std::optional<int> equipment_position);
  // Mystic Maid's Equipment while she is the active character.
  std::vector<int> MysticMaidEquipment(int player) const;
  void FinishMysticMaidAvoid(int card_instance);
  void CompleteActivatedAbility(CharacterName character);
  void ResolveAction(int card_instance, Target target);
  void ResolveActionEffect(CardName name, Target target);
  void BeginDuel(Target target);
  void FinishDuelResponse(std::optional<int> card_instance);
  void BeginWagon();
  void FinishWagon(int discarded_instance);
  void BeginGeneralStore();
  void FinishGeneralStorePick(int card_instance);
  void BeginSchoolmarm();
  void FinishSchoolmarm(std::optional<int> equipment_instance);
  void BeginStealing();
  void FinishStealingEquipment(int equipment_instance);
  void FinishStealingHand(Side origin);
  void FinishEffectChoice();
  std::vector<int> SelectableEquipment(int actor) const;
  void ResolveReturnFire(int card_instance);
  void RecordAvoid(bool return_fire, int counter_character = -1);
  void BeginSidCurryReveal();
  void BeginBarrelReveal(int barrel_instance);
  void BeginReveal(PendingReveal reveal);
  void FinishReveal(int selected_index);
  void MoveEquipment(int from_player, int from_position, int card_instance,
                     int to_player, int to_position);
  void BeginHitSequence(std::vector<PendingHit> hits, int resume_player);
  void AdvanceHitSequence();
  void ContinueAfterResolvedHit(const PendingHit& hit);
  bool BeginWildBillReaction(
      int owner, int actor, ResumeKind resume_kind,
      std::optional<int> equipment_position = std::nullopt);
  bool BeginAlanPinkertoonHits(int player, int count,
                              const PendingHit& continuation);
  bool BeginOpponentDamageReaction(int player, int character_id, int count,
                                   int source_player,
                                   const PendingHit& continuation);
  bool BeginWyattEarReaction(const PendingHit& hit,
                             std::optional<PendingHit> next_hit);
  void BeginElimination(
      int player, int position, const PendingHit& continuation,
      std::vector<DeferredEffect> deferred_effects = {});
  void FinishBullAndersonEquipment(int equipment_instance);
  void FinishBullAndersonHand(Side origin);
  void FinishBullAnderson();
  void FinishBillTightman(bool draw);
  bool BullAndersonHasChoices() const;
  void ResumeElimination();
  void FinalizeElimination();
  void CompleteElimination();
  void BeginDamage(int player, int position, int amount, int source_player,
                   const PendingHit& continuation);
  void ApplyPendingDamage();
  void FinishBuffaloBell(bool redirect);
  void ActivateDamageReaction(DamageReaction reaction);
  void ResumeDamageReaction();
  void FinishDamageReaction(bool accept);
  void FinishSlimPoetHand(Side origin);
  void ResolveHit();
  void ActivatePendingHit(PendingHit hit);
  void ResolveAvoidedHit(const PendingHit& hit,
                         std::optional<PendingHit> next_hit,
                         bool check_wyatt = true);
  void FinishRemingtonChoice(bool draw);
  void FinishForcedDiscard(int card_instance);
  void FinishCarbineChoice(int equipment_instance);
  void FinishShotgunChoice(bool opponent);
  void ResumeAfterPendingEffect();
  std::vector<int> ActiveEquipmentInstances() const;
  void ApplyAvoidedHitEffects(const PendingHit& hit);
  void Heal(int player, int position, int amount);
  int LoseLife(int player, int position, int amount);
  void RecordVisibleCharacters();
};

}  // namespace bang
