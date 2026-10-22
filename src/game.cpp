#include "bang/game.hpp"

#include "bang/observation_layout.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>

namespace bang {
namespace {

int PlayerIndex(Side side) { return static_cast<int>(side); }

int OtherPlayer(int player) {
  if (player != 0 && player != 1) throw std::out_of_range("invalid player");
  return 1 - player;
}

}  // namespace

FastRng::FastRng(std::uint64_t seed) : state_(seed) {}

std::uint64_t FastRng::NextU64() {
  state_ += 0x9e3779b97f4a7c15ULL;
  std::uint64_t value = state_;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

std::size_t FastRng::USize(std::size_t upper) {
  if (upper == 0) throw std::invalid_argument("random upper bound must be positive");
  const std::uint64_t bound = static_cast<std::uint64_t>(upper);
  const std::uint64_t threshold = (0ULL - bound) % bound;
  for (;;) {
    const std::uint64_t value = NextU64();
    if (value >= threshold) return static_cast<std::size_t>(value % bound);
  }
}

GameState::GameState(GameConfig config) : config_(std::move(config)), rng_(config_.seed) {
  const int roster_size =
      static_cast<int>(CharacterIds(Side::kLaw, config_.mode).size());
  if (config_.characters_per_side < 2 ||
      config_.characters_per_side > roster_size) {
    throw std::invalid_argument(
        "characters_per_side must be between 2 and " + std::to_string(roster_size));
  }
  InitializePlayers();
}

void GameState::InitializePlayers() {
  for (int player = 0; player < kSideCount; ++player) {
    auto& state = players_[static_cast<std::size_t>(player)];
    state.side = static_cast<Side>(player);
    const auto expected_deck = StartingDeck(state.side, config_.mode);
    state.deck = config_.starting_decks[static_cast<std::size_t>(player)];
    if (state.deck.empty()) {
      state.deck = expected_deck;
      if (config_.shuffle_decks) rng_.Shuffle(state.deck);
    } else {
      auto sorted_actual = state.deck;
      auto sorted_expected = expected_deck;
      std::sort(sorted_actual.begin(), sorted_actual.end());
      std::sort(sorted_expected.begin(), sorted_expected.end());
      if (sorted_actual != sorted_expected) {
        throw std::invalid_argument(
            "configured starting deck must contain each card from its side exactly once");
      }
    }

    std::vector<int> roster = config_.character_rosters[static_cast<std::size_t>(player)];
    if (roster.empty()) {
      roster = CharacterIds(state.side, config_.mode);
      if (config_.shuffle_characters) rng_.Shuffle(roster);
      roster.resize(static_cast<std::size_t>(config_.characters_per_side));
      // The selected group is shuffled again before forming the reserve.
      if (config_.shuffle_characters) rng_.Shuffle(roster);
    } else {
      if (static_cast<int>(roster.size()) != config_.characters_per_side) {
        throw std::invalid_argument("configured roster size must match characters_per_side");
      }
      auto unique = roster;
      std::sort(unique.begin(), unique.end());
      if (std::adjacent_find(unique.begin(), unique.end()) != unique.end()) {
        throw std::invalid_argument("configured character roster contains duplicates");
      }
      const auto dealable = CharacterIds(state.side, config_.mode);
      for (const int character_id : roster) {
        if (std::find(dealable.begin(), dealable.end(), character_id) == dealable.end()) {
          throw std::invalid_argument(
              "configured character is not dealt to this side in this game mode");
        }
      }
    }

    for (int position = 0; position < 2; ++position) {
      const int character_id = roster[static_cast<std::size_t>(position)];
      state.positions[static_cast<std::size_t>(position)] = {
          character_id, GetCharacter(character_id).max_life, {}};
    }
    // Back is the top of every draw pile in the engine.
    for (int index = static_cast<int>(roster.size()) - 1; index >= 2; --index) {
      state.reserve.push_back(roster[static_cast<std::size_t>(index)]);
    }
  }

  DrawCards(PlayerIndex(Side::kLaw), 4);
  DrawCards(PlayerIndex(Side::kOutlaw), 5);
  decision_player_ = PlayerIndex(Side::kLaw);
  turn_player_ = PlayerIndex(Side::kLaw);
  phase_ = Phase::kChooseActive;
}

int GameState::CurrentPlayer() const { return decision_player_; }
int GameState::TurnPlayer() const { return turn_player_; }
Phase GameState::CurrentPhase() const { return phase_; }
bool GameState::IsTerminal() const { return phase_ == Phase::kTerminal; }
int GameState::Winner() const { return winner_; }
int GameState::TurnNumber() const { return turn_number_; }
const std::array<PlayerState, kSideCount>& GameState::Players() const { return players_; }
const std::vector<int>& GameState::DiscardPile() const { return discard_; }
const std::vector<int>& GameState::CommonDeck() const { return common_deck_; }
const std::vector<int>& GameState::ChoiceCards() const { return choice_cards_; }
const std::vector<int>& GameState::RevealedCards() const {
  return revealed_cards_;
}

const std::vector<GameState::RevealEvent>& GameState::RevealEvents() const {
  return reveal_events_;
}
bool GameState::CommonDeckCreated() const { return common_deck_created_; }
bool GameState::SetupComplete() const {
  return active_chosen_[0] && active_chosen_[1];
}

bool GameState::ChoiceCardsVisibleTo(int observer) const {
  if (observer < 0 || observer >= kSideCount) {
    throw std::out_of_range("invalid observer");
  }
  return choice_cards_public_ || decision_player_ == observer;
}

std::vector<int> GameState::KnownHandCards(int observer, int player) const {
  if (observer < 0 || observer >= kSideCount ||
      player < 0 || player >= kSideCount) {
    throw std::out_of_range("invalid hand viewer");
  }
  if (observer == player) {
    return players_[static_cast<std::size_t>(player)].hand;
  }
  return public_hand_cards_[static_cast<std::size_t>(player)];
}

std::vector<std::pair<int, ActionFeature>> GameState::LegalActionFeatures()
    const {
  const auto actions = LegalActions();
  std::vector<std::pair<int, ActionFeature>> result;
  result.reserve(actions.size());
  for (const int action : actions) {
    result.emplace_back(action, EncodeAction(action));
  }
  return result;
}

int GameState::ActingCharacterId(int action_id) const {
  const ActionKey action = DecodeAction(action_id);
  const auto character_at = [&](int player, int position) {
    if (player < 0 || player >= kSideCount || position < 0 || position >= 2) {
      return -1;
    }
    return players_[static_cast<std::size_t>(player)]
        .positions[static_cast<std::size_t>(position)]
        .character_id;
  };

  if (action.kind == ActionKind::kChooseActive) {
    return character_at(decision_player_, action.card_instance);
  }
  if (action.kind == ActionKind::kUseAbility) {
    return action.card_instance;
  }
  if (phase_ == Phase::kChooseAbility && pending_ability_ >= 0) {
    return pending_ability_;
  }
  if (phase_ == Phase::kRespondToHit && pending_hit_) {
    return character_at(pending_hit_->target, pending_hit_->target_position);
  }
  if (phase_ == Phase::kDuelResponse && effect_actor_ >= 0) {
    const int position =
        decision_player_ == effect_actor_
            ? players_[static_cast<std::size_t>(decision_player_)]
                  .active_position
            : effect_target_position_;
    return character_at(decision_player_, position);
  }
  if (phase_ == Phase::kForcedDiscard && optional_effect_hit_) {
    return character_at(optional_effect_hit_->target,
                        optional_effect_hit_->target_position);
  }
  if (phase_ == Phase::kChooseReveal) {
    const int bart = static_cast<int>(CharacterName::kBartMasterson);
    for (const CharacterState& character :
         players_[static_cast<std::size_t>(decision_player_)].positions) {
      if (character.character_id == bart && character.life > 0) return bart;
    }
  }

  if (decision_player_ < 0 || decision_player_ >= kSideCount) return -1;
  const PlayerState& player =
      players_[static_cast<std::size_t>(decision_player_)];
  return character_at(decision_player_, player.active_position);
}

int GameState::PendingAbilityCharacter() const {
  return phase_ == Phase::kChooseAbility ? pending_ability_ : -1;
}

std::vector<int> GameState::LegalActions() const {
  std::vector<int> result;
  if (phase_ == Phase::kTerminal) return result;
  if (phase_ == Phase::kChooseActive) {
    return {kChooseActiveLeftAction, kChooseActiveRightAction};
  }
  if (phase_ == Phase::kChooseReveal) {
    result.push_back(kChooseRevealFirstAction);
    if (revealed_cards_.size() > 1) result.push_back(kChooseRevealSecondAction);
    return result;
  }
  if (phase_ == Phase::kChooseRemington) {
    return {kSkipOptionalAction, kAcceptOptionalAction};
  }
  if (phase_ == Phase::kForcedDiscard) {
    const auto& hand = players_[static_cast<std::size_t>(decision_player_)].hand;
    for (const int instance : hand) result.push_back(DiscardCardAction(instance));
    return result;
  }
  if (phase_ == Phase::kChooseCarbine) {
    result.push_back(kSkipOptionalAction);
    for (const int instance : ActiveEquipmentInstances()) {
      result.push_back(DiscardCardAction(instance));
    }
    return result;
  }
  if (phase_ == Phase::kChooseShotgun) {
    const auto& own = players_[static_cast<std::size_t>(optional_effect_player_)];
    if (own.positions[0].life > 0 && own.positions[1].life > 0) {
      result.push_back(kChooseOwnSwapAction);
    }
    const auto& opponent =
        players_[static_cast<std::size_t>(OtherPlayer(optional_effect_player_))];
    if (opponent.positions[0].life > 0 && opponent.positions[1].life > 0) {
      result.push_back(kChooseOpponentSwapAction);
    }
    return result;
  }
  if (phase_ == Phase::kDuelResponse) {
    result.push_back(kSkipOptionalAction);
    const int position =
        decision_player_ == effect_actor_
            ? players_[static_cast<std::size_t>(decision_player_)]
                  .active_position
            : effect_target_position_;
    const auto& hand = players_[static_cast<std::size_t>(decision_player_)].hand;
    for (const int instance : hand) {
      if (CanProvideHit(decision_player_, position, instance)) {
        result.push_back(DiscardCardAction(instance));
      }
    }
    return result;
  }
  if (phase_ == Phase::kChooseWagonDiscard) {
    for (const int instance : choice_cards_) {
      result.push_back(DiscardCardAction(instance));
    }
    return result;
  }
  if (phase_ == Phase::kChooseGeneralStore) {
    for (const int instance : choice_cards_) {
      result.push_back(SelectCardAction(instance));
    }
    return result;
  }
  if (phase_ == Phase::kChooseSchoolmarm) {
    result.push_back(kSkipOptionalAction);
    for (const int instance : SelectableEquipment(effect_actor_)) {
      result.push_back(SelectCardAction(instance));
    }
    return result;
  }
  if (phase_ == Phase::kChooseStealing) {
    const auto& opponent = players_[static_cast<std::size_t>(OtherPlayer(effect_actor_))];
    const auto has_origin = [&](Side origin) {
      return std::any_of(opponent.hand.begin(), opponent.hand.end(), [origin](int instance) {
        return GetCardInstance(instance).origin == origin;
      });
    };
    if (has_origin(Side::kLaw)) result.push_back(kChooseLawBackAction);
    if (has_origin(Side::kOutlaw)) result.push_back(kChooseOutlawBackAction);
    for (const int instance : SelectableEquipment(effect_actor_)) {
      result.push_back(SelectCardAction(instance));
    }
    return result;
  }
  if (phase_ == Phase::kChooseAbility) {
    if (pending_damage_ &&
        pending_ability_ ==
            static_cast<int>(CharacterName::kBuffaloBell)) {
      result.push_back(kSkipOptionalAction);
      const auto& damage = *pending_damage_;
      const int buffalo_position =
          LivingCharacterPosition(damage.player,
                                  CharacterName::kBuffaloBell);
      if (buffalo_position >= 0 && damage.remaining > 0 &&
          buffalo_position != damage.position &&
          players_[static_cast<std::size_t>(damage.player)]
                  .positions[static_cast<std::size_t>(buffalo_position)]
                  .life > 1) {
        result.push_back(kAcceptOptionalAction);
      }
      return result;
    }
    const bool resolving_elimination =
        !elimination_stack_.empty() &&
        (pending_ability_ ==
             static_cast<int>(CharacterName::kBillTightman) ||
         pending_ability_ ==
             static_cast<int>(CharacterName::kBullAnderson));
    if (resolving_elimination) {
      const auto ability = static_cast<CharacterName>(pending_ability_);
      result.push_back(kSkipOptionalAction);
      if (ability == CharacterName::kBillTightman) {
        result.push_back(kAcceptOptionalAction);
      } else if (ability == CharacterName::kBullAnderson) {
        const auto& elimination = elimination_stack_.back();
        const auto& character =
            players_[static_cast<std::size_t>(elimination.player)]
                .positions[static_cast<std::size_t>(elimination.position)];
        for (const int instance : character.equipment) {
          result.push_back(SelectCardAction(instance));
        }
        if (elimination.player != elimination.bull_player) {
          const auto& hand =
              players_[static_cast<std::size_t>(elimination.player)].hand;
          const auto has_origin = [&](Side origin) {
            return std::any_of(hand.begin(), hand.end(), [origin](int instance) {
              return GetCardInstance(instance).origin == origin;
            });
          };
          if (has_origin(Side::kLaw)) result.push_back(kChooseLawBackAction);
          if (has_origin(Side::kOutlaw)) {
            result.push_back(kChooseOutlawBackAction);
          }
        }
      }
      return result;
    }
    const bool resolving_damage_reaction =
        !damage_reaction_stack_.empty() &&
        damage_reaction_stack_.back().character_id == pending_ability_;
    if (resolving_damage_reaction) {
      const auto& reaction = damage_reaction_stack_.back();
      if (reaction.character_id ==
              static_cast<int>(CharacterName::kSlimPoet) &&
          reaction.choosing_origin) {
        const auto& hand =
            players_[static_cast<std::size_t>(
                OtherPlayer(reaction.player))]
                .hand;
        const auto has_origin = [&](Side origin) {
          return std::any_of(
              hand.begin(), hand.end(), [origin](int instance) {
                return GetCardInstance(instance).origin == origin;
              });
        };
        if (has_origin(Side::kLaw)) {
          result.push_back(kChooseLawBackAction);
        }
        if (has_origin(Side::kOutlaw)) {
          result.push_back(kChooseOutlawBackAction);
        }
        return result;
      }
      result.push_back(kSkipOptionalAction);
      if (reaction.character_id !=
              static_cast<int>(CharacterName::kSlimPoet) ||
          !players_[static_cast<std::size_t>(OtherPlayer(reaction.player))]
               .hand.empty()) {
        result.push_back(kAcceptOptionalAction);
      }
      return result;
    }
    const auto ability = static_cast<CharacterName>(pending_ability_);
    if (ability == CharacterName::kJango ||
        ability == CharacterName::kPatGarret) {
      result.push_back(kSkipOptionalAction);
      result.push_back(kAcceptOptionalAction);
    } else if (ability == CharacterName::kCattleKatie) {
      for (const int instance : CattleKatieEquipment(decision_player_)) {
        result.push_back(SelectCardAction(instance));
      }
    } else if (ability == CharacterName::kSoundanceKid) {
      if (ability_stage_ == 0) {
        for (const int instance :
             players_[static_cast<std::size_t>(decision_player_)].hand) {
          result.push_back(DiscardCardAction(instance));
        }
      } else {
        for (const int instance : choice_cards_) {
          result.push_back(DiscardCardAction(instance));
        }
      }
    } else if (ability == CharacterName::kPearlHat) {
      if (ability_stage_ == 0) {
        for (const int instance :
             players_[static_cast<std::size_t>(decision_player_)].hand) {
          result.push_back(DiscardCardAction(instance));
        }
      } else {
        result = PearlHatActions();
      }
    }
    return result;
  }

  const auto& player = players_[static_cast<std::size_t>(decision_player_)];
  if (phase_ == Phase::kRespondToHit) {
    if (!pending_hit_) throw std::logic_error("hit response phase has no pending hit");
    result.push_back(kTakeHitAction);
    const bool needs_additional_card = pending_hit_->avoids_played > 0;
    for (const int instance : player.hand) {
      const auto& card = GetCard(GetCardInstance(instance).name);
      const bool can_respond =
          needs_additional_card ||
          pending_hit_->response == ResponseKind::kAnyHandCard ||
          (pending_hit_->response == ResponseKind::kHitCard &&
           CanProvideHit(pending_hit_->target,
                         pending_hit_->target_position, instance)) ||
          (pending_hit_->response == ResponseKind::kAvoid &&
           ((card.has_avoid && card.kind != CardKind::kResource) ||
            (TargetHasAbility(pending_hit_->target, pending_hit_->target_position,
                              CharacterName::kAnnieOakey) &&
             card.has_hit)));
      if (can_respond) {
        result.push_back(AvoidHitAction(instance));
      }
    }
    if (needs_additional_card ||
        pending_hit_->response == ResponseKind::kHitCard) {
      return result;
    }
    if (!pending_hit_->sid_attempted &&
        TargetHasAbility(pending_hit_->target, pending_hit_->target_position,
                         CharacterName::kSidCurry)) {
      result.push_back(UseAbilityAction(CharacterName::kSidCurry));
    }
    const auto& target_character =
        players_[static_cast<std::size_t>(pending_hit_->target)]
            .positions[static_cast<std::size_t>(pending_hit_->target_position)];
    for (const int instance : target_character.equipment) {
      const CardName name = GetCardInstance(instance).name;
      if (name == CardName::kHat ||
          (name == CardName::kBarrel && !pending_hit_->barrel_attempted) ||
          (GetCard(name).has_avoid && CanUseResource(pending_hit_->target, instance))) {
        result.push_back(AvoidHitAction(instance));
      }
    }
    return result;
  }

  if (phase_ == Phase::kDiscard) {
    for (const int instance : player.hand) result.push_back(DiscardCardAction(instance));
    return result;
  }

  result.push_back(kEndPlayAction);
  const int other_position = 1 - player.active_position;
  if (!swapped_this_turn_ && player.positions[static_cast<std::size_t>(other_position)].life > 0) {
    result.push_back(kSwapAction);
  }
  const bool can_reload = LivingCharacterCount(0) == 1 || LivingCharacterCount(1) == 1;
  const bool jack_active =
      IsCharacterAbilityActive(decision_player_, CharacterName::kJackRansome);
  const bool can_play_bang =
      bang_cards_played_ < BangCardAllowance(decision_player_) ||
      HasActiveEquipment(decision_player_, CardName::kGunBelt);
  for (const int instance : player.hand) {
    const auto& card = GetCard(GetCardInstance(instance).name);
    if (card.kind == CardKind::kBang && IsSupportedBang(card.name) &&
        can_play_bang) {
      result.push_back(PlayCardAction(instance, Target::kOpponentActive));
      const int opponent = OtherPlayer(decision_player_);
      const auto& opponent_state = players_[static_cast<std::size_t>(opponent)];
      const int opponent_rear = 1 - opponent_state.active_position;
      if (opponent_state.positions[static_cast<std::size_t>(opponent_rear)].life > 0 &&
          (jack_active || card.name == CardName::kWinchester ||
           HasActiveEquipment(decision_player_, CardName::kScope))) {
        result.push_back(PlayCardAction(instance, Target::kOpponentRearguard));
      }
    }
    if ((card.kind == CardKind::kEquipment || card.kind == CardKind::kResource) &&
        IsSupportedEquipment(card.name)) {
      if (jack_active) {
        result.push_back(PlayCardAction(instance, Target::kOwnActive));
      }
      result.push_back(PlayCardAction(instance, Target::kOwnRearguard));
      if (card.name != CardName::kDynamite || jack_active) {
        result.push_back(PlayCardAction(instance, Target::kOpponentActive));
      }
      if (jack_active) {
        const int opponent = OtherPlayer(decision_player_);
        const auto& opponent_state = players_[static_cast<std::size_t>(opponent)];
        const int opponent_rear = 1 - opponent_state.active_position;
        if (opponent_state.positions[static_cast<std::size_t>(opponent_rear)].life > 0) {
          result.push_back(PlayCardAction(instance, Target::kOpponentRearguard));
        }
      }
    }
    if (card.kind == CardKind::kAction && IsSupportedAction(card.name)) {
      if (card.name == CardName::kBeer) {
        result.push_back(PlayCardAction(instance, Target::kOwnActive));
      } else if (card.name == CardName::kStrongAle) {
        result.push_back(PlayCardAction(instance, Target::kOwnActive));
        if (player.positions[static_cast<std::size_t>(other_position)].life > 0) {
          result.push_back(PlayCardAction(instance, Target::kOwnRearguard));
        }
        result.push_back(PlayCardAction(instance, Target::kOpponentActive));
        if (jack_active) {
          const int opponent = OtherPlayer(decision_player_);
          const auto& opponent_state = players_[static_cast<std::size_t>(opponent)];
          const int opponent_rear = 1 - opponent_state.active_position;
          if (opponent_state.positions[static_cast<std::size_t>(opponent_rear)].life > 0) {
            result.push_back(PlayCardAction(instance, Target::kOpponentRearguard));
          }
        }
      } else if (card.name == CardName::kWater) {
        if (player.positions[static_cast<std::size_t>(other_position)].life > 0) {
          result.push_back(PlayCardAction(instance, Target::kOwnActive));
        }
        const int opponent = OtherPlayer(decision_player_);
        const auto& opponent_state = players_[static_cast<std::size_t>(opponent)];
        const int opponent_other = 1 - opponent_state.active_position;
        if (opponent_state.positions[static_cast<std::size_t>(opponent_other)].life > 0) {
          result.push_back(PlayCardAction(instance, Target::kOpponentActive));
        }
      } else if (card.name == CardName::kDerringer ||
                 card.name == CardName::kDuel || card.name == CardName::kKnife) {
        result.push_back(PlayCardAction(instance, Target::kOpponentActive));
        if (jack_active ||
            (card.has_hit &&
             HasActiveEquipment(decision_player_, CardName::kScope))) {
          const int opponent = OtherPlayer(decision_player_);
          const auto& opponent_state = players_[static_cast<std::size_t>(opponent)];
          const int opponent_rear = 1 - opponent_state.active_position;
          if (opponent_state.positions[static_cast<std::size_t>(opponent_rear)].life > 0) {
            result.push_back(PlayCardAction(instance, Target::kOpponentRearguard));
          }
        }
      } else if (card.name == CardName::kGatling ||
                 card.name == CardName::kGeneralStore ||
                 card.name == CardName::kIndians ||
                 card.name == CardName::kSchoolmarm ||
                 card.name == CardName::kStealing ||
                 card.name == CardName::kWagon) {
        result.push_back(PlayCardAction(instance, Target::kOwnActive));
      }
    }
    const bool transforms_as_colt =
        IsCharacterAbilityActive(decision_player_, CharacterName::kTocoRamirez) ||
        (IsCharacterAbilityActive(decision_player_, CharacterName::kAnnieOakey) &&
         card.name == CardName::kMissed);
    if (can_play_bang && transforms_as_colt) {
      result.push_back(PlayAsColtAction(instance, Target::kOpponentActive));
      const int opponent = OtherPlayer(decision_player_);
      const auto& opponent_state = players_[static_cast<std::size_t>(opponent)];
      const int opponent_rear = 1 - opponent_state.active_position;
      if (opponent_state.positions[static_cast<std::size_t>(opponent_rear)].life > 0 &&
          (jack_active || HasActiveEquipment(decision_player_, CardName::kScope))) {
        result.push_back(PlayAsColtAction(instance, Target::kOpponentRearguard));
      }
    }
    if (can_reload && card.reloadable) result.push_back(ReloadCardAction(instance));
  }
  if (can_reload) {
    const auto& active = player.positions[static_cast<std::size_t>(player.active_position)];
    for (const int instance : active.equipment) {
      if (GetCard(GetCardInstance(instance).name).reloadable) {
        result.push_back(ReloadCardAction(instance));
      }
    }
  }
  // Resources on the active character, from the turn after they entered
  // play. Hit Resources are not BANG! cards and ignore the BANG! limit.
  {
    const int opponent = OtherPlayer(decision_player_);
    const auto& opponent_state = players_[static_cast<std::size_t>(opponent)];
    const bool opponent_rear_alive =
        opponent_state.positions[static_cast<std::size_t>(1 - opponent_state.active_position)]
            .life > 0;
    const auto& active =
        player.positions[static_cast<std::size_t>(player.active_position)];
    for (const int instance : active.equipment) {
      if (!CanUseResource(decision_player_, instance)) continue;
      const CardName name = GetCardInstance(instance).name;
      const auto shoot = [&](bool may_target_rear) {
        result.push_back(UseResourceAction(instance, Target::kOpponentActive));
        if (may_target_rear && opponent_rear_alive) {
          result.push_back(UseResourceAction(instance, Target::kOpponentRearguard));
        }
      };
      if (name == CardName::kGoldPan) {
        if (!discard_.empty()) result.push_back(UseResourceAction(instance, Target::kOwnActive));
      } else if (name == CardName::kBuffaloRifle) {
        shoot(true);
      } else if (name == CardName::kPardner) {
        shoot(jack_active || HasActiveEquipment(decision_player_, CardName::kScope));
      }
    }
  }
  if (!ability_used_this_turn_[static_cast<std::size_t>(CharacterName::kBabeLeroy)] &&
      IsCharacterAbilityActive(decision_player_, CharacterName::kBabeLeroy) &&
      player.hand.empty()) {
    result.push_back(UseAbilityAction(CharacterName::kBabeLeroy));
  }
  if (!ability_used_this_turn_[static_cast<std::size_t>(CharacterName::kCattleKatie)] &&
      IsCharacterAbilityActive(decision_player_, CharacterName::kCattleKatie) &&
      !CattleKatieEquipment(decision_player_).empty()) {
    result.push_back(UseAbilityAction(CharacterName::kCattleKatie));
  }
  if (!ability_used_this_turn_[static_cast<std::size_t>(CharacterName::kSoundanceKid)] &&
      IsCharacterAbilityActive(decision_player_, CharacterName::kSoundanceKid) &&
      !player.hand.empty()) {
    result.push_back(UseAbilityAction(CharacterName::kSoundanceKid));
  }
  if (!ability_used_this_turn_[static_cast<std::size_t>(CharacterName::kPearlHat)] &&
      IsCharacterAbilityActive(decision_player_, CharacterName::kPearlHat) &&
      CanUsePearlHat()) {
    result.push_back(UseAbilityAction(CharacterName::kPearlHat));
  }
  return result;
}

void GameState::ApplyAction(int action_id) {
  const auto legal = LegalActions();
  if (std::find(legal.begin(), legal.end(), action_id) == legal.end()) {
    const std::string description =
        action_id >= 0 && action_id < kActionCount ? ActionToString(action_id)
                                                   : std::to_string(action_id);
    throw std::invalid_argument("illegal action: " + description);
  }
  const auto action = DecodeAction(action_id);

  if (action.kind == ActionKind::kChooseActive) {
    players_[static_cast<std::size_t>(decision_player_)].active_position = action.card_instance;
    active_chosen_[static_cast<std::size_t>(decision_player_)] = true;
    if (decision_player_ == PlayerIndex(Side::kLaw)) {
      decision_player_ = PlayerIndex(Side::kOutlaw);
    } else {
      decision_player_ = turn_player_ = PlayerIndex(Side::kLaw);
      RecordVisibleCharacters();
      StartTurn();
    }
    return;
  }
  if (action.kind == ActionKind::kChooseReveal) {
    FinishReveal(action.card_instance);
    return;
  }
  if (action.kind == ActionKind::kResolveOptional) {
    if (phase_ == Phase::kChooseAbility && pending_damage_ &&
        pending_ability_ ==
            static_cast<int>(CharacterName::kBuffaloBell)) {
      FinishBuffaloBell(action.card_instance == 1);
    } else if (phase_ == Phase::kChooseAbility &&
               !elimination_stack_.empty() &&
               (pending_ability_ ==
                    static_cast<int>(CharacterName::kBullAnderson) ||
                pending_ability_ ==
                    static_cast<int>(CharacterName::kBillTightman))) {
      if (pending_ability_ ==
          static_cast<int>(CharacterName::kBullAnderson)) {
        FinishBullAnderson();
      } else if (pending_ability_ ==
                 static_cast<int>(CharacterName::kBillTightman)) {
        FinishBillTightman(action.card_instance == 1);
      }
    } else if (phase_ == Phase::kChooseAbility &&
               !damage_reaction_stack_.empty() &&
               damage_reaction_stack_.back().character_id ==
                   pending_ability_) {
      FinishDamageReaction(action.card_instance == 1);
    } else if (phase_ == Phase::kChooseAbility &&
               pending_ability_ == static_cast<int>(CharacterName::kJango)) {
      FinishJango(action.card_instance == 1);
    } else if (phase_ == Phase::kChooseAbility &&
               pending_ability_ ==
                   static_cast<int>(CharacterName::kPatGarret)) {
      FinishPatGarret(action.card_instance == 1);
    } else if (phase_ == Phase::kChooseRemington) {
      FinishRemingtonChoice(action.card_instance == 1);
    } else if (phase_ == Phase::kChooseCarbine && action.card_instance == 0) {
      FinishCarbineChoice(-1);
    } else if (phase_ == Phase::kDuelResponse && action.card_instance == 0) {
      FinishDuelResponse(std::nullopt);
    } else if (phase_ == Phase::kChooseSchoolmarm && action.card_instance == 0) {
      FinishSchoolmarm(std::nullopt);
    } else {
      throw std::logic_error("optional action does not match pending effect");
    }
    return;
  }
  if (action.kind == ActionKind::kChooseSwap) {
    FinishShotgunChoice(action.card_instance == 1);
    return;
  }
  if (action.kind == ActionKind::kChooseCardBack) {
    if (phase_ == Phase::kChooseAbility &&
        !damage_reaction_stack_.empty() &&
        pending_ability_ ==
            static_cast<int>(CharacterName::kSlimPoet) &&
        damage_reaction_stack_.back().character_id ==
            static_cast<int>(CharacterName::kSlimPoet) &&
        damage_reaction_stack_.back().choosing_origin) {
      FinishSlimPoetHand(static_cast<Side>(action.card_instance));
    } else if (phase_ == Phase::kChooseAbility &&
               !elimination_stack_.empty() &&
        pending_ability_ ==
            static_cast<int>(CharacterName::kBullAnderson)) {
      FinishBullAndersonHand(static_cast<Side>(action.card_instance));
    } else {
      FinishStealingHand(static_cast<Side>(action.card_instance));
    }
    return;
  }
  if (action.kind == ActionKind::kSelectCard) {
    if (phase_ == Phase::kChooseGeneralStore) {
      FinishGeneralStorePick(action.card_instance);
    } else if (phase_ == Phase::kChooseSchoolmarm) {
      FinishSchoolmarm(action.card_instance);
    } else if (phase_ == Phase::kChooseStealing) {
      FinishStealingEquipment(action.card_instance);
    } else if (phase_ == Phase::kChooseAbility &&
               pending_ability_ == static_cast<int>(CharacterName::kCattleKatie)) {
      FinishCattleKatie(action.card_instance);
    } else if (phase_ == Phase::kChooseAbility &&
               !elimination_stack_.empty() &&
               pending_ability_ ==
                   static_cast<int>(CharacterName::kBullAnderson)) {
      FinishBullAndersonEquipment(action.card_instance);
    } else {
      throw std::logic_error("selected card does not match pending effect");
    }
    return;
  }
  if (action.kind == ActionKind::kPlayAsColt) {
    ResolveAsColt(action.card_instance, static_cast<Target>(action.target));
    return;
  }
  if (action.kind == ActionKind::kUseAbility) {
    const auto character = static_cast<CharacterName>(action.card_instance);
    if (phase_ == Phase::kRespondToHit &&
        character == CharacterName::kSidCurry) {
      BeginSidCurryReveal();
    } else {
      ResolveActivatedAbility(character);
    }
    return;
  }
  if (action.kind == ActionKind::kSwap) {
    SwapCharacters(decision_player_, true);
    return;
  }
  if (action.kind == ActionKind::kEndPlay) {
    if (static_cast<int>(players_[static_cast<std::size_t>(turn_player_)].hand.size()) >
        HandLimit(turn_player_)) {
      phase_ = Phase::kDiscard;
    } else {
      FinishTurn();
    }
    return;
  }
  if (action.kind == ActionKind::kDiscardCard) {
    if (phase_ == Phase::kChooseAbility &&
        pending_ability_ == static_cast<int>(CharacterName::kPearlHat)) {
      FinishPearlHatCost(action.card_instance);
      return;
    }
    if (phase_ == Phase::kChooseAbility &&
        pending_ability_ == static_cast<int>(CharacterName::kSoundanceKid)) {
      FinishSoundanceKid(action.card_instance);
      return;
    }
    if (phase_ == Phase::kDuelResponse) {
      FinishDuelResponse(action.card_instance);
      return;
    }
    if (phase_ == Phase::kChooseWagonDiscard) {
      FinishWagon(action.card_instance);
      return;
    }
    if (phase_ == Phase::kForcedDiscard) {
      FinishForcedDiscard(action.card_instance);
      return;
    }
    if (phase_ == Phase::kChooseCarbine) {
      FinishCarbineChoice(action.card_instance);
      return;
    }
    DiscardFromHand(decision_player_, action.card_instance);
    if (static_cast<int>(players_[static_cast<std::size_t>(decision_player_)].hand.size()) <=
        HandLimit(decision_player_)) {
      FinishTurn();
    }
    return;
  }
  if (action.kind == ActionKind::kReloadCard) {
    if (HasCardInHand(decision_player_, action.card_instance)) {
      DiscardFromHand(decision_player_, action.card_instance);
    } else {
      auto& state = players_[static_cast<std::size_t>(decision_player_)];
      RemoveEquipment(decision_player_, state.active_position, action.card_instance);
    }
    DrawOne(decision_player_);
    return;
  }
  if (action.kind == ActionKind::kPlayCard) {
    if (phase_ == Phase::kChooseAbility &&
        pending_ability_ == static_cast<int>(CharacterName::kPearlHat)) {
      FinishPearlHatEffect(action.card_instance,
                           static_cast<Target>(action.target));
      return;
    }
    const auto& card = GetCard(GetCardInstance(action.card_instance).name);
    if (card.kind == CardKind::kEquipment || card.kind == CardKind::kResource) {
      PlaceEquipment(decision_player_, action.card_instance, static_cast<Target>(action.target));
    } else if (card.kind == CardKind::kBang) {
      ResolveBang(action.card_instance, static_cast<Target>(action.target));
    } else {
      ResolveAction(action.card_instance, static_cast<Target>(action.target));
    }
    return;
  }
  if (action.kind == ActionKind::kAvoidHit) {
    if (HasCardInHand(decision_player_, action.card_instance)) {
      const bool additional_tex_payment =
          pending_hit_ && pending_hit_->avoids_played > 0;
      const CardName name = GetCardInstance(action.card_instance).name;
      if (!additional_tex_payment && name == CardName::kReturnFire) {
        ResolveReturnFire(action.card_instance);
      } else {
        DiscardFromHand(decision_player_, action.card_instance);
        if (!additional_tex_payment && name == CardName::kAgility) {
          DrawOne(decision_player_);
        }
        if (!additional_tex_payment && name == CardName::kFeint && pending_hit_) {
          pending_hit_->feint = true;
        }
        RecordAvoid(false);
      }
    } else {
      if (!pending_hit_) throw std::logic_error("avoid action has no pending hit");
      const CardName name = GetCardInstance(action.card_instance).name;
      if (name == CardName::kHat) {
        RemoveEquipment(pending_hit_->target, pending_hit_->target_position,
                        action.card_instance);
        RecordAvoid(false);
      } else if (name == CardName::kBarrel) {
        BeginBarrelReveal(action.card_instance);
      } else if (GetCard(name).kind == CardKind::kResource) {
        const int owner = pending_hit_->target;
        RemoveEquipment(owner, pending_hit_->target_position, action.card_instance);
        // Lucky Penny's card comes immediately, so it can answer later hits.
        if (name == CardName::kLuckyPenny) DrawOne(owner);
        RecordAvoid(false);
      } else {
        throw std::logic_error("unsupported equipment avoid action");
      }
    }
    return;
  }
  if (action.kind == ActionKind::kTakeHit) {
    ResolveHit();
    return;
  }
  if (action.kind == ActionKind::kUseResource) {
    UseResource(action.card_instance, static_cast<Target>(action.target));
    return;
  }
  throw std::logic_error("unhandled legal action");
}

void GameState::StartTurn() {
  decision_player_ = turn_player_;
  swapped_this_turn_ = false;
  bang_cards_played_ = 0;
  ability_used_this_turn_.fill(false);
  extra_bang_allowance_ = 0;
  phase_ = Phase::kPlay;
  start_turn_step_ = StartTurnStep::kDynamite;
  AdvanceStartTurn();
}

void GameState::AdvanceStartTurn() {
  while (!IsTerminal()) {
    if (start_turn_step_ == StartTurnStep::kDynamite) {
      start_turn_step_ = StartTurnStep::kRattlesnake;
      const auto dynamite = FindEquipment(turn_player_, CardName::kDynamite);
      if (dynamite) {
        BeginReveal({RevealReason::kDynamite, turn_player_, dynamite->first,
                     dynamite->second});
        return;
      }
      continue;
    }
    if (start_turn_step_ == StartTurnStep::kRattlesnake) {
      start_turn_step_ = StartTurnStep::kStampede;
      const auto rattlesnake = FindEquipment(turn_player_, CardName::kRattlesnake);
      if (rattlesnake) {
        BeginReveal({RevealReason::kRattlesnake, turn_player_, rattlesnake->first,
                     rattlesnake->second});
        return;
      }
      continue;
    }
    if (start_turn_step_ == StartTurnStep::kStampede) {
      start_turn_step_ = StartTurnStep::kDraw;
      auto& player = players_[static_cast<std::size_t>(turn_player_)];
      const bool has_two_in_play =
          player.positions[0].life > 0 && player.positions[1].life > 0;
      if (has_two_in_play && FindEquipment(turn_player_, CardName::kStampede)) {
        if (SwapCharacters(turn_player_, false, ResumeKind::kStartTurn)) return;
      }
      continue;
    }
    if (start_turn_step_ == StartTurnStep::kDraw) {
      start_turn_step_ = StartTurnStep::kNone;
      const int draw_count = 2 +
                             (HasActiveEquipment(turn_player_, CardName::kBank) ? 1 : 0) +
                             (IsCharacterAbilityActive(
                                  turn_player_, CharacterName::kDaltonBros)
                                  ? 1
                                  : 0);
      DrawTurnCards(draw_count);
      return;
    }
    throw std::logic_error("invalid beginning-of-turn continuation");
  }
}

void GameState::FinishTurn() {
  turn_player_ = OtherPlayer(turn_player_);
  ++turn_number_;
  StartTurn();
}

void GameState::DrawCards(int player, int count) {
  for (int i = 0; i < count; ++i) DrawOne(player);
}

void GameState::DrawOne(int player) {
  const auto card = TakeTopCard(player);
  if (card) players_[static_cast<std::size_t>(player)].hand.push_back(*card);
}

void GameState::DrawTurnCards(int count) {
  if (count <= 0) {
    decision_player_ = turn_player_;
    phase_ = Phase::kPlay;
    return;
  }
  DrawOne(turn_player_);
  if (count == 1) {
    decision_player_ = turn_player_;
    phase_ = Phase::kPlay;
    return;
  }
  const auto second = TakeTopCard(turn_player_);
  if (second) {
    players_[static_cast<std::size_t>(turn_player_)].hand.push_back(*second);
    if (IsCharacterAbilityActive(turn_player_, CharacterName::kPatGarret)) {
      MarkHandCardPublic(turn_player_, *second);
    }
  }
  pending_draw_count_ = count - 2;
  if (second &&
      IsCharacterAbilityActive(turn_player_, CharacterName::kPatGarret) &&
      GetCardInstance(*second).reveal == RevealSymbol::kHorseshoe) {
    pending_ability_ = static_cast<int>(CharacterName::kPatGarret);
    ability_stage_ = pending_draw_count_;
    decision_player_ = turn_player_;
    phase_ = Phase::kChooseAbility;
    return;
  }
  DrawCards(turn_player_, pending_draw_count_);
  pending_draw_count_ = 0;
  decision_player_ = turn_player_;
  phase_ = Phase::kPlay;
}

void GameState::FinishPatGarret(bool draw) {
  if (phase_ != Phase::kChooseAbility ||
      pending_ability_ != static_cast<int>(CharacterName::kPatGarret)) {
    throw std::logic_error("there is no pending Pat Garret choice");
  }
  if (draw) {
    const int opponent = OtherPlayer(turn_player_);
    const auto card = TakeTopCard(opponent);
    if (card) players_[static_cast<std::size_t>(turn_player_)].hand.push_back(*card);
  }
  DrawCards(turn_player_, pending_draw_count_);
  pending_draw_count_ = 0;
  pending_ability_ = -1;
  ability_stage_ = 0;
  decision_player_ = turn_player_;
  phase_ = Phase::kPlay;
}

std::optional<int> GameState::TakeTopCard(int player) {
  auto& state = players_[static_cast<std::size_t>(player)];
  if (!state.personal_deck_exhausted && !state.deck.empty()) {
    const int card = state.deck.back();
    state.deck.pop_back();
    return card;
  }
  if (!state.personal_deck_exhausted) {
    state.personal_deck_exhausted = true;
    if (!common_deck_created_) RefillCommonDeck();
  }
  if (common_deck_.empty()) RefillCommonDeck();
  if (!common_deck_.empty()) {
    const int card = common_deck_.back();
    common_deck_.pop_back();
    return card;
  }
  return std::nullopt;
}

std::optional<int> GameState::TakeTopCommonCard() {
  if (!common_deck_created_) {
    throw std::logic_error("common deck has not been created");
  }
  if (common_deck_.empty()) RefillCommonDeck();
  if (common_deck_.empty()) return std::nullopt;
  const int card = common_deck_.back();
  common_deck_.pop_back();
  return card;
}

void GameState::RefillCommonDeck() {
  common_deck_created_ = true;
  if (discard_.empty()) return;
  common_deck_ = std::move(discard_);
  discard_.clear();
  rng_.Shuffle(common_deck_);
}

void GameState::DiscardFromHand(int player, int card_instance) {
  auto& hand = players_[static_cast<std::size_t>(player)].hand;
  const auto found = std::find(hand.begin(), hand.end(), card_instance);
  if (found == hand.end()) throw std::logic_error("card is not in player's hand");
  discard_.push_back(*found);
  hand.erase(found);
  ForgetPublicHandCard(player, card_instance);
}

void GameState::MarkHandCardPublic(int player, int card_instance) {
  auto& known = public_hand_cards_[static_cast<std::size_t>(player)];
  if (std::find(known.begin(), known.end(), card_instance) == known.end()) {
    known.push_back(card_instance);
  }
}

void GameState::ForgetPublicHandCard(int player, int card_instance) {
  auto& known = public_hand_cards_[static_cast<std::size_t>(player)];
  const auto found = std::find(known.begin(), known.end(), card_instance);
  if (found != known.end()) known.erase(found);
}

bool GameState::SwapCharacters(int player, bool voluntary, ResumeKind resume_kind) {
  if (movement_mover_ >= 0 || !movement_triggers_.empty()) {
    throw std::logic_error("another movement is already resolving");
  }
  auto& state = players_[static_cast<std::size_t>(player)];
  const int destination = 1 - state.active_position;
  if (state.positions[static_cast<std::size_t>(destination)].life <= 0) {
    throw std::logic_error("cannot swap with an empty position");
  }
  state.active_position = destination;
  if (voluntary) swapped_this_turn_ = true;
  const int opposing_player = OtherPlayer(player);
  const bool tom_triggers =
      IsCharacterAbilityActive(player, CharacterName::kTomThorn);
  const bool clock_triggers =
      HasActiveEquipment(opposing_player, CardName::kClock);
  const bool jango_triggers =
      IsCharacterAbilityActive(opposing_player, CharacterName::kJango);

  movement_mover_ = player;
  movement_resume_kind_ = resume_kind;
  std::vector<MovementTrigger> ordered;
  if (player == turn_player_) {
    if (tom_triggers) ordered.push_back(MovementTrigger::kTomThorn);
    if (clock_triggers) ordered.push_back(MovementTrigger::kClock);
    if (jango_triggers) ordered.push_back(MovementTrigger::kJango);
  } else {
    if (clock_triggers) ordered.push_back(MovementTrigger::kClock);
    if (jango_triggers) ordered.push_back(MovementTrigger::kJango);
    if (tom_triggers) ordered.push_back(MovementTrigger::kTomThorn);
  }
  std::reverse(ordered.begin(), ordered.end());
  movement_triggers_ = std::move(ordered);
  return AdvanceMovementTriggers(false);
}

bool GameState::AdvanceMovementTriggers(bool resume_when_complete) {
  if (movement_mover_ < 0) {
    throw std::logic_error("there is no movement to continue");
  }
  while (!movement_triggers_.empty()) {
    const MovementTrigger trigger = movement_triggers_.back();
    movement_triggers_.pop_back();
    const int opposing_player = OtherPlayer(movement_mover_);
    if (trigger == MovementTrigger::kClock) {
      DrawOne(opposing_player);
      continue;
    }
    if (trigger == MovementTrigger::kJango) {
      pending_ability_ = static_cast<int>(CharacterName::kJango);
      ability_stage_ = 0;
      decision_player_ = opposing_player;
      phase_ = Phase::kChooseAbility;
      return true;
    }

    const auto [target_player, target_position] =
        ResolveTarget(movement_mover_, Target::kOpponentRearguard);
    PendingHit hit{movement_mover_, target_player, target_position, turn_player_,
                   CardName::kColt, false};
    hit.resume_kind = ResumeKind::kMovement;
    hit.source_character = static_cast<int>(CharacterName::kTomThorn);
    ActivatePendingHit(hit);
    return true;
  }

  if (resume_when_complete) {
    ResumeAfterMovement();
  } else {
    movement_mover_ = -1;
    movement_resume_kind_ = ResumeKind::kPlay;
  }
  return false;
}

void GameState::ResumeAfterMovement() {
  if (movement_mover_ < 0 || !movement_triggers_.empty()) {
    throw std::logic_error("movement is not ready to resume");
  }
  const ResumeKind resume_kind = movement_resume_kind_;
  movement_mover_ = -1;
  movement_resume_kind_ = ResumeKind::kPlay;
  if (resume_kind == ResumeKind::kStartTurn) {
    AdvanceStartTurn();
  } else if (resume_kind == ResumeKind::kPendingEffect) {
    ResumeAfterPendingEffect();
  } else {
    decision_player_ = turn_player_;
    phase_ = Phase::kPlay;
  }
}

int GameState::LivingCharacterCount(int player) const {
  const auto& state = players_[static_cast<std::size_t>(player)];
  int result = static_cast<int>(state.reserve.size());
  for (const auto& character : state.positions) {
    if (character.life > 0) ++result;
  }
  return result;
}

int GameState::HandLimit(int player) const {
  const auto& state = players_[static_cast<std::size_t>(player)];
  const int life = state.positions[static_cast<std::size_t>(state.active_position)].life;
  return life == 1 ? 2 : life;
}

bool GameState::HasCardInHand(int player, int card_instance) const {
  const auto& hand = players_[static_cast<std::size_t>(player)].hand;
  return std::find(hand.begin(), hand.end(), card_instance) != hand.end();
}

bool GameState::HasEquipment(int player, int position, CardName name) const {
  const auto& equipment = players_[static_cast<std::size_t>(player)]
                              .positions[static_cast<std::size_t>(position)]
                              .equipment;
  return std::any_of(equipment.begin(), equipment.end(), [name](int instance) {
    return GetCardInstance(instance).name == name;
  });
}

bool GameState::HasActiveEquipment(int player, CardName name) const {
  const auto& state = players_[static_cast<std::size_t>(player)];
  return HasEquipment(player, state.active_position, name);
}

bool GameState::TargetHasAbility(int player, int position, CharacterName name) const {
  const auto& character = players_[static_cast<std::size_t>(player)]
                              .positions[static_cast<std::size_t>(position)];
  return character.life > 0 && character.character_id == static_cast<int>(name);
}

bool GameState::IsCharacterAbilityActive(int player, CharacterName name) const {
  const auto& state = players_[static_cast<std::size_t>(player)];
  const int character_id = static_cast<int>(name);
  for (int position = 0; position < 2; ++position) {
    const auto& character = state.positions[static_cast<std::size_t>(position)];
    if (character.life <= 0 || character.character_id != character_id) continue;
    return position == state.active_position || GetCharacter(character_id).always_active;
  }
  return false;
}

int GameState::LivingCharacterPosition(int player,
                                       CharacterName name) const {
  const auto& state = players_[static_cast<std::size_t>(player)];
  for (int position = 0; position < 2; ++position) {
    const auto& character =
        state.positions[static_cast<std::size_t>(position)];
    if (character.life > 0 &&
        character.character_id == static_cast<int>(name)) {
      return position;
    }
  }
  return -1;
}

bool GameState::CanProvideHit(int player, int position,
                              int card_instance) const {
  const auto& card = GetCard(GetCardInstance(card_instance).name);
  if (card.has_hit) return true;
  return TargetHasAbility(player, position, CharacterName::kTocoRamirez) ||
         (TargetHasAbility(player, position, CharacterName::kAnnieOakey) &&
          card.name == CardName::kMissed);
}

int GameState::BangCardAllowance(int player) const {
  if (IsCharacterAbilityActive(player, CharacterName::kWestHarding)) {
    return kCardInstanceCount;
  }
  return (IsCharacterAbilityActive(player, CharacterName::kTheStranger) ? 2 : 1) +
         extra_bang_allowance_;
}

std::vector<int> GameState::CattleKatieEquipment(int player) const {
  if (!IsCharacterAbilityActive(player, CharacterName::kCattleKatie)) return {};
  const auto& state = players_[static_cast<std::size_t>(player)];
  const auto& equipment =
      state.positions[static_cast<std::size_t>(state.active_position)].equipment;
  std::vector<int> result;
  for (const int instance : equipment) {
    if (!GetCard(GetCardInstance(instance).name).always_active) {
      result.push_back(instance);
    }
  }
  return result;
}

std::optional<std::pair<int, int>> GameState::FindEquipment(int player,
                                                            CardName name) const {
  const auto& state = players_[static_cast<std::size_t>(player)];
  for (int position = 0; position < 2; ++position) {
    const auto& equipment = state.positions[static_cast<std::size_t>(position)].equipment;
    const auto found = std::find_if(equipment.begin(), equipment.end(), [name](int instance) {
      return GetCardInstance(instance).name == name;
    });
    if (found != equipment.end()) return std::pair<int, int>{position, *found};
  }
  return std::nullopt;
}

std::pair<int, int> GameState::ResolveTarget(int actor, Target target) const {
  const bool opponent_target =
      target == Target::kOpponentActive || target == Target::kOpponentRearguard;
  const int player = opponent_target ? OtherPlayer(actor) : actor;
  const auto& state = players_[static_cast<std::size_t>(player)];
  const bool rear_target =
      target == Target::kOwnRearguard || target == Target::kOpponentRearguard;
  int position = state.active_position;
  const int other = 1 - position;
  if (rear_target && state.positions[static_cast<std::size_t>(other)].life > 0) {
    position = other;
  }
  return {player, position};
}

bool GameState::IsSupportedEquipment(CardName name) const {
  if (IsSupportedResource(name)) return true;
  switch (name) {
    case CardName::kBank:
    case CardName::kBarrel:
    case CardName::kClock:
    case CardName::kGunBelt:
    case CardName::kHat:
    case CardName::kDynamite:
    case CardName::kRattlesnake:
    case CardName::kScope:
    case CardName::kStampede: return true;
    default: return false;
  }
}

bool GameState::IsSupportedBang(CardName name) const {
  return name == CardName::kCarbine || name == CardName::kColt ||
         name == CardName::kFanning || name == CardName::kPeacemaker ||
         name == CardName::kRemington ||
         name == CardName::kSchofield || name == CardName::kShotgun ||
         name == CardName::kWinchester ||
         // Renegades
         name == CardName::kLightning || name == CardName::kSavage ||
         name == CardName::kSpencer || name == CardName::kWalker ||
         name == CardName::kWhitney;
}

bool GameState::IsSupportedAction(CardName name) const {
  return name == CardName::kBeer || name == CardName::kDerringer ||
         name == CardName::kDuel || name == CardName::kGatling ||
         name == CardName::kGeneralStore || name == CardName::kIndians ||
         name == CardName::kKnife || name == CardName::kSchoolmarm ||
         name == CardName::kStealing || name == CardName::kStrongAle ||
         name == CardName::kWagon || name == CardName::kWater;
}

void GameState::PlaceEquipment(int actor, int card_instance, Target target) {
  if (!HasCardInHand(actor, card_instance)) {
    throw std::logic_error("equipment is not in player's hand");
  }
  const CardName name = GetCardInstance(card_instance).name;
  if (!IsSupportedEquipment(name)) throw std::logic_error("equipment is not implemented");
  const auto [target_player, target_position] = ResolveTarget(actor, target);
  auto& equipment = players_[static_cast<std::size_t>(target_player)]
                        .positions[static_cast<std::size_t>(target_position)]
                        .equipment;
  const auto duplicate = std::find_if(equipment.begin(), equipment.end(), [name](int instance) {
    return GetCardInstance(instance).name == name;
  });
  const bool replaced = duplicate != equipment.end();
  if (duplicate != equipment.end()) {
    discard_.push_back(*duplicate);
    equipment.erase(duplicate);
  }
  auto& hand = players_[static_cast<std::size_t>(actor)].hand;
  const auto card = std::find(hand.begin(), hand.end(), card_instance);
  if (card == hand.end()) throw std::logic_error("equipment vanished from hand");
  hand.erase(card);
  ForgetPublicHandCard(actor, card_instance);
  equipment.push_back(card_instance);
  entered_play_turn_[static_cast<std::size_t>(card_instance)] = turn_number_;
  if (replaced) {
    BeginWildBillReaction(target_player, actor, ResumeKind::kPlay,
                          target_position);
  }
}

bool GameState::IsSupportedResource(CardName name) const {
  return name == CardName::kBuffaloRifle || name == CardName::kGoldPan ||
         name == CardName::kLuckyPenny || name == CardName::kPardner;
}

bool GameState::CanUseResource(int player, int card_instance) const {
  const auto& card = GetCardInstance(card_instance);
  if (GetCard(card.name).kind != CardKind::kResource || !IsSupportedResource(card.name)) {
    return false;
  }
  if (entered_play_turn_[static_cast<std::size_t>(card_instance)] == turn_number_) {
    return false;
  }
  // Like blue Equipment, a Resource works only on the active character.
  const auto& state = players_[static_cast<std::size_t>(player)];
  const auto& active = state.positions[static_cast<std::size_t>(state.active_position)];
  return active.life > 0 &&
         std::find(active.equipment.begin(), active.equipment.end(), card_instance) !=
             active.equipment.end();
}

void GameState::UseResource(int card_instance, Target target) {
  if (!CanUseResource(turn_player_, card_instance)) {
    throw std::logic_error("resource is not usable");
  }
  const CardName name = GetCardInstance(card_instance).name;
  auto& state = players_[static_cast<std::size_t>(turn_player_)];
  if (name == CardName::kGoldPan) {
    // The top discard is taken before the Gold Pan itself is discarded.
    if (discard_.empty()) throw std::logic_error("Gold Pan needs a discard pile");
    const int top = discard_.back();
    discard_.pop_back();
    state.hand.push_back(top);
    MarkHandCardPublic(turn_player_, top);
    RemoveEquipment(turn_player_, state.active_position, card_instance);
    return;
  }
  RemoveEquipment(turn_player_, state.active_position, card_instance);
  if (name == CardName::kBuffaloRifle) {
    // Drawn immediately, before the opponent reacts.
    DrawOne(turn_player_);
  }
  if (name == CardName::kBuffaloRifle || name == CardName::kPardner) {
    const auto [target_player, target_position] = ResolveTarget(turn_player_, target);
    pending_hit_ = PendingHit{turn_player_, target_player, target_position, turn_player_,
                              name, false, ResponseKind::kAvoid, 1};
    decision_player_ = target_player;
    phase_ = Phase::kRespondToHit;
    return;
  }
  throw std::logic_error("unsupported resource");
}

void GameState::RemoveEquipment(int player, int position, int card_instance) {
  auto& equipment = players_[static_cast<std::size_t>(player)]
                        .positions[static_cast<std::size_t>(position)]
                        .equipment;
  const auto found = std::find(equipment.begin(), equipment.end(), card_instance);
  if (found == equipment.end()) throw std::logic_error("equipment is not in play");
  discard_.push_back(*found);
  equipment.erase(found);
}

void GameState::ResolveBang(int card_instance, Target target) {
  const CardName name = GetCardInstance(card_instance).name;
  if (!HasCardInHand(turn_player_, card_instance) || !IsSupportedBang(name)) {
    throw std::logic_error("play action is not a supported BANG card in hand");
  }
  DiscardFromHand(turn_player_, card_instance);
  if (!HasActiveEquipment(turn_player_, CardName::kGunBelt)) ++bang_cards_played_;
  // Lightning allows one more BANG! card later this turn.
  if (name == CardName::kLightning) ++extra_bang_allowance_;
  // Savage's card is drawn before the opponent reacts.
  if (name == CardName::kSavage) DrawOne(turn_player_);
  const auto [target_player, target_position] = ResolveTarget(turn_player_, target);
  const int avoids_required =
      IsCharacterAbilityActive(turn_player_, CharacterName::kTexKiller) ? 2 : 1;
  if (name == CardName::kFanning) {
    const auto& equipment = players_[static_cast<std::size_t>(target_player)]
                                .positions[static_cast<std::size_t>(target_position)]
                                .equipment;
    std::vector<PendingHit> hits;
    hits.reserve(equipment.size());
    for (std::size_t index = 0; index < equipment.size(); ++index) {
      hits.push_back({turn_player_, target_player, target_position, turn_player_, name,
                      false, ResponseKind::kAvoid, avoids_required});
      hits.back().source_instance = card_instance;
    }
    BeginHitSequence(std::move(hits), turn_player_);
    return;
  }
  // Whitney can only be avoided with a hit card, like answering Indians!.
  const ResponseKind response =
      name == CardName::kWhitney ? ResponseKind::kHitCard : ResponseKind::kAvoid;
  pending_hit_ =
      PendingHit{turn_player_, target_player, target_position, turn_player_, name,
                 false, response, avoids_required};
  pending_hit_->source_instance = card_instance;
  decision_player_ = target_player;
  phase_ = Phase::kRespondToHit;
}

void GameState::ResolveAsColt(int card_instance, Target target) {
  if (!HasCardInHand(turn_player_, card_instance)) {
    throw std::logic_error("transformed Colt card is not in hand");
  }
  const CardName original = GetCardInstance(card_instance).name;
  const bool allowed =
      IsCharacterAbilityActive(turn_player_, CharacterName::kTocoRamirez) ||
      (IsCharacterAbilityActive(turn_player_, CharacterName::kAnnieOakey) &&
       original == CardName::kMissed);
  if (!allowed) throw std::logic_error("no active ability can transform this card");
  DiscardFromHand(turn_player_, card_instance);
  if (!HasActiveEquipment(turn_player_, CardName::kGunBelt)) ++bang_cards_played_;
  const auto [target_player, target_position] = ResolveTarget(turn_player_, target);
  pending_hit_ = PendingHit{turn_player_, target_player, target_position,
                            turn_player_, CardName::kColt, false};
  decision_player_ = target_player;
  phase_ = Phase::kRespondToHit;
}

void GameState::ResolveActivatedAbility(CharacterName character) {
  const auto index = static_cast<std::size_t>(character);
  if (index >= ability_used_this_turn_.size() || ability_used_this_turn_[index] ||
      !IsCharacterAbilityActive(turn_player_, character)) {
    throw std::logic_error("character ability is not available");
  }
  if (character == CharacterName::kBabeLeroy) {
    if (!players_[static_cast<std::size_t>(turn_player_)].hand.empty()) {
      throw std::logic_error("Babe Leroy requires an empty hand");
    }
    ability_used_this_turn_[index] = true;
    DrawCards(turn_player_, 2);
    return;
  }
  if (character == CharacterName::kCattleKatie) {
    if (CattleKatieEquipment(turn_player_).empty()) {
      throw std::logic_error("Cattle Katie has no discardable Equipment");
    }
    decision_player_ = turn_player_;
    pending_ability_ = static_cast<int>(character);
    ability_stage_ = 0;
    phase_ = Phase::kChooseAbility;
    return;
  }
  if (character == CharacterName::kSoundanceKid) {
    if (players_[static_cast<std::size_t>(turn_player_)].hand.empty()) {
      throw std::logic_error("Soundance Kid requires a card to discard");
    }
    decision_player_ = turn_player_;
    pending_ability_ = static_cast<int>(character);
    ability_stage_ = 0;
    choice_cards_.clear();
    choice_cards_public_ = false;
    phase_ = Phase::kChooseAbility;
    return;
  }
  if (character == CharacterName::kPearlHat) {
    if (!CanUsePearlHat()) {
      throw std::logic_error("Pearl Hat has no replicable Action");
    }
    decision_player_ = turn_player_;
    pending_ability_ = static_cast<int>(character);
    pending_replicated_card_ = discard_.back();
    ability_stage_ = 0;
    phase_ = Phase::kChooseAbility;
    return;
  }
  throw std::logic_error("activated character ability is not implemented");
}

void GameState::FinishCattleKatie(int equipment_instance) {
  const auto equipment = CattleKatieEquipment(decision_player_);
  if (phase_ != Phase::kChooseAbility ||
      pending_ability_ != static_cast<int>(CharacterName::kCattleKatie) ||
      std::find(equipment.begin(), equipment.end(), equipment_instance) ==
          equipment.end()) {
    throw std::logic_error("invalid Cattle Katie Equipment choice");
  }
  const int position = players_[static_cast<std::size_t>(decision_player_)].active_position;
  RemoveEquipment(decision_player_, position, equipment_instance);
  DrawCards(decision_player_, 2);
  CompleteActivatedAbility(CharacterName::kCattleKatie);
}

void GameState::FinishSoundanceKid(int card_instance) {
  if (phase_ != Phase::kChooseAbility ||
      pending_ability_ != static_cast<int>(CharacterName::kSoundanceKid)) {
    throw std::logic_error("there is no pending Soundance Kid choice");
  }
  if (ability_stage_ == 0) {
    DiscardFromHand(decision_player_, card_instance);
    choice_cards_.clear();
    if (common_deck_created_) {
      for (int index = 0; index < 2; ++index) {
        const auto card = TakeTopCommonCard();
        if (card) choice_cards_.push_back(*card);
      }
    } else {
      const auto own = TakeTopCard(decision_player_);
      if (own) choice_cards_.push_back(*own);
      const auto opposing = TakeTopCard(OtherPlayer(decision_player_));
      if (opposing) choice_cards_.push_back(*opposing);
    }
    if (choice_cards_.empty()) {
      CompleteActivatedAbility(CharacterName::kSoundanceKid);
      return;
    }
    ability_stage_ = 1;
    return;
  }
  const auto selected = std::find(choice_cards_.begin(), choice_cards_.end(), card_instance);
  if (selected == choice_cards_.end()) {
    throw std::logic_error("Soundance Kid discard is not a drawn card");
  }
  discard_.push_back(*selected);
  choice_cards_.erase(selected);
  auto& hand = players_[static_cast<std::size_t>(decision_player_)].hand;
  hand.insert(hand.end(), choice_cards_.begin(), choice_cards_.end());
  CompleteActivatedAbility(CharacterName::kSoundanceKid);
}

std::vector<int> GameState::PearlHatActionsFor(int source_instance) const {
  if (source_instance < 0 || source_instance >= kCardInstanceCount) return {};
  const auto& card = GetCard(GetCardInstance(source_instance).name);
  if (card.kind != CardKind::kAction || !IsSupportedAction(card.name)) return {};

  std::vector<int> actions;
  const auto append = [&](Target target) {
    actions.push_back(PlayCardAction(source_instance, target));
  };
  const auto& player = players_[static_cast<std::size_t>(turn_player_)];
  const int rear = 1 - player.active_position;
  const int opponent = OtherPlayer(turn_player_);
  const auto& opponent_state = players_[static_cast<std::size_t>(opponent)];
  const int opponent_rear = 1 - opponent_state.active_position;
  const bool jack_active =
      IsCharacterAbilityActive(turn_player_, CharacterName::kJackRansome);

  if (card.name == CardName::kBeer) {
    append(Target::kOwnActive);
  } else if (card.name == CardName::kStrongAle) {
    append(Target::kOwnActive);
    if (player.positions[static_cast<std::size_t>(rear)].life > 0) {
      append(Target::kOwnRearguard);
    }
    append(Target::kOpponentActive);
    if (jack_active &&
        opponent_state.positions[static_cast<std::size_t>(opponent_rear)].life > 0) {
      append(Target::kOpponentRearguard);
    }
  } else if (card.name == CardName::kWater) {
    if (player.positions[static_cast<std::size_t>(rear)].life > 0) {
      append(Target::kOwnActive);
    }
    if (opponent_state.positions[static_cast<std::size_t>(opponent_rear)].life > 0) {
      append(Target::kOpponentActive);
    }
  } else if (card.name == CardName::kDerringer ||
             card.name == CardName::kDuel ||
             card.name == CardName::kKnife) {
    append(Target::kOpponentActive);
    if ((jack_active ||
         (card.has_hit &&
          HasActiveEquipment(turn_player_, CardName::kScope))) &&
        opponent_state.positions[static_cast<std::size_t>(opponent_rear)].life > 0) {
      append(Target::kOpponentRearguard);
    }
  } else {
    append(Target::kOwnActive);
  }
  return actions;
}

std::vector<int> GameState::PearlHatActions() const {
  if (pending_ability_ != static_cast<int>(CharacterName::kPearlHat) ||
      ability_stage_ != 1) {
    return {};
  }
  return PearlHatActionsFor(pending_replicated_card_);
}

bool GameState::CanUsePearlHat() const {
  if (players_[static_cast<std::size_t>(turn_player_)].hand.empty() ||
      discard_.empty()) {
    return false;
  }
  return !PearlHatActionsFor(discard_.back()).empty();
}

void GameState::FinishPearlHatCost(int card_instance) {
  if (phase_ != Phase::kChooseAbility ||
      pending_ability_ != static_cast<int>(CharacterName::kPearlHat) ||
      ability_stage_ != 0 ||
      !HasCardInHand(decision_player_, card_instance)) {
    throw std::logic_error("invalid Pearl Hat cost");
  }
  DiscardFromHand(decision_player_, card_instance);
  ability_stage_ = 1;
}

void GameState::FinishPearlHatEffect(int source_instance, Target target) {
  if (phase_ != Phase::kChooseAbility ||
      pending_ability_ != static_cast<int>(CharacterName::kPearlHat) ||
      ability_stage_ != 1 || source_instance != pending_replicated_card_) {
    throw std::logic_error("invalid Pearl Hat replicated Action");
  }
  const int action = PlayCardAction(source_instance, target);
  const auto legal = PearlHatActions();
  if (std::find(legal.begin(), legal.end(), action) == legal.end()) {
    throw std::logic_error("invalid target for Pearl Hat replicated Action");
  }
  const CardName name = GetCardInstance(source_instance).name;
  ability_used_this_turn_[static_cast<std::size_t>(CharacterName::kPearlHat)] =
      true;
  pending_ability_ = -1;
  pending_replicated_card_ = -1;
  ability_stage_ = 0;
  decision_player_ = turn_player_;
  phase_ = Phase::kPlay;
  ResolveActionEffect(name, target);
}

void GameState::FinishJango(bool draw) {
  if (phase_ != Phase::kChooseAbility ||
      pending_ability_ != static_cast<int>(CharacterName::kJango) ||
      movement_mover_ < 0) {
    throw std::logic_error("there is no pending Jango choice");
  }
  const int jango_player = decision_player_;
  pending_ability_ = -1;
  ability_stage_ = 0;
  if (draw) DrawOne(jango_player);
  AdvanceMovementTriggers(true);
}

void GameState::CompleteActivatedAbility(CharacterName character) {
  ability_used_this_turn_[static_cast<std::size_t>(character)] = true;
  pending_ability_ = -1;
  pending_replicated_card_ = -1;
  ability_stage_ = 0;
  choice_cards_.clear();
  choice_cards_public_ = false;
  decision_player_ = turn_player_;
  phase_ = Phase::kPlay;
}

void GameState::ResolveAction(int card_instance, Target target) {
  const CardName name = GetCardInstance(card_instance).name;
  if (!HasCardInHand(turn_player_, card_instance) || !IsSupportedAction(name)) {
    throw std::logic_error("play action is not a supported Action card in hand");
  }
  DiscardFromHand(turn_player_, card_instance);
  ResolveActionEffect(name, target);
}

void GameState::ResolveActionEffect(CardName name, Target target) {
  if (!IsSupportedAction(name)) {
    throw std::logic_error("replicated card is not a supported Action");
  }
  if (name == CardName::kDuel) {
    BeginDuel(target);
    return;
  }
  if (name == CardName::kWagon) {
    BeginWagon();
    return;
  }
  if (name == CardName::kGeneralStore) {
    BeginGeneralStore();
    return;
  }
  if (name == CardName::kSchoolmarm) {
    BeginSchoolmarm();
    return;
  }
  if (name == CardName::kStealing) {
    BeginStealing();
    return;
  }
  if (name == CardName::kDerringer || name == CardName::kKnife) {
    const auto [target_player, target_position] = ResolveTarget(turn_player_, target);
    const ResponseKind response = name == CardName::kDerringer
                                      ? ResponseKind::kAnyHandCard
                                      : ResponseKind::kAvoid;
    pending_hit_ = PendingHit{turn_player_, target_player, target_position,
                              turn_player_, name, false, response};
    decision_player_ = target_player;
    phase_ = Phase::kRespondToHit;
    return;
  }
  if (name == CardName::kGatling || name == CardName::kIndians) {
    const ResponseKind response = name == CardName::kIndians
                                      ? ResponseKind::kHitCard
                                      : ResponseKind::kAvoid;
    std::vector<PendingHit> hits;
    const auto append = [&](int player, int position) {
      const auto& character = players_[static_cast<std::size_t>(player)]
                                  .positions[static_cast<std::size_t>(position)];
      if (character.life > 0) {
        hits.push_back({turn_player_, player, position, turn_player_, name, false,
                        response});
      }
    };
    const int opponent = OtherPlayer(turn_player_);
    append(opponent, players_[static_cast<std::size_t>(opponent)].active_position);
    const auto& actor = players_[static_cast<std::size_t>(turn_player_)];
    const int actor_rear = 1 - actor.active_position;
    if (actor.positions[static_cast<std::size_t>(actor_rear)].life > 0) {
      append(turn_player_, actor_rear);
    }
    const auto& opponent_state = players_[static_cast<std::size_t>(opponent)];
    const int opponent_rear = 1 - opponent_state.active_position;
    if (opponent_state.positions[static_cast<std::size_t>(opponent_rear)].life > 0) {
      append(opponent, opponent_rear);
    }
    BeginHitSequence(std::move(hits), turn_player_);
    return;
  }
  if (name == CardName::kBeer) {
    for (int position = 0; position < 2; ++position) {
      if (players_[static_cast<std::size_t>(turn_player_)]
              .positions[static_cast<std::size_t>(position)]
              .life > 0) {
        Heal(turn_player_, position, 1);
      }
    }
    return;
  }
  if (name == CardName::kWater) {
    const bool opponent_target = target == Target::kOpponentActive;
    SwapCharacters(opponent_target ? OtherPlayer(turn_player_) : turn_player_, false);
    return;
  }
  if (name != CardName::kStrongAle) {
    throw std::logic_error("supported Action card has no resolver");
  }
  const auto [target_player, target_position] = ResolveTarget(turn_player_, target);
  const int life = players_[static_cast<std::size_t>(target_player)]
                       .positions[static_cast<std::size_t>(target_position)]
                       .life;
  if (life > 3) {
    PendingHit continuation{-1, target_player, target_position, turn_player_,
                            CardName::kColt, false};
    BeginDamage(target_player, target_position, life - 3, turn_player_,
                continuation);
  } else if (life < 3) {
    Heal(target_player, target_position, 3 - life);
  }
}

void GameState::BeginDuel(Target target) {
  effect_actor_ = turn_player_;
  effect_target_position_ = ResolveTarget(turn_player_, target).second;
  decision_player_ = OtherPlayer(turn_player_);
  phase_ = Phase::kDuelResponse;
}

void GameState::FinishDuelResponse(std::optional<int> card_instance) {
  if (phase_ != Phase::kDuelResponse || effect_actor_ < 0) {
    throw std::logic_error("there is no Duel response pending");
  }
  if (card_instance) {
    const int position =
        decision_player_ == effect_actor_
            ? players_[static_cast<std::size_t>(decision_player_)]
                  .active_position
            : effect_target_position_;
    if (!CanProvideHit(decision_player_, position, *card_instance)) {
      throw std::logic_error("Duel response is not a hit card");
    }
    DiscardFromHand(decision_player_, *card_instance);
    decision_player_ = OtherPlayer(decision_player_);
    return;
  }

  const int loser = decision_player_;
  const int position = loser == effect_actor_
                           ? players_[static_cast<std::size_t>(loser)].active_position
                           : effect_target_position_;
  const int resume = effect_actor_;
  effect_actor_ = -1;
  effect_target_position_ = -1;
  PendingHit continuation{-1, loser, position, resume, CardName::kColt, false};
  BeginDamage(loser, position, 1, OtherPlayer(loser), continuation);
}

void GameState::BeginWagon() {
  effect_actor_ = turn_player_;
  choice_cards_.clear();
  choice_cards_public_ = false;
  for (int index = 0; index < 3; ++index) {
    const auto card = TakeTopCard(effect_actor_);
    if (card) choice_cards_.push_back(*card);
  }
  if (choice_cards_.size() <= 2) {
    auto& hand = players_[static_cast<std::size_t>(effect_actor_)].hand;
    hand.insert(hand.end(), choice_cards_.begin(), choice_cards_.end());
    choice_cards_.clear();
    FinishEffectChoice();
    return;
  }
  decision_player_ = effect_actor_;
  phase_ = Phase::kChooseWagonDiscard;
}

void GameState::FinishWagon(int discarded_instance) {
  if (phase_ != Phase::kChooseWagonDiscard || effect_actor_ < 0) {
    throw std::logic_error("there is no Wagon choice pending");
  }
  const auto discarded =
      std::find(choice_cards_.begin(), choice_cards_.end(), discarded_instance);
  if (discarded == choice_cards_.end()) {
    throw std::logic_error("Wagon discard is not one of the drawn cards");
  }
  discard_.push_back(*discarded);
  choice_cards_.erase(discarded);
  auto& hand = players_[static_cast<std::size_t>(effect_actor_)].hand;
  hand.insert(hand.end(), choice_cards_.begin(), choice_cards_.end());
  choice_cards_.clear();
  FinishEffectChoice();
}

void GameState::BeginGeneralStore() {
  effect_actor_ = turn_player_;
  choice_cards_.clear();
  choice_cards_public_ = true;
  if (common_deck_created_) {
    for (int index = 0; index < 4; ++index) {
      const auto card = TakeTopCommonCard();
      if (card) choice_cards_.push_back(*card);
    }
  } else {
    for (int player = 0; player < kSideCount; ++player) {
      for (int index = 0; index < 2; ++index) {
        const auto card = TakeTopCard(player);
        if (card) choice_cards_.push_back(*card);
      }
    }
  }
  if (choice_cards_.empty()) {
    FinishEffectChoice();
    return;
  }
  decision_player_ = effect_actor_;
  phase_ = Phase::kChooseGeneralStore;
}

void GameState::FinishGeneralStorePick(int card_instance) {
  if (phase_ != Phase::kChooseGeneralStore || effect_actor_ < 0) {
    throw std::logic_error("there is no General Store choice pending");
  }
  const auto selected = std::find(choice_cards_.begin(), choice_cards_.end(), card_instance);
  if (selected == choice_cards_.end()) {
    throw std::logic_error("General Store selection is not displayed");
  }
  players_[static_cast<std::size_t>(decision_player_)].hand.push_back(*selected);
  MarkHandCardPublic(decision_player_, *selected);
  choice_cards_.erase(selected);
  if (choice_cards_.empty()) {
    FinishEffectChoice();
  } else {
    decision_player_ = OtherPlayer(decision_player_);
  }
}

std::vector<int> GameState::SelectableEquipment(int actor) const {
  if (actor < 0 || actor >= kSideCount) return {};
  std::vector<int> result;
  const auto& own = players_[static_cast<std::size_t>(actor)];
  for (const auto& character : own.positions) {
    result.insert(result.end(), character.equipment.begin(), character.equipment.end());
  }
  const auto& opponent = players_[static_cast<std::size_t>(OtherPlayer(actor))];
  const auto& active =
      opponent.positions[static_cast<std::size_t>(opponent.active_position)];
  result.insert(result.end(), active.equipment.begin(), active.equipment.end());
  if (IsCharacterAbilityActive(actor, CharacterName::kJackRansome)) {
    const int rear_position = 1 - opponent.active_position;
    const auto& rear = opponent.positions[static_cast<std::size_t>(rear_position)];
    if (rear.life > 0) {
      result.insert(result.end(), rear.equipment.begin(), rear.equipment.end());
    }
  }
  return result;
}

void GameState::BeginSchoolmarm() {
  effect_actor_ = turn_player_;
  choice_cards_.clear();
  choice_cards_public_ = false;
  if (SelectableEquipment(effect_actor_).empty()) {
    FinishEffectChoice();
    return;
  }
  decision_player_ = effect_actor_;
  phase_ = Phase::kChooseSchoolmarm;
}

void GameState::FinishSchoolmarm(std::optional<int> equipment_instance) {
  if (phase_ != Phase::kChooseSchoolmarm || effect_actor_ < 0) {
    throw std::logic_error("there is no Schoolmarm choice pending");
  }
  if (equipment_instance) {
    const auto selectable = SelectableEquipment(effect_actor_);
    if (std::find(selectable.begin(), selectable.end(), *equipment_instance) ==
        selectable.end()) {
      throw std::logic_error("Schoolmarm Equipment is not selectable");
    }
    int owner = -1;
    int owner_position = -1;
    for (int player = 0; player < kSideCount && owner < 0; ++player) {
      for (int position = 0; position < 2; ++position) {
        auto& character = players_[static_cast<std::size_t>(player)]
                              .positions[static_cast<std::size_t>(position)];
        const auto equipment =
            std::find(character.equipment.begin(), character.equipment.end(),
                      *equipment_instance);
        if (equipment != character.equipment.end()) {
          character.equipment.erase(equipment);
          owner = player;
          owner_position = position;
          break;
        }
      }
    }
    if (owner < 0) throw std::logic_error("Schoolmarm Equipment vanished");
    players_[static_cast<std::size_t>(effect_actor_)].hand.push_back(
        *equipment_instance);
    MarkHandCardPublic(effect_actor_, *equipment_instance);
    if (BeginWildBillReaction(owner, effect_actor_,
                              ResumeKind::kEffectChoice, owner_position)) {
      return;
    }
  }
  FinishEffectChoice();
}

void GameState::BeginStealing() {
  effect_actor_ = turn_player_;
  choice_cards_.clear();
  choice_cards_public_ = false;
  const auto& opponent = players_[static_cast<std::size_t>(OtherPlayer(effect_actor_))];
  if (opponent.hand.empty() && SelectableEquipment(effect_actor_).empty()) {
    FinishEffectChoice();
    return;
  }
  decision_player_ = effect_actor_;
  phase_ = Phase::kChooseStealing;
}

void GameState::FinishStealingEquipment(int equipment_instance) {
  if (phase_ != Phase::kChooseStealing || effect_actor_ < 0) {
    throw std::logic_error("there is no Stealing choice pending");
  }
  const auto selectable = SelectableEquipment(effect_actor_);
  if (std::find(selectable.begin(), selectable.end(), equipment_instance) ==
      selectable.end()) {
    throw std::logic_error("Stealing Equipment is not selectable");
  }
  for (int player = 0; player < kSideCount; ++player) {
    for (int position = 0; position < 2; ++position) {
      const auto& equipment = players_[static_cast<std::size_t>(player)]
                                  .positions[static_cast<std::size_t>(position)]
                                  .equipment;
      if (std::find(equipment.begin(), equipment.end(), equipment_instance) !=
          equipment.end()) {
        RemoveEquipment(player, position, equipment_instance);
        if (BeginWildBillReaction(player, effect_actor_,
                                  ResumeKind::kEffectChoice, position)) {
          return;
        }
        FinishEffectChoice();
        return;
      }
    }
  }
  throw std::logic_error("Stealing Equipment vanished");
}

void GameState::FinishStealingHand(Side origin) {
  if (phase_ != Phase::kChooseStealing || effect_actor_ < 0) {
    throw std::logic_error("there is no Stealing hand choice pending");
  }
  auto& opponent = players_[static_cast<std::size_t>(OtherPlayer(effect_actor_))].hand;
  std::vector<std::size_t> candidates;
  for (std::size_t index = 0; index < opponent.size(); ++index) {
    if (GetCardInstance(opponent[index]).origin == origin) candidates.push_back(index);
  }
  if (candidates.empty()) throw std::logic_error("selected card back is unavailable");
  const std::size_t selected = candidates[rng_.USize(candidates.size())];
  const int card = opponent[selected];
  const int opponent_player = OtherPlayer(effect_actor_);
  opponent.erase(opponent.begin() + static_cast<std::ptrdiff_t>(selected));
  ForgetPublicHandCard(opponent_player, card);
  players_[static_cast<std::size_t>(effect_actor_)].hand.push_back(card);
  MarkHandCardPublic(effect_actor_, card);
  if (BeginWildBillReaction(opponent_player, effect_actor_,
                            ResumeKind::kEffectChoice)) {
    return;
  }
  FinishEffectChoice();
}

void GameState::FinishEffectChoice() {
  if (effect_actor_ < 0) throw std::logic_error("there is no effect choice pending");
  const int resume = effect_actor_;
  effect_actor_ = -1;
  effect_target_position_ = -1;
  choice_cards_.clear();
  choice_cards_public_ = false;
  decision_player_ = resume;
  phase_ = Phase::kPlay;
}

void GameState::ResolveReturnFire(int card_instance) {
  if (!pending_hit_ || !HasCardInHand(decision_player_, card_instance) ||
      GetCardInstance(card_instance).name != CardName::kReturnFire) {
    throw std::logic_error("invalid Return Fire response");
  }
  DiscardFromHand(decision_player_, card_instance);
  RecordAvoid(true);
}

void GameState::RecordAvoid(bool return_fire, int counter_character) {
  if (!pending_hit_) throw std::logic_error("there is no pending hit to avoid");
  ++pending_hit_->avoids_played;
  pending_hit_->return_fire_pending |= return_fire;
  if (counter_character >= 0) {
    pending_hit_->counter_character = counter_character;
  }
  if (pending_hit_->avoids_played < pending_hit_->avoids_required) {
    decision_player_ = pending_hit_->target;
    phase_ = Phase::kRespondToHit;
    return;
  }

  const PendingHit original = *pending_hit_;
  pending_hit_.reset();
  ApplyAvoidedCardMoves(original);
  if (!original.return_fire_pending && original.counter_character < 0) {
    ResolveAvoidedHit(original, std::nullopt);
    return;
  }

  const int counter_target = original.attacker;
  const int counter_position =
      players_[static_cast<std::size_t>(counter_target)].active_position;
  const bool return_fire_counter = original.return_fire_pending;
  PendingHit counter{original.target, counter_target, counter_position,
                     original.resume_player,
                     return_fire_counter ? CardName::kReturnFire
                                         : CardName::kColt,
                     false};
  counter.resume_kind = original.resume_kind;
  counter.source_character = original.counter_character;
  if (defer_eliminations_) {
    BeginWyattEarReaction(original, std::nullopt);
    deferred_effects_.push_back({counter, std::nullopt});
    ResolveAvoidedHit(original, std::nullopt, false);
  } else {
    ResolveAvoidedHit(original, counter);
  }
}

void GameState::ApplyAvoidedCardMoves(const PendingHit& hit) {
  if (hit.source_instance < 0) return;
  const auto found = std::find(discard_.begin(), discard_.end(), hit.source_instance);
  // Gone already, e.g. a Fanning taken by a Feint on an earlier hit.
  if (found == discard_.end()) return;
  const CardName name = GetCardInstance(hit.source_instance).name;
  int receiver = -1;
  if (name == CardName::kWalker) {
    receiver = hit.attacker;  // even when a Feint avoided it
  } else if (hit.feint && GetCard(name).kind == CardKind::kBang) {
    receiver = hit.target;
  }
  if (receiver < 0) return;
  discard_.erase(found);
  players_[static_cast<std::size_t>(receiver)].hand.push_back(hit.source_instance);
  MarkHandCardPublic(receiver, hit.source_instance);
}

void GameState::BeginSidCurryReveal() {
  if (!pending_hit_ || pending_hit_->sid_attempted ||
      !TargetHasAbility(pending_hit_->target, pending_hit_->target_position,
                        CharacterName::kSidCurry)) {
    throw std::logic_error("Sid Curry cannot reveal for this hit");
  }
  pending_hit_->sid_attempted = true;
  BeginReveal({RevealReason::kSidCurry, OtherPlayer(pending_hit_->target),
               pending_hit_->target_position, -1});
}

void GameState::BeginBarrelReveal(int barrel_instance) {
  if (!pending_hit_ || pending_hit_->barrel_attempted) {
    throw std::logic_error("Barrel cannot be attempted for this hit");
  }
  const auto& equipment =
      players_[static_cast<std::size_t>(pending_hit_->target)]
          .positions[static_cast<std::size_t>(pending_hit_->target_position)]
          .equipment;
  if (GetCardInstance(barrel_instance).name != CardName::kBarrel ||
      std::find(equipment.begin(), equipment.end(), barrel_instance) == equipment.end()) {
    throw std::logic_error("target does not have the selected Barrel");
  }
  pending_hit_->barrel_attempted = true;
  BeginReveal({RevealReason::kBarrel, pending_hit_->target,
               pending_hit_->target_position, barrel_instance});
}

void GameState::BeginReveal(PendingReveal reveal) {
  if (pending_reveal_) throw std::logic_error("another reveal is already pending");
  pending_reveal_ = reveal;
  revealed_cards_.clear();
  int reveal_count = 1;
  int chooser = reveal.player;
  for (int player = 0; player < kSideCount; ++player) {
    for (const auto& character : players_[static_cast<std::size_t>(player)].positions) {
      if (character.life > 0 && character.character_id == 2) {
        reveal_count = 2;
        chooser = player;
      }
    }
  }
  for (int index = 0; index < reveal_count; ++index) {
    const auto card = TakeTopCard(reveal.player);
    if (card) revealed_cards_.push_back(*card);
  }
  for (const int card : revealed_cards_) discard_.push_back(card);
  if (revealed_cards_.size() <= 1) {
    FinishReveal(0);
    return;
  }
  decision_player_ = chooser;
  phase_ = Phase::kChooseReveal;
}

void GameState::FinishReveal(int selected_index) {
  if (!pending_reveal_) throw std::logic_error("there is no pending reveal");
  const PendingReveal reveal = *pending_reveal_;
  std::optional<RevealSymbol> symbol;
  if (selected_index >= 0 && selected_index < static_cast<int>(revealed_cards_.size())) {
    symbol = GetCardInstance(revealed_cards_[static_cast<std::size_t>(selected_index)]).reveal;
    const bool triggered =
        (reveal.reason == RevealReason::kBarrel && symbol == RevealSymbol::kBarrel) ||
        (reveal.reason == RevealReason::kDynamite && symbol == RevealSymbol::kDynamite) ||
        (reveal.reason == RevealReason::kRattlesnake && symbol == RevealSymbol::kSnake) ||
        (reveal.reason == RevealReason::kSidCurry && symbol == RevealSymbol::kBarrel);
    reveal_events_.push_back(
        {++reveal_sequence_, reveal.reason, reveal.player,
         revealed_cards_[static_cast<std::size_t>(selected_index)], triggered});
    constexpr std::size_t kMaxRevealEvents = 16;
    if (reveal_events_.size() > kMaxRevealEvents) {
      reveal_events_.erase(reveal_events_.begin());
    }
  }
  pending_reveal_.reset();
  revealed_cards_.clear();
  if (reveal.reason == RevealReason::kBarrel) {
    if (!pending_hit_) throw std::logic_error("Barrel reveal has no pending hit");
    if (symbol == RevealSymbol::kBarrel) {
      RecordAvoid(false);
    } else {
      decision_player_ = pending_hit_->target;
      phase_ = Phase::kRespondToHit;
    }
    return;
  }
  if (reveal.reason == RevealReason::kDynamite) {
    if (symbol == RevealSymbol::kDynamite) {
      RemoveEquipment(reveal.player, reveal.position, reveal.equipment_instance);
      PendingHit continuation{-1, reveal.player, reveal.position, turn_player_,
                              CardName::kColt, false};
      continuation.resume_kind = ResumeKind::kStartTurn;
      BeginDamage(reveal.player, reveal.position, 3, -1, continuation);
      return;
    } else {
      const auto [target_player, target_position] =
          ResolveTarget(reveal.player, Target::kOpponentRearguard);
      MoveEquipment(reveal.player, reveal.position, reveal.equipment_instance,
                    target_player, target_position);
    }
    if (IsTerminal()) {
      start_turn_step_ = StartTurnStep::kNone;
    } else {
      AdvanceStartTurn();
    }
    return;
  }
  if (reveal.reason == RevealReason::kRattlesnake) {
    if (symbol == RevealSymbol::kSnake) {
      PendingHit continuation{-1, reveal.player, reveal.position, turn_player_,
                              CardName::kColt, false};
      continuation.resume_kind = ResumeKind::kStartTurn;
      BeginDamage(reveal.player, reveal.position, 1, -1, continuation);
      return;
    }
    if (IsTerminal()) {
      start_turn_step_ = StartTurnStep::kNone;
    } else {
      AdvanceStartTurn();
    }
    return;
  }
  if (reveal.reason == RevealReason::kSidCurry) {
    if (!pending_hit_) throw std::logic_error("Sid Curry reveal has no pending hit");
    if (symbol == RevealSymbol::kBarrel) {
      RecordAvoid(false, static_cast<int>(CharacterName::kSidCurry));
    } else {
      decision_player_ = pending_hit_->target;
      phase_ = Phase::kRespondToHit;
    }
    return;
  }
  throw std::logic_error("unknown reveal reason");
}

void GameState::MoveEquipment(int from_player, int from_position, int card_instance,
                              int to_player, int to_position) {
  auto& from = players_[static_cast<std::size_t>(from_player)]
                   .positions[static_cast<std::size_t>(from_position)]
                   .equipment;
  const auto found = std::find(from.begin(), from.end(), card_instance);
  if (found == from.end()) throw std::logic_error("moving Equipment is not in play");
  from.erase(found);
  auto& to = players_[static_cast<std::size_t>(to_player)]
                 .positions[static_cast<std::size_t>(to_position)]
                 .equipment;
  const CardName name = GetCardInstance(card_instance).name;
  const auto duplicate = std::find_if(to.begin(), to.end(), [name](int instance) {
    return GetCardInstance(instance).name == name;
  });
  if (duplicate != to.end()) {
    discard_.push_back(*duplicate);
    to.erase(duplicate);
  }
  to.push_back(card_instance);
}

void GameState::BeginHitSequence(std::vector<PendingHit> hits, int resume_player) {
  if (hit_sequence_active_ || pending_hit_ || queued_hit_) {
    throw std::logic_error("another hit sequence is already active");
  }
  std::reverse(hits.begin(), hits.end());
  hit_sequence_ = std::move(hits);
  deferred_effects_.clear();
  deferred_eliminations_.clear();
  hit_sequence_active_ = true;
  defer_eliminations_ = true;
  hit_sequence_resume_player_ = resume_player;
  AdvanceHitSequence();
}

void GameState::AdvanceHitSequence() {
  if (!hit_sequence_active_) throw std::logic_error("there is no active hit sequence");
  for (;;) {
    while (!hit_sequence_.empty()) {
      PendingHit hit = hit_sequence_.back();
      hit_sequence_.pop_back();
      const auto& target = players_[static_cast<std::size_t>(hit.target)]
                               .positions[static_cast<std::size_t>(hit.target_position)];
      if (target.life <= 0) continue;
      ActivatePendingHit(hit);
      return;
    }

    if (defer_eliminations_) {
      defer_eliminations_ = false;
    }

    while (!deferred_eliminations_.empty()) {
      const auto [player, position] = deferred_eliminations_.front();
      deferred_eliminations_.erase(deferred_eliminations_.begin());
      const auto& character = players_[static_cast<std::size_t>(player)]
                                  .positions[static_cast<std::size_t>(position)];
      if (character.character_id < 0 || character.life > 0) continue;
      PendingHit continuation{-1, player, position,
                              hit_sequence_resume_player_, CardName::kColt,
                              false};
      BeginElimination(player, position, continuation);
      return;
    }

    if (!deferred_effects_.empty()) {
      DeferredEffect effect = std::move(deferred_effects_.front());
      deferred_effects_.erase(deferred_effects_.begin());
      if (effect.hit) {
        ActivatePendingHit(*effect.hit);
      } else if (effect.reaction) {
        ActivateDamageReaction(*effect.reaction);
      } else {
        throw std::logic_error("deferred effect has no payload");
      }
      return;
    }

    const int resume = hit_sequence_resume_player_;
    hit_sequence_active_ = false;
    hit_sequence_resume_player_ = -1;
    decision_player_ = resume;
    phase_ = Phase::kPlay;
    return;
  }
}

void GameState::ContinueAfterResolvedHit(const PendingHit& hit) {
  if (!continuation_hits_.empty()) {
    PendingHit next = continuation_hits_.back();
    continuation_hits_.pop_back();
    ActivatePendingHit(next);
    return;
  }
  if (!hit_sequence_active_ && !deferred_effects_.empty()) {
    DeferredEffect effect = std::move(deferred_effects_.front());
    deferred_effects_.erase(deferred_effects_.begin());
    if (effect.hit) {
      ActivatePendingHit(*effect.hit);
    } else if (effect.reaction) {
      ActivateDamageReaction(*effect.reaction);
    } else {
      throw std::logic_error("deferred effect has no payload");
    }
    return;
  }
  if (hit.resume_kind == ResumeKind::kMovement) {
    AdvanceMovementTriggers(true);
    return;
  }
  if (hit.resume_kind == ResumeKind::kStartTurn) {
    AdvanceStartTurn();
    return;
  }
  if (hit.resume_kind == ResumeKind::kPendingEffect) {
    ResumeAfterPendingEffect();
    return;
  }
  if (hit.resume_kind == ResumeKind::kEffectChoice) {
    FinishEffectChoice();
    return;
  }
  if (hit.resume_kind == ResumeKind::kDamageReaction) {
    ResumeDamageReaction();
    return;
  }
  if (hit.resume_kind == ResumeKind::kElimination) {
    ResumeElimination();
    return;
  }
  if (hit_sequence_active_) {
    AdvanceHitSequence();
    return;
  }
  decision_player_ = hit.resume_player;
  phase_ = Phase::kPlay;
}

bool GameState::BeginWildBillReaction(
    int owner, int actor, ResumeKind resume_kind,
    std::optional<int> equipment_position) {
  if (owner < 0 || actor < 0 || owner == actor ||
      !IsCharacterAbilityActive(owner, CharacterName::kWildBill)) {
    return false;
  }
  if (equipment_position &&
      !TargetHasAbility(owner, *equipment_position,
                        CharacterName::kWildBill)) {
    return false;
  }
  const int target_position =
      players_[static_cast<std::size_t>(actor)].active_position;
  PendingHit hit{owner, actor, target_position, turn_player_,
                 CardName::kColt, false};
  hit.resume_kind = resume_kind;
  hit.source_character = static_cast<int>(CharacterName::kWildBill);
  ActivatePendingHit(hit);
  return true;
}

void GameState::ResolveHit() {
  if (!pending_hit_) throw std::logic_error("there is no pending hit");
  const PendingHit hit = *pending_hit_;
  pending_hit_.reset();
  const int target_life = players_[static_cast<std::size_t>(hit.target)]
                              .positions[static_cast<std::size_t>(hit.target_position)]
                              .life;
  const int damage = hit.source == CardName::kSpencer && target_life >= 4 ? 2 : 1;
  BeginDamage(hit.target, hit.target_position, damage, hit.attacker, hit);
}

bool GameState::BeginAlanPinkertoonHits(
    int player, int count, const PendingHit& continuation) {
  if (count <= 0 || IsTerminal()) return false;
  const int target = OtherPlayer(player);
  const int target_position =
      players_[static_cast<std::size_t>(target)].active_position;
  std::vector<PendingHit> hits;
  hits.reserve(static_cast<std::size_t>(count));
  for (int index = 0; index < count; ++index) {
    PendingHit hit{player, target, target_position, continuation.resume_player,
                   CardName::kColt, false};
    hit.resume_kind = continuation.resume_kind;
    hit.source_character =
        static_cast<int>(CharacterName::kAlanPinkertoon);
    hits.push_back(hit);
  }
  if (defer_eliminations_) {
    for (const PendingHit& hit : hits) {
      deferred_effects_.push_back({hit, std::nullopt});
    }
    return false;
  }
  for (auto iterator = hits.rbegin(); iterator != hits.rend(); ++iterator) {
    continuation_hits_.push_back(*iterator);
  }
  PendingHit first = continuation_hits_.back();
  continuation_hits_.pop_back();
  ActivatePendingHit(first);
  return true;
}

bool GameState::BeginOpponentDamageReaction(
    int player, int character_id, int count, int source_player,
    const PendingHit& continuation) {
  if (count <= 0 || source_player != OtherPlayer(player) || IsTerminal()) {
    return false;
  }
  if (character_id != static_cast<int>(CharacterName::kDalonRanger) &&
      character_id != static_cast<int>(CharacterName::kSlimPoet)) {
    return false;
  }
  DamageReaction reaction{character_id, player, count, continuation};
  if (defer_eliminations_) {
    deferred_effects_.push_back({std::nullopt, reaction});
    return false;
  }
  ActivateDamageReaction(std::move(reaction));
  return true;
}

void GameState::ActivateDamageReaction(DamageReaction reaction) {
  damage_reaction_stack_.push_back(std::move(reaction));
  const auto& pending = damage_reaction_stack_.back();
  pending_ability_ = pending.character_id;
  ability_stage_ = pending.remaining;
  decision_player_ = pending.player;
  phase_ = Phase::kChooseAbility;
}

void GameState::ResumeDamageReaction() {
  if (damage_reaction_stack_.empty()) {
    throw std::logic_error("there is no damage reaction to resume");
  }
  const auto& reaction = damage_reaction_stack_.back();
  const bool can_repeat =
      reaction.remaining > 0 &&
      (reaction.character_id != static_cast<int>(CharacterName::kSlimPoet) ||
       !players_[static_cast<std::size_t>(OtherPlayer(reaction.player))]
            .hand.empty());
  if (can_repeat) {
    pending_ability_ = reaction.character_id;
    ability_stage_ = reaction.remaining;
    decision_player_ = reaction.player;
    phase_ = Phase::kChooseAbility;
    return;
  }
  const PendingHit continuation = reaction.continuation;
  damage_reaction_stack_.pop_back();
  pending_ability_ = -1;
  ability_stage_ = 0;
  if (continuation.source_character ==
          static_cast<int>(CharacterName::kWyattEar) &&
      queued_hit_) {
    PendingHit next = *queued_hit_;
    queued_hit_.reset();
    ActivatePendingHit(next);
    return;
  }
  ContinueAfterResolvedHit(continuation);
}

void GameState::FinishDamageReaction(bool accept) {
  if (damage_reaction_stack_.empty() ||
      phase_ != Phase::kChooseAbility) {
    throw std::logic_error("there is no pending damage reaction");
  }
  auto& reaction = damage_reaction_stack_.back();
  if (accept) {
    if (reaction.character_id ==
        static_cast<int>(CharacterName::kDalonRanger)) {
      DrawOne(reaction.player);
    } else if (reaction.character_id ==
               static_cast<int>(CharacterName::kWyattEar)) {
      DrawOne(reaction.player);
    } else if (reaction.character_id ==
               static_cast<int>(CharacterName::kSlimPoet)) {
      if (players_[static_cast<std::size_t>(
              OtherPlayer(reaction.player))]
              .hand.empty()) {
        throw std::logic_error("Slim Poet cannot discard from an empty hand");
      }
      reaction.choosing_origin = true;
      return;
    } else {
      throw std::logic_error("unknown damage reaction");
    }
  }
  --reaction.remaining;
  ResumeDamageReaction();
}

void GameState::FinishSlimPoetHand(Side origin) {
  if (damage_reaction_stack_.empty() ||
      phase_ != Phase::kChooseAbility) {
    throw std::logic_error("there is no pending Slim Poet discard");
  }
  auto& reaction = damage_reaction_stack_.back();
  if (reaction.character_id !=
          static_cast<int>(CharacterName::kSlimPoet) ||
      !reaction.choosing_origin) {
    throw std::logic_error("Slim Poet is not choosing a card back");
  }
  const int opponent = OtherPlayer(reaction.player);
  const auto& hand = players_[static_cast<std::size_t>(opponent)].hand;
  std::vector<int> candidates;
  for (const int card : hand) {
    if (GetCardInstance(card).origin == origin) {
      candidates.push_back(card);
    }
  }
  if (candidates.empty()) {
    throw std::logic_error("selected Slim Poet card back is unavailable");
  }
  const int card = candidates[rng_.USize(candidates.size())];
  DiscardFromHand(opponent, card);
  reaction.choosing_origin = false;
  --reaction.remaining;
  if (BeginWildBillReaction(opponent, reaction.player,
                            ResumeKind::kDamageReaction)) {
    return;
  }
  ResumeDamageReaction();
}

bool GameState::BeginWyattEarReaction(
    const PendingHit& hit, std::optional<PendingHit> next_hit) {
  const bool captured =
      hit.source_character == static_cast<int>(CharacterName::kWyattEar);
  if (!captured &&
      (hit.source_character >= 0 ||
       !GetCard(hit.source).has_hit ||
       !IsCharacterAbilityActive(hit.attacker, CharacterName::kWyattEar))) {
    return false;
  }
  PendingHit continuation = hit;
  continuation.source_character =
      static_cast<int>(CharacterName::kWyattEar);
  DamageReaction reaction{
      static_cast<int>(CharacterName::kWyattEar), hit.attacker, 1,
      continuation};
  if (defer_eliminations_) {
    if (next_hit) {
      throw std::logic_error(
          "deferred Wyatt Ear reaction cannot own a queued hit");
    }
    deferred_effects_.push_back({std::nullopt, reaction});
    return false;
  }
  queued_hit_ = std::move(next_hit);
  ActivateDamageReaction(std::move(reaction));
  return true;
}

void GameState::ResolveAvoidedHit(const PendingHit& hit,
                                  std::optional<PendingHit> next_hit,
                                  bool check_wyatt) {
  ApplyAvoidedHitEffects(hit);
  const bool wyatt_triggered =
      check_wyatt && hit.source_character < 0 &&
      GetCard(hit.source).has_hit &&
      IsCharacterAbilityActive(hit.attacker, CharacterName::kWyattEar);
  const auto remember_optional_hit = [&]() {
    optional_effect_hit_ = hit;
    if (wyatt_triggered) {
      optional_effect_hit_->source_character =
          static_cast<int>(CharacterName::kWyattEar);
    }
  };
  if (hit.source == CardName::kRemington) {
    queued_hit_ = std::move(next_hit);
    remember_optional_hit();
    optional_effect_player_ = hit.attacker;
    optional_effect_resume_player_ = hit.resume_player;
    decision_player_ = hit.attacker;
    phase_ = Phase::kChooseRemington;
    return;
  }
  if (hit.source == CardName::kPeacemaker &&
      !players_[static_cast<std::size_t>(hit.target)].hand.empty()) {
    queued_hit_ = std::move(next_hit);
    remember_optional_hit();
    optional_effect_player_ = hit.target;
    optional_effect_resume_player_ = hit.resume_player;
    decision_player_ = hit.target;
    phase_ = Phase::kForcedDiscard;
    return;
  }
  if (hit.source == CardName::kCarbine && !ActiveEquipmentInstances().empty()) {
    queued_hit_ = std::move(next_hit);
    remember_optional_hit();
    optional_effect_player_ = hit.attacker;
    optional_effect_resume_player_ = hit.resume_player;
    decision_player_ = hit.attacker;
    phase_ = Phase::kChooseCarbine;
    return;
  }
  if (hit.source == CardName::kShotgun) {
    const auto& own = players_[static_cast<std::size_t>(hit.attacker)];
    const auto& opponent = players_[static_cast<std::size_t>(OtherPlayer(hit.attacker))];
    const bool can_swap_own = own.positions[0].life > 0 && own.positions[1].life > 0;
    const bool can_swap_opponent =
        opponent.positions[0].life > 0 && opponent.positions[1].life > 0;
    if (can_swap_own || can_swap_opponent) {
      queued_hit_ = std::move(next_hit);
      remember_optional_hit();
      optional_effect_player_ = hit.attacker;
      optional_effect_resume_player_ = hit.resume_player;
      decision_player_ = hit.attacker;
      phase_ = Phase::kChooseShotgun;
      return;
    }
  }
  if (check_wyatt && BeginWyattEarReaction(hit, next_hit)) return;
  if (next_hit) {
    ActivatePendingHit(*next_hit);
  } else {
    ContinueAfterResolvedHit(hit);
  }
}

void GameState::FinishRemingtonChoice(bool draw) {
  if (optional_effect_player_ < 0) {
    throw std::logic_error("there is no pending Remington choice");
  }
  if (draw) DrawOne(optional_effect_player_);
  ResumeAfterPendingEffect();
}

void GameState::FinishForcedDiscard(int card_instance) {
  if (phase_ != Phase::kForcedDiscard || optional_effect_player_ < 0) {
    throw std::logic_error("there is no pending forced discard");
  }
  if (!optional_effect_hit_) {
    throw std::logic_error("forced discard has no originating hit");
  }
  const int owner = optional_effect_player_;
  const int actor = optional_effect_hit_->attacker;
  DiscardFromHand(optional_effect_player_, card_instance);
  if (BeginWildBillReaction(owner, actor, ResumeKind::kPendingEffect)) {
    return;
  }
  ResumeAfterPendingEffect();
}

std::vector<int> GameState::ActiveEquipmentInstances() const {
  std::vector<int> result;
  for (const auto& player : players_) {
    const auto& active =
        player.positions[static_cast<std::size_t>(player.active_position)];
    result.insert(result.end(), active.equipment.begin(), active.equipment.end());
  }
  return result;
}

void GameState::FinishCarbineChoice(int equipment_instance) {
  if (phase_ != Phase::kChooseCarbine || optional_effect_player_ < 0) {
    throw std::logic_error("there is no pending Carbine choice");
  }
  if (equipment_instance >= 0) {
    bool removed = false;
    int owner = -1;
    int position = -1;
    for (int player = 0; player < kSideCount && !removed; ++player) {
      const int active = players_[static_cast<std::size_t>(player)].active_position;
      auto& equipment = players_[static_cast<std::size_t>(player)]
                            .positions[static_cast<std::size_t>(active)]
                            .equipment;
      if (std::find(equipment.begin(), equipment.end(), equipment_instance) !=
          equipment.end()) {
        RemoveEquipment(player, active, equipment_instance);
        removed = true;
        owner = player;
        position = active;
      }
    }
    if (!removed) throw std::logic_error("Carbine target is not on an AC");
    if (BeginWildBillReaction(owner, optional_effect_player_,
                              ResumeKind::kPendingEffect, position)) {
      return;
    }
  }
  ResumeAfterPendingEffect();
}

void GameState::FinishShotgunChoice(bool opponent) {
  if (phase_ != Phase::kChooseShotgun || optional_effect_player_ < 0) {
    throw std::logic_error("there is no pending Shotgun choice");
  }
  const int target =
      opponent ? OtherPlayer(optional_effect_player_) : optional_effect_player_;
  if (!SwapCharacters(target, false, ResumeKind::kPendingEffect)) {
    ResumeAfterPendingEffect();
  }
}

void GameState::ResumeAfterPendingEffect() {
  if (optional_effect_player_ < 0 || optional_effect_resume_player_ < 0) {
    throw std::logic_error("there is no pending effect continuation");
  }
  const int resume = optional_effect_resume_player_;
  const std::optional<PendingHit> resolved_hit = optional_effect_hit_;
  optional_effect_hit_.reset();
  optional_effect_player_ = -1;
  optional_effect_resume_player_ = -1;
  std::optional<PendingHit> next_hit = queued_hit_;
  queued_hit_.reset();
  if (resolved_hit &&
      BeginWyattEarReaction(*resolved_hit, next_hit)) {
    return;
  }
  if (next_hit) {
    PendingHit next = *next_hit;
    ActivatePendingHit(next);
  } else {
    if (hit_sequence_active_) {
      AdvanceHitSequence();
    } else {
      decision_player_ = resume;
      phase_ = Phase::kPlay;
    }
  }
}

void GameState::ActivatePendingHit(PendingHit hit) {
  if (hit.source == CardName::kReturnFire ||
      hit.source_character ==
          static_cast<int>(CharacterName::kAlanPinkertoon) ||
      hit.source_character == static_cast<int>(CharacterName::kSidCurry) ||
      hit.source_character == static_cast<int>(CharacterName::kWildBill)) {
    hit.target_position =
        players_[static_cast<std::size_t>(hit.target)].active_position;
  }
  pending_hit_ = hit;
  decision_player_ = hit.target;
  phase_ = Phase::kRespondToHit;
}

void GameState::ApplyAvoidedHitEffects(const PendingHit& hit) {
  if (hit.source == CardName::kSchofield) {
    const int active = players_[static_cast<std::size_t>(hit.attacker)].active_position;
    Heal(hit.attacker, active, 1);
  }
}

void GameState::Heal(int player, int position, int amount) {
  auto& character = players_[static_cast<std::size_t>(player)]
                        .positions[static_cast<std::size_t>(position)];
  if (character.life <= 0 || amount < 0) throw std::logic_error("invalid healing");
  character.life =
      std::min(GetCharacter(character.character_id).max_life, character.life + amount);
}

void GameState::BeginDamage(int player, int position, int amount,
                            int source_player,
                            const PendingHit& continuation) {
  if (pending_damage_ || amount <= 0) {
    throw std::logic_error("invalid or nested pending damage");
  }
  pending_damage_ = PendingDamage{player, position, amount, source_player,
                                  continuation};
  const int buffalo_position =
      LivingCharacterPosition(player, CharacterName::kBuffaloBell);
  const bool can_redirect =
      buffalo_position >= 0 && position != buffalo_position &&
      IsCharacterAbilityActive(player, CharacterName::kBuffaloBell) &&
      players_[static_cast<std::size_t>(player)]
              .positions[static_cast<std::size_t>(buffalo_position)]
              .life > 1;
  if (can_redirect) {
    pending_ability_ = static_cast<int>(CharacterName::kBuffaloBell);
    ability_stage_ = amount;
    decision_player_ = player;
    phase_ = Phase::kChooseAbility;
    return;
  }
  ApplyPendingDamage();
}

void GameState::ApplyPendingDamage() {
  if (!pending_damage_) {
    throw std::logic_error("there is no pending damage");
  }
  const PendingDamage damage = *pending_damage_;
  pending_damage_.reset();
  pending_ability_ = -1;
  ability_stage_ = 0;

  const auto& before =
      players_[static_cast<std::size_t>(damage.player)]
          .positions[static_cast<std::size_t>(damage.position)];
  const int character_id = before.character_id;
  const int previous_life = before.life;
  const int alan_triggers =
      LoseLife(damage.player, damage.position, damage.remaining);
  const int current_life =
      players_[static_cast<std::size_t>(damage.player)]
          .positions[static_cast<std::size_t>(damage.position)]
          .life;
  if (!defer_eliminations_ && current_life == 0) {
    const std::size_t deferred_effect_start = deferred_effects_.size();
    defer_eliminations_ = true;
    BeginAlanPinkertoonHits(damage.player, alan_triggers,
                            damage.continuation);
    BeginOpponentDamageReaction(
        damage.player, character_id,
        std::min(previous_life - current_life,
                 std::max(0, previous_life - 1)),
        damage.source_player, damage.continuation);
    defer_eliminations_ = false;
    std::vector<DeferredEffect> elimination_effects;
    elimination_effects.reserve(deferred_effects_.size() -
                                deferred_effect_start);
    std::move(deferred_effects_.begin() +
                  static_cast<std::ptrdiff_t>(deferred_effect_start),
              deferred_effects_.end(),
              std::back_inserter(elimination_effects));
    deferred_effects_.erase(
        deferred_effects_.begin() +
            static_cast<std::ptrdiff_t>(deferred_effect_start),
        deferred_effects_.end());
    BeginElimination(damage.player, damage.position, damage.continuation,
                     std::move(elimination_effects));
    return;
  }
  const int non_last_life_lost =
      std::min(previous_life - current_life, std::max(0, previous_life - 1));
  if (!BeginAlanPinkertoonHits(damage.player, alan_triggers,
                               damage.continuation) &&
      !BeginOpponentDamageReaction(
          damage.player, character_id, non_last_life_lost,
          damage.source_player, damage.continuation)) {
    ContinueAfterResolvedHit(damage.continuation);
  }
}

void GameState::FinishBuffaloBell(bool redirect) {
  if (!pending_damage_ ||
      pending_ability_ != static_cast<int>(CharacterName::kBuffaloBell)) {
    throw std::logic_error("there is no pending Buffalo Bell choice");
  }
  if (!redirect) {
    ApplyPendingDamage();
    return;
  }
  auto& damage = *pending_damage_;
  const int buffalo_position =
      LivingCharacterPosition(damage.player,
                              CharacterName::kBuffaloBell);
  if (buffalo_position < 0) {
    throw std::logic_error("Buffalo Bell is no longer in play");
  }
  auto& buffalo =
      players_[static_cast<std::size_t>(damage.player)]
          .positions[static_cast<std::size_t>(buffalo_position)];
  if (buffalo_position == damage.position ||
      buffalo.character_id !=
          static_cast<int>(CharacterName::kBuffaloBell) ||
      buffalo.life <= 1) {
    throw std::logic_error("Buffalo Bell cannot redirect this life point");
  }
  LoseLife(damage.player, buffalo_position, 1);
  --damage.remaining;
  if (damage.remaining == 0) {
    const PendingHit continuation = damage.continuation;
    pending_damage_.reset();
    pending_ability_ = -1;
    ability_stage_ = 0;
    ContinueAfterResolvedHit(continuation);
    return;
  }
  ability_stage_ = damage.remaining;
}

int GameState::LoseLife(int player, int position, int amount) {
  auto& character = players_[static_cast<std::size_t>(player)]
                        .positions[static_cast<std::size_t>(position)];
  if (character.life <= 0 || amount < 0) throw std::logic_error("invalid life loss");
  const int previous_life = character.life;
  const bool alan = character.character_id ==
                    static_cast<int>(CharacterName::kAlanPinkertoon);
  character.life = std::max(0, character.life - amount);
  const int alan_triggers =
      alan ? std::max(0, previous_life - std::max(1, character.life)) : 0;
  if (character.life == 0) {
    if (defer_eliminations_) {
      const std::pair<int, int> eliminated{player, position};
      if (std::find(deferred_eliminations_.begin(), deferred_eliminations_.end(),
                    eliminated) == deferred_eliminations_.end()) {
        deferred_eliminations_.push_back(eliminated);
      }
    }
  }
  return alan_triggers;
}

bool GameState::BullAndersonHasChoices() const {
  if (elimination_stack_.empty() ||
      elimination_stack_.back().bull_player < 0 ||
      elimination_stack_.back().bull_picks >= 2) {
    return false;
  }
  const auto& elimination = elimination_stack_.back();
  const auto& character =
      players_[static_cast<std::size_t>(elimination.player)]
          .positions[static_cast<std::size_t>(elimination.position)];
  if (!character.equipment.empty()) return true;
  return elimination.player != elimination.bull_player &&
         !players_[static_cast<std::size_t>(elimination.player)].hand.empty();
}

void GameState::BeginElimination(
    int player, int position, const PendingHit& continuation,
    std::vector<DeferredEffect> deferred_effects) {
  const auto& character = players_[static_cast<std::size_t>(player)]
                              .positions[static_cast<std::size_t>(position)];
  if (character.character_id < 0 || character.life != 0) {
    throw std::logic_error("character is not ready for elimination");
  }

  PendingElimination elimination;
  elimination.player = player;
  elimination.position = position;
  elimination.character_id = character.character_id;
  elimination.continuation = continuation;
  elimination.deferred_effects = std::move(deferred_effects);
  for (int candidate = 0; candidate < kSideCount; ++candidate) {
    if (character.character_id !=
            static_cast<int>(CharacterName::kBullAnderson) &&
        IsCharacterAbilityActive(candidate, CharacterName::kBullAnderson)) {
      elimination.bull_player = candidate;
    }
    if (character.character_id !=
            static_cast<int>(CharacterName::kBillTightman) &&
        IsCharacterAbilityActive(candidate, CharacterName::kBillTightman)) {
      elimination.bill_player = candidate;
    }
  }
  elimination_stack_.push_back(std::move(elimination));
  if (BullAndersonHasChoices()) {
    pending_ability_ = static_cast<int>(CharacterName::kBullAnderson);
    ability_stage_ = 0;
    decision_player_ = elimination_stack_.back().bull_player;
    phase_ = Phase::kChooseAbility;
    return;
  }
  FinalizeElimination();
}

void GameState::FinishBullAndersonEquipment(int equipment_instance) {
  if (elimination_stack_.empty() ||
      pending_ability_ != static_cast<int>(CharacterName::kBullAnderson)) {
    throw std::logic_error("there is no pending Bull Anderson choice");
  }
  auto& elimination = elimination_stack_.back();
  auto& equipment =
      players_[static_cast<std::size_t>(elimination.player)]
          .positions[static_cast<std::size_t>(elimination.position)]
          .equipment;
  const auto found =
      std::find(equipment.begin(), equipment.end(), equipment_instance);
  if (found == equipment.end()) {
    throw std::logic_error("Bull Anderson Equipment is unavailable");
  }
  equipment.erase(found);
  players_[static_cast<std::size_t>(elimination.bull_player)]
      .hand.push_back(equipment_instance);
  MarkHandCardPublic(elimination.bull_player, equipment_instance);
  ++elimination.bull_picks;
  ability_stage_ = elimination.bull_picks;
  if (BeginWildBillReaction(elimination.player, elimination.bull_player,
                            ResumeKind::kElimination,
                            elimination.position)) {
    return;
  }
  if (!BullAndersonHasChoices()) FinalizeElimination();
}

void GameState::FinishBullAndersonHand(Side origin) {
  if (elimination_stack_.empty() ||
      pending_ability_ != static_cast<int>(CharacterName::kBullAnderson)) {
    throw std::logic_error("there is no pending Bull Anderson choice");
  }
  auto& elimination = elimination_stack_.back();
  if (elimination.player == elimination.bull_player) {
    throw std::logic_error("Bull Anderson cannot draw from his owner's hand");
  }
  auto& hand = players_[static_cast<std::size_t>(elimination.player)].hand;
  std::vector<std::size_t> candidates;
  for (std::size_t index = 0; index < hand.size(); ++index) {
    if (GetCardInstance(hand[index]).origin == origin) candidates.push_back(index);
  }
  if (candidates.empty()) {
    throw std::logic_error("selected Bull Anderson card back is unavailable");
  }
  const std::size_t selected = candidates[rng_.USize(candidates.size())];
  const int card = hand[selected];
  hand.erase(hand.begin() + static_cast<std::ptrdiff_t>(selected));
  ForgetPublicHandCard(elimination.player, card);
  players_[static_cast<std::size_t>(elimination.bull_player)].hand.push_back(card);
  MarkHandCardPublic(elimination.bull_player, card);
  ++elimination.bull_picks;
  ability_stage_ = elimination.bull_picks;
  if (BeginWildBillReaction(elimination.player, elimination.bull_player,
                            ResumeKind::kElimination)) {
    return;
  }
  if (!BullAndersonHasChoices()) FinalizeElimination();
}

void GameState::FinishBullAnderson() {
  if (elimination_stack_.empty() ||
      pending_ability_ != static_cast<int>(CharacterName::kBullAnderson)) {
    throw std::logic_error("there is no pending Bull Anderson choice");
  }
  FinalizeElimination();
}

void GameState::ResumeElimination() {
  if (elimination_stack_.empty()) {
    throw std::logic_error("there is no elimination to resume");
  }
  const auto& elimination = elimination_stack_.back();
  if (elimination.bull_player >= 0 &&
      IsCharacterAbilityActive(elimination.bull_player,
                               CharacterName::kBullAnderson) &&
      BullAndersonHasChoices()) {
    pending_ability_ = static_cast<int>(CharacterName::kBullAnderson);
    ability_stage_ = elimination.bull_picks;
    decision_player_ = elimination.bull_player;
    phase_ = Phase::kChooseAbility;
    return;
  }
  FinalizeElimination();
}

void GameState::FinalizeElimination() {
  if (elimination_stack_.empty()) {
    throw std::logic_error("there is no pending elimination");
  }
  auto& elimination = elimination_stack_.back();
  auto& state = players_[static_cast<std::size_t>(elimination.player)];
  auto& character =
      state.positions[static_cast<std::size_t>(elimination.position)];
  for (const int equipment : character.equipment) discard_.push_back(equipment);
  character = {};

  const int survivor_position = 1 - elimination.position;
  const bool has_survivor =
      state.positions[static_cast<std::size_t>(survivor_position)].life > 0;
  if (state.reserve.empty() && !has_survivor) {
    const int winner = OtherPlayer(elimination.player);
    elimination_stack_.clear();
    pending_ability_ = -1;
    ability_stage_ = 0;
    winner_ = winner;
    decision_player_ = winner;
    phase_ = Phase::kTerminal;
    hit_sequence_.clear();
    deferred_effects_.clear();
    deferred_eliminations_.clear();
    continuation_hits_.clear();
    damage_reaction_stack_.clear();
    pending_damage_.reset();
    hit_sequence_active_ = false;
    defer_eliminations_ = false;
    hit_sequence_resume_player_ = -1;
    return;
  }

  DrawCards(elimination.player, 2);
  if (!state.reserve.empty()) {
    const int replacement = state.reserve.back();
    state.reserve.pop_back();
    character = {replacement, GetCharacter(replacement).max_life, {}};
    RecordVisibleCharacters();
  } else {
    state.active_position = survivor_position;
  }

  if (elimination.bill_player >= 0 &&
      IsCharacterAbilityActive(elimination.bill_player,
                               CharacterName::kBillTightman)) {
    pending_ability_ = static_cast<int>(CharacterName::kBillTightman);
    ability_stage_ = 0;
    decision_player_ = elimination.bill_player;
    phase_ = Phase::kChooseAbility;
    return;
  }

  CompleteElimination();
}

void GameState::CompleteElimination() {
  if (elimination_stack_.empty()) {
    throw std::logic_error("there is no completed elimination");
  }
  PendingElimination elimination = std::move(elimination_stack_.back());
  elimination_stack_.pop_back();
  pending_ability_ = -1;
  ability_stage_ = 0;
  deferred_effects_.insert(
      deferred_effects_.begin(),
      std::make_move_iterator(elimination.deferred_effects.begin()),
      std::make_move_iterator(elimination.deferred_effects.end()));
  ContinueAfterResolvedHit(elimination.continuation);
}

void GameState::FinishBillTightman(bool draw) {
  if (elimination_stack_.empty() ||
      pending_ability_ != static_cast<int>(CharacterName::kBillTightman)) {
    throw std::logic_error("there is no pending Bill Tightman choice");
  }
  if (draw) DrawCards(elimination_stack_.back().bill_player, 2);
  CompleteElimination();
}

std::array<float, kObservationSize> GameState::ObservationTensor(int observer) const {
  if (observer < 0 || observer >= kSideCount) throw std::out_of_range("invalid observer");
  std::array<float, kObservationSize> result{};
  result[obs_layout::kPhaseOffset + static_cast<std::size_t>(phase_)] = 1.0F;
  result[obs_layout::kDecisionPlayerOffset] = decision_player_ == observer ? 1.0F : 0.0F;
  result[obs_layout::kTurnPlayerOffset] = turn_player_ == observer ? 1.0F : 0.0F;
  result[obs_layout::kTurnNumberOffset] = static_cast<float>(turn_number_) / 100.0F;
  result[obs_layout::kDiscardCountOffset] = static_cast<float>(discard_.size()) / 80.0F;
  result[obs_layout::kCommonDeckCountOffset] =
      static_cast<float>(common_deck_.size()) / 80.0F;
  result[obs_layout::kCommonDeckCreatedOffset] = common_deck_created_ ? 1.0F : 0.0F;
  result[obs_layout::kSwappedOffset] = swapped_this_turn_ ? 1.0F : 0.0F;
  result[obs_layout::kBangLimitUsedOffset] = static_cast<float>(bang_cards_played_);
  result[obs_layout::kPendingHitOffset] = pending_hit_ ? 1.0F : 0.0F;
  if (pending_hit_) {
    result[obs_layout::kPendingTargetObserverOffset] =
        pending_hit_->target == observer ? 1.0F : 0.0F;
    result[obs_layout::kBarrelAttemptedOffset] =
        pending_hit_->barrel_attempted ? 1.0F : 0.0F;
    result[obs_layout::kPendingTargetPositionOffset + pending_hit_->target_position] = 1.0F;
  }
  result[obs_layout::kOwnActiveChosenOffset] =
      active_chosen_[static_cast<std::size_t>(observer)] ? 1.0F : 0.0F;
  result[obs_layout::kSetupRevealedOffset] =
      active_chosen_[0] && active_chosen_[1] ? 1.0F : 0.0F;
  result[obs_layout::kOptionalEffectPlayerOffset] =
      optional_effect_player_ == observer ? 1.0F : 0.0F;

  const bool setup_revealed = active_chosen_[0] && active_chosen_[1];
  for (int relative = 0; relative < kSideCount; ++relative) {
    const int player = relative == 0 ? observer : OtherPlayer(observer);
    const auto& state = players_[static_cast<std::size_t>(player)];
    const int offset =
        obs_layout::kPlayerBlocksOffset + relative * obs_layout::kPlayerBlockSize;
    const bool can_see_characters = player == observer || setup_revealed;
    const int active = state.active_position;
    const int rear = 1 - active;
    if (can_see_characters) {
      const auto& active_character = state.positions[static_cast<std::size_t>(active)];
      const auto& rear_character = state.positions[static_cast<std::size_t>(rear)];
      if (active_character.character_id >= 0) {
        result[static_cast<std::size_t>(offset + obs_layout::kActiveCharacterOffset +
                                        active_character.character_id)] = 1.0F;
      }
      if (rear_character.character_id >= 0) {
        result[static_cast<std::size_t>(offset + obs_layout::kRearguardCharacterOffset +
                                        rear_character.character_id)] = 1.0F;
      }
      result[static_cast<std::size_t>(offset + obs_layout::kActiveLifeOffset)] =
          active_character.life / 5.0F;
      result[static_cast<std::size_t>(offset + obs_layout::kActiveMaxLifeOffset)] =
          active_character.character_id < 0 ? 0.0F
                                            : GetCharacter(active_character.character_id).max_life / 5.0F;
      result[static_cast<std::size_t>(offset + obs_layout::kRearguardLifeOffset)] =
          rear_character.life / 5.0F;
      result[static_cast<std::size_t>(offset + obs_layout::kRearguardMaxLifeOffset)] =
          rear_character.character_id < 0 ? 0.0F
                                          : GetCharacter(rear_character.character_id).max_life / 5.0F;
      for (const int instance : active_character.equipment) {
        const auto& equipment = GetCardInstance(instance);
        result[static_cast<std::size_t>(offset + obs_layout::kActiveEquipmentOffset +
                                        static_cast<int>(equipment.name))] += 0.25F;
        result[static_cast<std::size_t>(offset + obs_layout::kActiveEquipmentRevealOffset +
                                        static_cast<int>(equipment.reveal))] += 0.25F;
      }
      for (const int instance : rear_character.equipment) {
        const auto& equipment = GetCardInstance(instance);
        result[static_cast<std::size_t>(offset + obs_layout::kRearguardEquipmentOffset +
                                        static_cast<int>(equipment.name))] += 0.25F;
        result[static_cast<std::size_t>(offset + obs_layout::kRearguardEquipmentRevealOffset +
                                        static_cast<int>(equipment.reveal))] += 0.25F;
      }
    }
    result[static_cast<std::size_t>(offset + obs_layout::kReserveCountOffset)] =
        state.reserve.size() / 12.0F;
    result[static_cast<std::size_t>(offset + obs_layout::kHandCountOffset)] =
        state.hand.size() / 40.0F;
    result[static_cast<std::size_t>(offset + obs_layout::kPersonalDeckCountOffset)] =
        state.deck.size() / 40.0F;
    result[static_cast<std::size_t>(offset + obs_layout::kPersonalDeckExhaustedOffset)] =
        state.personal_deck_exhausted ? 1.0F : 0.0F;
    if (player == observer) {
      for (const int character_id : state.reserve) {
        result[obs_layout::kPrivateReserveOffset +
               static_cast<std::size_t>(character_id)] = 1.0F;
      }
    }
  }

  for (const int instance : players_[static_cast<std::size_t>(observer)].hand) {
    const auto& card = GetCardInstance(instance);
    result[static_cast<std::size_t>(obs_layout::kPrivateHandOffset +
                                    static_cast<int>(card.name))] += 0.125F;
    result[static_cast<std::size_t>(obs_layout::kPrivateHandRevealOffset +
                                    static_cast<int>(card.reveal))] += 0.05F;
  }
  for (const int instance : discard_) {
    const auto& card = GetCardInstance(instance);
    result[static_cast<std::size_t>(obs_layout::kDiscardCardsOffset +
                                    static_cast<int>(card.name))] += 0.125F;
    result[static_cast<std::size_t>(obs_layout::kDiscardRevealOffset +
                                    static_cast<int>(card.reveal))] += 0.05F;
  }
  if (!discard_.empty()) {
    const auto& top = GetCardInstance(discard_.back());
    result[obs_layout::kTopDiscardNameOffset +
           static_cast<std::size_t>(top.name)] = 1.0F;
    result[obs_layout::kTopDiscardRevealOffset +
           static_cast<std::size_t>(top.reveal)] = 1.0F;
    result[obs_layout::kTopDiscardOriginOffset +
           static_cast<std::size_t>(top.origin)] = 1.0F;
  }
  for (std::size_t slot = 0; slot < revealed_cards_.size() && slot < 2; ++slot) {
    const auto& card = GetCardInstance(revealed_cards_[slot]);
    const int offset = slot == 0 ? obs_layout::kFirstRevealedCardOffset
                                 : obs_layout::kSecondRevealedCardOffset;
    result[static_cast<std::size_t>(offset + static_cast<int>(card.name))] = 1.0F;
    result[static_cast<std::size_t>(offset + kCardNameCount +
                                    static_cast<int>(card.reveal))] = 1.0F;
  }
  if (pending_hit_) {
    result[obs_layout::kPendingHitSourceOffset +
           static_cast<std::size_t>(pending_hit_->source)] = 1.0F;
    result[obs_layout::kPendingAttackerObserverOffset] =
        pending_hit_->attacker == observer ? 1.0F : 0.0F;
  }
  if (pending_reveal_) {
    if (pending_reveal_->reason == RevealReason::kSidCurry) {
      result[obs_layout::kSidRevealOffset] = 1.0F;
    } else {
      result[obs_layout::kRevealReasonOffset +
             static_cast<std::size_t>(pending_reveal_->reason)] = 1.0F;
    }
  }
  result[obs_layout::kStartTurnPendingOffset] =
      start_turn_step_ == StartTurnStep::kNone ? 0.0F : 1.0F;
  if (queued_hit_) {
    result[obs_layout::kQueuedHitSourceOffset +
           static_cast<std::size_t>(queued_hit_->source)] = 1.0F;
    result[obs_layout::kQueuedHitTargetObserverOffset] =
        queued_hit_->target == observer ? 1.0F : 0.0F;
    result[obs_layout::kQueuedHitAttackerObserverOffset] =
        queued_hit_->attacker == observer ? 1.0F : 0.0F;
  }
  if (pending_hit_) {
    result[obs_layout::kPendingResponseKindOffset +
           static_cast<std::size_t>(pending_hit_->response)] = 1.0F;
    result[obs_layout::kPendingAvoidsRequiredOffset] =
        static_cast<float>(pending_hit_->avoids_required);
    result[obs_layout::kPendingAvoidsPlayedOffset] =
        static_cast<float>(pending_hit_->avoids_played);
    result[obs_layout::kPendingReturnFireOffset] =
        pending_hit_->return_fire_pending ? 1.0F : 0.0F;
    if (pending_hit_->counter_character >= 0) {
      result[obs_layout::kPendingCounterCharacterOffset +
             static_cast<std::size_t>(pending_hit_->counter_character)] = 1.0F;
    }
    result[obs_layout::kPendingSidAttemptedOffset] =
        pending_hit_->sid_attempted ? 1.0F : 0.0F;
    if (pending_hit_->source_character >= 0) {
      result[obs_layout::kPendingSourceCharacterOffset +
             static_cast<std::size_t>(pending_hit_->source_character)] = 1.0F;
    }
    result[obs_layout::kPendingResumeKindOffset +
           static_cast<std::size_t>(pending_hit_->resume_kind)] = 1.0F;
  }
  result[obs_layout::kHitSequenceActiveOffset] =
      hit_sequence_active_ ? 1.0F : 0.0F;
  result[obs_layout::kContinuationHitCountOffset] =
      static_cast<float>(continuation_hits_.size()) / 8.0F;
  if (!damage_reaction_stack_.empty()) {
    const auto& reaction = damage_reaction_stack_.back();
    result[obs_layout::kDamageReactionResumeOffset +
           static_cast<std::size_t>(
               reaction.continuation.resume_kind)] = 1.0F;
    result[obs_layout::kDamageReactionResumePlayerOffset] =
        reaction.continuation.resume_player == observer ? 1.0F : 0.0F;
    result[obs_layout::kDamageReactionChoosingOriginOffset] =
        reaction.choosing_origin ? 1.0F : 0.0F;
  }
  result[obs_layout::kDamageReactionDepthOffset] =
      static_cast<float>(damage_reaction_stack_.size()) / 4.0F;
  if (movement_mover_ >= 0) {
    result[obs_layout::kMovementActiveOffset] = 1.0F;
    result[obs_layout::kMovementMoverObserverOffset] =
        movement_mover_ == observer ? 1.0F : 0.0F;
    for (const MovementTrigger trigger : movement_triggers_) {
      result[obs_layout::kMovementTriggersOffset +
             static_cast<std::size_t>(trigger)] = 1.0F;
    }
    result[obs_layout::kMovementResumeKindOffset +
           static_cast<std::size_t>(movement_resume_kind_)] = 1.0F;
  }
  result[obs_layout::kDeferredEliminationOffset] =
      deferred_eliminations_.empty() ? 0.0F : 1.0F;
  result[obs_layout::kRemainingSequenceHitsOffset] =
      static_cast<float>(hit_sequence_.size()) / 16.0F;
  std::size_t deferred_effect_count = deferred_effects_.size();
  for (const auto& elimination : elimination_stack_) {
    deferred_effect_count += elimination.deferred_effects.size();
  }
  result[obs_layout::kDeferredHitCountOffset] =
      static_cast<float>(deferred_effect_count) / 8.0F;
  const bool can_see_choice_cards = choice_cards_public_ || effect_actor_ == observer;
  if (can_see_choice_cards) {
    for (const int instance : choice_cards_) {
      const auto& card = GetCardInstance(instance);
      result[obs_layout::kChoiceCardNamesOffset +
             static_cast<std::size_t>(card.name)] += 0.25F;
      result[obs_layout::kChoiceCardRevealsOffset +
             static_cast<std::size_t>(card.reveal)] += 0.25F;
    }
  }
  result[obs_layout::kChoiceCardCountOffset] =
      static_cast<float>(choice_cards_.size()) / 4.0F;
  result[obs_layout::kChoiceCardsPublicOffset] =
      choice_cards_public_ ? 1.0F : 0.0F;
  result[obs_layout::kEffectActorObserverOffset] =
      effect_actor_ == observer ? 1.0F : 0.0F;
  std::array<std::array<int, kSideCount>, kSideCount> back_counts{};
  for (int player = 0; player < kSideCount; ++player) {
    for (const int instance : players_[static_cast<std::size_t>(player)].hand) {
      ++back_counts[static_cast<std::size_t>(player)]
                   [static_cast<std::size_t>(GetCardInstance(instance).origin)];
    }
  }
  const int opponent = OtherPlayer(observer);
  result[obs_layout::kOwnLawBackCountOffset] =
      static_cast<float>(back_counts[static_cast<std::size_t>(observer)][0]) / 40.0F;
  result[obs_layout::kOwnOutlawBackCountOffset] =
      static_cast<float>(back_counts[static_cast<std::size_t>(observer)][1]) / 40.0F;
  result[obs_layout::kOpponentLawBackCountOffset] =
      static_cast<float>(back_counts[static_cast<std::size_t>(opponent)][0]) / 40.0F;
  result[obs_layout::kOpponentOutlawBackCountOffset] =
      static_cast<float>(back_counts[static_cast<std::size_t>(opponent)][1]) / 40.0F;
  for (std::size_t index = 0; index < ability_used_this_turn_.size(); ++index) {
    result[obs_layout::kAbilityUsedOffset + index] =
        ability_used_this_turn_[index] ? 1.0F : 0.0F;
  }
  if (pending_ability_ >= 0) {
    result[obs_layout::kPendingAbilityOffset +
           static_cast<std::size_t>(pending_ability_)] = 1.0F;
  }
  result[obs_layout::kPendingAbilityStageOffset] =
      static_cast<float>(ability_stage_);
  if (pending_replicated_card_ >= 0) {
    result[obs_layout::kPendingReplicatedCardOffset +
           static_cast<std::size_t>(
               GetCardInstance(pending_replicated_card_).name)] = 1.0F;
  }
  if (!elimination_stack_.empty()) {
    const auto& elimination = elimination_stack_.back();
    result[obs_layout::kPendingEliminationOffset] = 1.0F;
    result[obs_layout::kEliminationOwnerObserverOffset] =
        elimination.player == observer ? 1.0F : 0.0F;
    result[obs_layout::kEliminationPositionOffset +
           static_cast<std::size_t>(elimination.position)] = 1.0F;
    result[obs_layout::kEliminationBullPicksOffset] =
        static_cast<float>(elimination.bull_picks);
    result[obs_layout::kEliminationResumeKindOffset +
           static_cast<std::size_t>(
               elimination.continuation.resume_kind)] = 1.0F;
    result[obs_layout::kEliminationResumePlayerOffset] =
        elimination.continuation.resume_player == observer ? 1.0F : 0.0F;
  }
  result[obs_layout::kEliminationDepthOffset] =
      static_cast<float>(elimination_stack_.size()) / 4.0F;
  if (pending_damage_) {
    result[obs_layout::kPendingDamageOffset] = 1.0F;
    result[obs_layout::kDamageOwnerObserverOffset] =
        pending_damage_->player == observer ? 1.0F : 0.0F;
    result[obs_layout::kDamagePositionOffset +
           static_cast<std::size_t>(pending_damage_->position)] = 1.0F;
    result[obs_layout::kDamageRemainingOffset] =
        static_cast<float>(pending_damage_->remaining);
    result[obs_layout::kDamageSourceObserverOffset] =
        pending_damage_->source_player == observer ? 1.0F : 0.0F;
    result[obs_layout::kDamageResumeKindOffset +
           static_cast<std::size_t>(
               pending_damage_->continuation.resume_kind)] = 1.0F;
  }
  for (int relative = 0; relative < kSideCount; ++relative) {
    const int player = relative == 0 ? observer : OtherPlayer(observer);
    const int offset = obs_layout::kPublicHandCardsOffset +
                       relative * obs_layout::kPublicHandBlockSize;
    for (const int instance :
         public_hand_cards_[static_cast<std::size_t>(player)]) {
      const auto& card = GetCardInstance(instance);
      result[static_cast<std::size_t>(offset + static_cast<int>(card.name))] +=
          0.125F;
      result[static_cast<std::size_t>(offset + kCardNameCount +
                                      static_cast<int>(card.reveal))] += 0.05F;
    }
  }
  return result;
}

std::array<float, kTrainingObservationSize>
GameState::TrainingObservationTensor(int observer) const {
  if (observer < 0 || observer >= kSideCount) {
    throw std::out_of_range("invalid training observer");
  }
  std::array<float, kTrainingObservationSize> result{};
  const auto base = ObservationTensor(observer);
  std::copy(base.begin(), base.end(), result.begin());
  result[kTrainingObservationSideOffset +
         static_cast<std::size_t>(players_[static_cast<std::size_t>(observer)]
                                      .side)] = 1.0F;
  for (const int card :
       players_[static_cast<std::size_t>(observer)].hand) {
    result[kTrainingObservationOwnHandOffset +
           static_cast<std::size_t>(card)] = 1.0F;
  }
  for (const int card : discard_) {
    result[kTrainingObservationDiscardOffset +
           static_cast<std::size_t>(card)] = 1.0F;
  }
  for (const int card :
       public_hand_cards_[static_cast<std::size_t>(observer)]) {
    result[kTrainingObservationKnownOwnHandOffset +
           static_cast<std::size_t>(card)] = 1.0F;
  }
  const int opponent = OtherPlayer(observer);
  for (const int card :
       public_hand_cards_[static_cast<std::size_t>(opponent)]) {
    result[kTrainingObservationKnownOpponentHandOffset +
           static_cast<std::size_t>(card)] = 1.0F;
  }
  for (int character = 0; character < kCharacterCount; ++character) {
    if (seen_characters_[static_cast<std::size_t>(observer)]
                        [static_cast<std::size_t>(character)]) {
      result[kTrainingObservationSeenOwnCharactersOffset +
             static_cast<std::size_t>(character)] = 1.0F;
    }
    if (seen_characters_[static_cast<std::size_t>(opponent)]
                        [static_cast<std::size_t>(character)]) {
      result[kTrainingObservationSeenOpponentCharactersOffset +
             static_cast<std::size_t>(character)] = 1.0F;
    }
  }
  return result;
}

std::array<float, kPrivilegedObservationSize>
GameState::PrivilegedObservationTensor(int observer) const {
  if (observer < 0 || observer >= kSideCount) {
    throw std::out_of_range("invalid privileged observer");
  }
  std::array<float, kPrivilegedObservationSize> result{};
  for (int relative = 0; relative < kSideCount; ++relative) {
    const int player = relative == 0 ? observer : OtherPlayer(observer);
    const int offset = relative * kPrivilegedPlayerSize;
    const auto& state = players_[static_cast<std::size_t>(player)];
    for (const int card : state.hand) {
      result[offset + kPrivilegedPlayerHandOffset +
             static_cast<std::size_t>(card)] = 1.0F;
    }
    for (const int card : state.deck) {
      result[offset + kPrivilegedPlayerDeckOffset +
             static_cast<std::size_t>(card)] = 1.0F;
    }
    for (const int character : state.reserve) {
      result[offset + kPrivilegedPlayerReserveOffset +
             static_cast<std::size_t>(character)] = 1.0F;
    }
  }
  for (const int card : common_deck_) {
    result[kPrivilegedCommonDeckOffset +
           static_cast<std::size_t>(card)] = 1.0F;
  }
  return result;
}

std::array<float, kSideCount> GameState::Returns() const {
  if (!IsTerminal()) return {0.0F, 0.0F};
  return winner_ == 0 ? std::array<float, 2>{1.0F, -1.0F}
                      : std::array<float, 2>{-1.0F, 1.0F};
}

void GameState::RecordVisibleCharacters() {
  if (!SetupComplete()) return;
  for (int player = 0; player < kSideCount; ++player) {
    for (const auto& position :
         players_[static_cast<std::size_t>(player)].positions) {
      if (position.character_id >= 0) {
        seen_characters_[static_cast<std::size_t>(player)]
                        [static_cast<std::size_t>(position.character_id)] = true;
      }
    }
  }
}

}  // namespace bang
