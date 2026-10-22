#include "bang/web.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "bang/action_space.hpp"
#include "bang/game.hpp"
#include "bang/heuristic.hpp"
#include "bang/static_data.hpp"
#include "bang/static_policy.hpp"
#ifdef BANG_ENABLE_TORCH
#include <torch/torch.h>

#include "bang/checkpoint.hpp"
#include "bang/policy_network.hpp"
#endif

// Platform sockets come last so Windows macros cannot rewrite project names.
#ifndef __EMSCRIPTEN__
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#endif

namespace bang {
namespace {

#ifndef __EMSCRIPTEN__
#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

void CloseSocket(SocketHandle socket) { ::closesocket(socket); }

std::string SocketErrorText() {
  return "winsock error " + std::to_string(::WSAGetLastError());
}

bool SocketCallInterrupted() { return ::WSAGetLastError() == WSAEINTR; }

void InitializeSockets() {
  WSADATA data{};
  if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    throw std::runtime_error("WSAStartup failed");
  }
}
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;

void CloseSocket(SocketHandle socket) { ::close(socket); }

std::string SocketErrorText() { return std::strerror(errno); }

bool SocketCallInterrupted() { return errno == EINTR; }

void InitializeSockets() {
  // A browser closing its tab mid-response must not kill the server.
  std::signal(SIGPIPE, SIG_IGN);
}
#endif

// Winsock takes int lengths; one request chunk always fits.
long long ReceiveSome(SocketHandle client, char* data, std::size_t size) {
  return ::recv(client, data, static_cast<int>(size), 0);
}

long long SendSome(SocketHandle client, const char* data, std::size_t size) {
  return ::send(client, data, static_cast<int>(std::min<std::size_t>(size, 1 << 30)), 0);
}
#endif

constexpr int kMaxBotSteps = 2000;
constexpr std::size_t kMaxLogEntries = 120;
#ifndef __EMSCRIPTEN__
constexpr std::size_t kMaxRequestBytes = 64 * 1024;
#endif

std::uint64_t WebEntropy() {
  std::random_device random;
  const auto time = static_cast<std::uint64_t>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  return time ^ (static_cast<std::uint64_t>(random()) << 32U) ^ random();
}

// A starting deck reordered so `draw_order` comes off the top, used by the
// smoke tests to build reproducible situations.
std::vector<int> DeckWithTop(Side side, const std::vector<CardName>& draw_order) {
  std::vector<int> deck = StartingDeck(side);
  std::vector<int> selected;
  selected.reserve(draw_order.size());
  for (const CardName name : draw_order) {
    const auto found = std::find_if(
        deck.begin(), deck.end(), [name](int instance) {
          return GetCardInstance(instance).name == name;
        });
    if (found == deck.end()) {
      throw std::runtime_error("smoke deck is missing a requested card");
    }
    selected.push_back(*found);
    deck.erase(found);
  }
  for (auto iterator = selected.rbegin(); iterator != selected.rend();
       ++iterator) {
    deck.push_back(*iterator);
  }
  return deck;
}

bool StartsWith(std::string_view value, std::string_view prefix) {
  return value.substr(0, prefix.size()) == prefix;
}

#ifndef __EMSCRIPTEN__
bool EndsWith(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.substr(value.size() - suffix.size()) == suffix;
}
#endif

const JsonValue::Object& JsonObject(const JsonValue& value,
                                    std::string_view context) {
  const auto* object = std::get_if<JsonValue::Object>(&value.storage());
  if (object == nullptr) {
    throw std::runtime_error(std::string(context) + " must be an object");
  }
  return *object;
}

const JsonValue::Array& JsonArray(const JsonValue& value,
                                  std::string_view context) {
  const auto* array = std::get_if<JsonValue::Array>(&value.storage());
  if (array == nullptr) {
    throw std::runtime_error(std::string(context) + " must be an array");
  }
  return *array;
}

std::optional<int> OptionalInt(const JsonValue::Object& object,
                               const std::string& key) {
  const auto found = object.find(key);
  if (found == object.end()) return std::nullopt;
  const auto* value = std::get_if<double>(&found->second.storage());
  if (value == nullptr) throw std::invalid_argument(key + " must be a number");
  return static_cast<int>(*value);
}

std::string PhaseName(Phase phase) {
  switch (phase) {
    case Phase::kChooseActive: return "choose_active";
    case Phase::kPlay: return "play";
    case Phase::kDiscard: return "discard";
    case Phase::kRespondToHit: return "respond_to_hit";
    case Phase::kChooseReveal: return "choose_reveal";
    case Phase::kChooseRemington: return "choose_remington";
    case Phase::kForcedDiscard: return "forced_discard";
    case Phase::kChooseCarbine: return "choose_carbine";
    case Phase::kChooseShotgun: return "choose_shotgun";
    case Phase::kDuelResponse: return "duel_response";
    case Phase::kChooseWagonDiscard: return "choose_wagon_discard";
    case Phase::kChooseGeneralStore: return "choose_general_store";
    case Phase::kChooseSchoolmarm: return "choose_schoolmarm";
    case Phase::kChooseStealing: return "choose_stealing";
    case Phase::kChooseAbility: return "choose_ability";
    case Phase::kTerminal: return "terminal";
  }
  throw std::logic_error("unknown phase");
}

std::string PhaseLabel(Phase phase) {
  switch (phase) {
    case Phase::kChooseActive: return "Choose your active character";
    case Phase::kPlay: return "Play cards or end the turn";
    case Phase::kDiscard: return "Discard down to your hand limit";
    case Phase::kRespondToHit: return "Answer the incoming hit";
    case Phase::kChooseReveal: return "Choose a revealed symbol";
    case Phase::kChooseRemington: return "Resolve Remington";
    case Phase::kForcedDiscard: return "Choose a card to discard";
    case Phase::kChooseCarbine: return "Resolve Carbine";
    case Phase::kChooseShotgun: return "Choose who swaps";
    case Phase::kDuelResponse: return "Discard a hit card or concede the Duel";
    case Phase::kChooseWagonDiscard: return "Choose one Wagon card to discard";
    case Phase::kChooseGeneralStore: return "Choose a General Store card";
    case Phase::kChooseSchoolmarm: return "Choose Equipment to steal";
    case Phase::kChooseStealing: return "Choose a card back or Equipment";
    case Phase::kChooseAbility: return "Resolve a character ability";
    case Phase::kTerminal: return "The duel is over";
  }
  throw std::logic_error("unknown phase");
}

// One helper sentence per phase, shown under the phase pill so the player can
// see what the decision is about without knowing the rulebook by heart.
// Phase::kChooseAbility is deliberately absent: it covers a dozen different
// abilities and gets the character-specific prompt built below.
std::string PhaseHint(Phase phase) {
  switch (phase) {
    case Phase::kChooseActive:
      return "Both sides commit face down. The active character makes the "
             "plays and is the default target; the other one waits in the "
             "rearguard.";
    case Phase::kPlay: return "";
    case Phase::kDiscard:
      return "You are over your hand limit — discard cards until you are back "
             "down to it.";
    case Phase::kRespondToHit:
      return "Answer with an avoid card to dodge the shot, or take the hit "
             "and lose 1 life.";
    case Phase::kChooseReveal:
      return "Bart Masterson flipped two cards. Pick the one whose symbol "
             "counts for this reveal.";
    case Phase::kChooseRemington:
      return "Your Remington shot was avoided, so you may draw 1 card.";
    case Phase::kForcedDiscard:
      return "An effect makes you throw a card away. Choose which one goes.";
    case Phase::kChooseCarbine:
      return "Your Carbine shot was avoided, so you may discard one Equipment "
             "card from either active character.";
    case Phase::kChooseShotgun:
      return "Your Shotgun shot was avoided. Swap either your own two "
             "characters or the opponent's — the active one becomes the "
             "rearguard and back.";
    case Phase::kDuelResponse:
      return "The Duel goes back and forth: discard a card that can shoot to "
             "keep it running, or concede and lose 1 life.";
    case Phase::kChooseWagonDiscard:
      return "The Wagon drew 3 cards. Keep 2 — pick the one you discard.";
    case Phase::kChooseGeneralStore:
      return "Take one of the revealed cards; the players alternate until the "
             "cards run out.";
    case Phase::kChooseSchoolmarm:
      return "Take any Equipment card in play into your hand, except from the "
             "opponent's rearguard.";
    case Phase::kChooseStealing:
      return "Either steal a random hand card by picking a deck back, or "
             "discard an Equipment card that is in play.";
    case Phase::kChooseAbility: return "";
    case Phase::kTerminal: return "";
  }
  throw std::logic_error("unknown phase");
}

// Phase::kChooseAbility is one phase for many decisions, so the player is told
// which character is asking and what the options actually do. Abilities that
// ask twice (a cost first, then the effect) are told apart by the shape of
// their legal actions, which keeps the engine's internal staging private.
struct AbilityPrompt {
  std::string label;         // phase pill text
  std::string hint;          // explanation shown under the pill
  std::string accept;        // "use it" button, when the ability is optional
  std::string accept_detail;
  std::string skip;          // "leave it" button
  std::string skip_detail;
  std::string pick_detail;   // detail for card picks (select / discard)
  std::string back_detail;   // detail for deck-back picks
};

AbilityPrompt AbilityPromptFor(const GameState& state) {
  const int character_id = state.PendingAbilityCharacter();
  if (character_id < 0) return {};
  const auto& character = GetCharacter(character_id);
  const std::string name(character.display_name);
  const std::vector<int> legal = state.LegalActions();
  const auto has_kind = [&legal](ActionKind kind) {
    return std::any_of(legal.begin(), legal.end(), [kind](int action) {
      return DecodeAction(action).kind == kind;
    });
  };
  const auto card_name = [](int instance) {
    return std::string(GetCard(GetCardInstance(instance).name).display_name);
  };

  AbilityPrompt prompt;
  prompt.label = name + "'s ability";
  prompt.hint = name + ": " + std::string(character.ability_text);
  prompt.accept = "Use the ability";
  prompt.accept_detail = std::string(character.ability_text);
  prompt.skip = "Skip it";
  prompt.skip_detail = name + " stays out of it this time";
  prompt.pick_detail = "Resolve " + name + "'s ability with this card";

  switch (static_cast<CharacterName>(character_id)) {
    case CharacterName::kBuffaloBell:
      prompt.label = "Buffalo Bell can step in";
      prompt.hint =
          "Your other character is about to lose life. Buffalo Bell may take "
          "it instead — he can only do that while the loss would not be his "
          "last life. Skip and the damage stays where it was aimed.";
      prompt.accept = "Buffalo Bell takes it";
      prompt.accept_detail = "He loses the life instead of your other character";
      prompt.skip = "Leave the damage";
      prompt.skip_detail = "The targeted character loses the life";
      break;
    case CharacterName::kBillTightman:
      prompt.label = "Bill Tightman: draw 2 cards?";
      prompt.hint =
          "A character was just eliminated, so Bill Tightman may draw 2 cards. "
          "It costs nothing — only your end-of-turn hand limit argues against "
          "it.";
      prompt.accept = "Draw 2 cards";
      prompt.accept_detail = "Bill Tightman's reward for the elimination";
      prompt.skip = "Skip the draw";
      prompt.skip_detail = "Take no cards for the elimination";
      break;
    case CharacterName::kBullAnderson:
      prompt.label = "Bull Anderson loots the fallen";
      prompt.hint =
          "Take from the eliminated character: pick one of their Equipment "
          "cards, or pick a deck back to take a random card with that back out "
          "of their hand. Bull Anderson may take up to 2 cards — stop whenever "
          "you like.";
      prompt.skip = "Take nothing more";
      prompt.skip_detail = "Bull Anderson stops looting";
      prompt.pick_detail = "Bull Anderson takes this Equipment";
      prompt.back_detail = "A random hand card with this back joins your hand";
      break;
    case CharacterName::kDalonRanger:
      prompt.label = "Dalon Ranger: draw 1 card?";
      prompt.hint =
          "The opponent just cost Dalon Ranger a life that was not his last, "
          "so he may draw 1 card in return — once for every life lost.";
      prompt.accept = "Draw 1 card";
      prompt.accept_detail = "Dalon Ranger's compensation for the life lost";
      prompt.skip = "Skip the draw";
      prompt.skip_detail = "Take no card for the life lost";
      break;
    case CharacterName::kWyattEar:
      prompt.label = "Wyatt Ear: draw 1 card?";
      prompt.hint =
          "The opponent avoided your hit, so Wyatt Ear may draw 1 card as "
          "consolation.";
      prompt.accept = "Draw 1 card";
      prompt.accept_detail = "Wyatt Ear's consolation for the avoided hit";
      prompt.skip = "Skip the draw";
      prompt.skip_detail = "Take no card for the avoided hit";
      break;
    case CharacterName::kJango:
      prompt.label = "Jango: draw 1 card?";
      prompt.hint =
          "The opponent moved their active token, so Jango may draw 1 card "
          "before play continues.";
      prompt.accept = "Draw 1 card";
      prompt.accept_detail = "Jango's reward for the opponent's swap";
      prompt.skip = "Skip the draw";
      prompt.skip_detail = "Let the swap pass without drawing";
      break;
    case CharacterName::kPatGarret:
      prompt.label = "Pat Garret: raid the opponent's deck?";
      prompt.hint =
          "Your second Draw Phase card showed a Horseshoe, so Pat Garret may "
          "take one extra card off the opponent's deck. It fills your hand and "
          "thins their deck at the same time.";
      prompt.accept = "Draw from their deck";
      prompt.accept_detail = "One extra card, taken off the opponent's deck";
      prompt.skip = "Skip the extra card";
      prompt.skip_detail = "Leave the opponent's deck alone";
      break;
    case CharacterName::kSlimPoet:
      if (has_kind(ActionKind::kChooseCardBack)) {
        prompt.label = "Slim Poet: pick a card back";
        prompt.hint =
            "Choose which back to grab. One random opponent hand card with "
            "that back is discarded — you do not get to see it first.";
        prompt.back_detail = "A random opponent card with this back is discarded";
      } else {
        prompt.label = "Slim Poet: hit back?";
        prompt.hint =
            "The opponent just cost Slim Poet a life that was not his last, so "
            "he may make them discard a random hand card — once for every life "
            "lost. You pick the card back, the card itself is random.";
        prompt.accept = "Discard one of their cards";
        prompt.accept_detail = "You then choose which card back it comes from";
        prompt.skip = "Let it go";
        prompt.skip_detail = "The opponent keeps their hand";
      }
      break;
    case CharacterName::kCattleKatie:
      prompt.label = "Cattle Katie: which Equipment goes?";
      prompt.hint =
          "Pick one of Cattle Katie's Equipment cards to discard — she draws 2 "
          "cards for it. Always-active Equipment cannot be traded in, and the "
          "ability works once per turn.";
      prompt.pick_detail = "Discard it and draw 2 cards";
      break;
    case CharacterName::kSoundanceKid:
      if (state.ChoiceCards().empty()) {
        prompt.label = "Soundance Kid: pay a card";
        prompt.hint =
            "Choose the hand card Soundance Kid discards. He then draws one "
            "card from each deck, and you keep one of the two.";
        prompt.pick_detail = "Discard it to start the draw";
      } else {
        prompt.label = "Soundance Kid: keep one card";
        prompt.hint =
            "These are the two cards Soundance Kid drew. The one you pick is "
            "discarded; the other one joins your hand.";
        prompt.pick_detail = "Discard this one and keep the other";
      }
      break;
    case CharacterName::kPearlHat: {
      const bool aiming = has_kind(ActionKind::kPlayCard);
      std::string copied;
      if (aiming) {
        for (const int action : legal) {
          const ActionKey key = DecodeAction(action);
          if (key.kind == ActionKind::kPlayCard) {
            copied = card_name(key.card_instance);
            break;
          }
        }
      } else if (!state.DiscardPile().empty()) {
        copied = card_name(state.DiscardPile().back());
      }
      if (aiming) {
        prompt.label = copied.empty() ? "Pearl Hat: aim the copy"
                                      : "Pearl Hat: aim the copied " + copied;
        prompt.hint =
            "Pearl Hat replays " +
            (copied.empty() ? std::string("the discarded Action")
                            : copied) +
            " off the discard pile. Choose where the copied effect lands — the "
            "card itself stays in the discard.";
        prompt.pick_detail = "Resolve the copied effect here";
      } else {
        prompt.label = "Pearl Hat: pay a card";
        prompt.hint =
            "Choose the hand card Pearl Hat discards to copy " +
            (copied.empty() ? std::string("the Action card on top of the "
                                          "discard pile")
                            : copied + " off the discard pile") +
            ". You aim the copied effect next; the ability works once per "
            "turn.";
        prompt.pick_detail = "Discard it to pay for the copy";
      }
      break;
    }
    default: break;
  }
  return prompt;
}

std::string RevealReasonName(GameState::RevealReason reason) {
  switch (reason) {
    case GameState::RevealReason::kBarrel: return "barrel";
    case GameState::RevealReason::kDynamite: return "dynamite";
    case GameState::RevealReason::kRattlesnake: return "rattlesnake";
    case GameState::RevealReason::kSidCurry: return "sid_curry";
  }
  throw std::logic_error("unknown reveal reason");
}

std::string RevealName(RevealSymbol reveal) {
  switch (reveal) {
    case RevealSymbol::kBarrel: return "barrel";
    case RevealSymbol::kSnake: return "snake";
    case RevealSymbol::kDynamite: return "dynamite";
    case RevealSymbol::kHorseshoe: return "horseshoe";
  }
  throw std::logic_error("unknown reveal symbol");
}

std::string CardKindName(CardKind kind) {
  switch (kind) {
    case CardKind::kAction: return "action";
    case CardKind::kEquipment: return "equipment";
    case CardKind::kBang: return "bang";
    case CardKind::kResource: return "resource";
  }
  throw std::logic_error("unknown card kind");
}

std::string TargetLabel(Target target) {
  switch (target) {
    case Target::kOwnActive: return "your active character";
    case Target::kOwnRearguard: return "your rearguard";
    case Target::kOpponentActive: return "opponent active character";
    case Target::kOpponentRearguard: return "opponent rearguard";
  }
  throw std::logic_error("unknown target");
}

JsonValue CardJson(int instance) {
  const auto& physical = GetCardInstance(instance);
  const auto& card = GetCard(physical.name);
  return JsonValue::Object{
      {"instance", instance},
      {"key", std::string(card.key)},
      {"name", std::string(card.display_name)},
      {"rulesText", std::string(card.rules_text)},
      {"kind", CardKindName(card.kind)},
      {"origin", std::string(SideName(physical.origin))},
      {"reveal", RevealName(physical.reveal)},
      {"hit", card.has_hit},
      {"avoid", card.has_avoid},
      {"reloadable", card.reloadable},
      {"alwaysActive", card.always_active},
  };
}

JsonValue CharacterJson(int character_id) {
  const auto& character = GetCharacter(character_id);
  return JsonValue::Object{
      {"id", character.id},
      {"key", std::string(character.key)},
      {"name", std::string(character.display_name)},
      {"abilityText", std::string(character.ability_text)},
      {"side", std::string(SideName(character.side))},
      {"maxLife", character.max_life},
      {"alwaysActive", character.always_active},
  };
}

JsonValue CardsJson(const std::vector<int>& instances) {
  JsonValue::Array result;
  result.reserve(instances.size());
  for (const int instance : instances) result.emplace_back(CardJson(instance));
  return result;
}

std::string ActionKindName(ActionKind kind) {
  switch (kind) {
    case ActionKind::kChooseActive: return "choose_active";
    case ActionKind::kChooseReveal: return "choose_reveal";
    case ActionKind::kResolveOptional: return "optional";
    case ActionKind::kChooseSwap: return "choose_swap";
    case ActionKind::kChooseCardBack: return "choose_back";
    case ActionKind::kEndPlay: return "end_turn";
    case ActionKind::kSwap: return "swap";
    case ActionKind::kTakeHit: return "take_hit";
    case ActionKind::kPlayCard: return "play_card";
    case ActionKind::kReloadCard: return "reload";
    case ActionKind::kDiscardCard: return "discard";
    case ActionKind::kAvoidHit: return "avoid";
    case ActionKind::kSelectCard: return "select_card";
    case ActionKind::kPlayAsColt: return "play_as_colt";
    case ActionKind::kUseAbility: return "use_ability";
    case ActionKind::kUseResource: return "use_resource";
  }
  throw std::logic_error("unknown action kind");
}

std::optional<int> ForcedHitResponseAction(
    const GameState& state,
    int human_player) {
  if (state.IsTerminal() || state.CurrentPlayer() != human_player) {
    return std::nullopt;
  }
  const std::vector<int> legal = state.LegalActions();
  if (legal.size() != 1) return std::nullopt;
  if (state.CurrentPhase() == Phase::kRespondToHit &&
      legal.front() == kTakeHitAction) {
    return legal.front();
  }
  if (state.CurrentPhase() == Phase::kDuelResponse &&
      legal.front() == kSkipOptionalAction) {
    return legal.front();
  }
  return std::nullopt;
}

JsonValue ActionJson(const GameState& state, int action_id) {
  const ActionKey action = DecodeAction(action_id);
  JsonValue::Object result{
      {"id", action_id},
      {"kind", ActionKindName(action.kind)},
      {"engineText", ActionToString(action_id)},
  };
  // Empty unless a character ability is waiting on this decision, in which
  // case it names what each option actually does.
  const AbilityPrompt ability = AbilityPromptFor(state);
  std::string label;
  std::string detail;
  switch (action.kind) {
    case ActionKind::kChooseActive: {
      const auto& position = state.Players()[static_cast<std::size_t>(state.CurrentPlayer())]
                                 .positions[static_cast<std::size_t>(action.card_instance)];
      const auto& character = GetCharacter(position.character_id);
      label = "Make " + std::string(character.display_name) + " active";
      detail = "Your other character begins as rearguard";
      result["character"] = CharacterJson(position.character_id);
      break;
    }
    case ActionKind::kChooseReveal: {
      label = action.card_instance == 0 ? "Choose first reveal" : "Choose second reveal";
      if (action.card_instance >= 0 &&
          action.card_instance < static_cast<int>(state.RevealedCards().size())) {
        result["card"] = CardJson(
            state.RevealedCards()[static_cast<std::size_t>(action.card_instance)]);
      }
      break;
    }
    case ActionKind::kResolveOptional: {
      const bool accept = action.card_instance == 1;
      result["accept"] = accept;
      if (state.CurrentPhase() == Phase::kDuelResponse && !accept) {
        label = "Concede the Duel";
        detail = "Lose 1 life";
      } else if (!ability.label.empty()) {
        label = accept ? ability.accept : ability.skip;
        detail = accept ? ability.accept_detail : ability.skip_detail;
      } else {
        label = accept ? "Use the effect" : "Skip the effect";
        detail = accept ? "Accept this optional ability" :
                          "Continue without using it";
      }
      break;
    }
    case ActionKind::kChooseSwap:
      label = action.card_instance == 1 ? "Swap opponent characters" :
                                         "Swap your characters";
      break;
    case ActionKind::kChooseCardBack:
      label = action.card_instance == 0 ? "Choose a Law card back" :
                                         "Choose an Outlaw card back";
      detail = ability.back_detail.empty()
                   ? "The matching card face is selected at random"
                   : ability.back_detail;
      result["origin"] = action.card_instance == 0 ? "Law" : "Outlaw";
      break;
    case ActionKind::kEndPlay:
      label = "End turn";
      detail = "Proceed to the hand-limit check";
      break;
    case ActionKind::kSwap:
      label = "Swap active and rearguard";
      detail = "Once per turn";
      break;
    case ActionKind::kTakeHit:
      label = state.CurrentPhase() == Phase::kDuelResponse ? "Concede the Duel" :
                                                            "Take the hit";
      detail = "Lose 1 life";
      break;
    case ActionKind::kPlayCard:
    case ActionKind::kPlayAsColt: {
      const auto& card = GetCard(GetCardInstance(action.card_instance).name);
      const std::string target = TargetLabel(static_cast<Target>(action.target));
      label = action.kind == ActionKind::kPlayAsColt
                  ? "Play " + std::string(card.display_name) + " as Colt"
                  : "Play " + std::string(card.display_name);
      detail = "Target: " + target;
      // Pearl Hat replays a card off the discard pile rather than playing one
      // out of hand — say so instead of pretending she holds it.
      if (!ability.label.empty()) {
        label = "Copy " + std::string(card.display_name);
        detail += ability.pick_detail.empty() ? "" : " — " + ability.pick_detail;
      }
      result["card"] = CardJson(action.card_instance);
      result["cardInstance"] = action.card_instance;
      result["target"] = TargetName(static_cast<Target>(action.target));
      result["targetLabel"] = target;
      break;
    }
    case ActionKind::kReloadCard:
    case ActionKind::kDiscardCard:
    case ActionKind::kAvoidHit:
    case ActionKind::kSelectCard: {
      const auto& card = GetCard(GetCardInstance(action.card_instance).name);
      if (action.kind == ActionKind::kReloadCard) label = "Reload " + std::string(card.display_name);
      if (action.kind == ActionKind::kDiscardCard) label = "Discard " + std::string(card.display_name);
      if (action.kind == ActionKind::kAvoidHit) label = "Respond with " + std::string(card.display_name);
      if (action.kind == ActionKind::kSelectCard) label = "Choose " + std::string(card.display_name);
      if (action.kind != ActionKind::kReloadCard &&
          action.kind != ActionKind::kAvoidHit) {
        detail = ability.pick_detail;
      }
      result["card"] = CardJson(action.card_instance);
      result["cardInstance"] = action.card_instance;
      break;
    }
    case ActionKind::kUseResource: {
      const auto& card = GetCard(GetCardInstance(action.card_instance).name);
      label = "Use " + std::string(card.display_name);
      detail = action.target == static_cast<int>(Target::kOwnActive)
                   ? "Discard it from play for its effect"
                   : "Target: " + TargetLabel(static_cast<Target>(action.target));
      result["card"] = CardJson(action.card_instance);
      result["cardInstance"] = action.card_instance;
      result["target"] = TargetName(static_cast<Target>(action.target));
      result["targetLabel"] = TargetLabel(static_cast<Target>(action.target));
      break;
    }
    case ActionKind::kUseAbility: {
      const auto& character = GetCharacter(action.card_instance);
      label = "Use " + std::string(character.display_name);
      detail = "Activate this character's ability";
      result["character"] = CharacterJson(action.card_instance);
      break;
    }
  }
  result["label"] = std::move(label);
  result["detail"] = std::move(detail);
  return result;
}

// What a card flipped off a deck did, in words. Several effects reveal for the
// same "Barrel" symbol (a Barrel card, Sid Curry's ability), so the line names
// the effect, the card, whose deck it came from, and the outcome.
std::string RevealText(const GameState::RevealEvent& event) {
  const auto& card = GetCard(GetCardInstance(event.card_instance).name);
  std::string side(SideName(static_cast<Side>(event.player)));
  if (!side.empty()) {
    side.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(side.front())));
  }
  const std::string drawn =
      std::string(card.display_name) + " from the " + side + " deck";
  switch (event.reason) {
    case GameState::RevealReason::kSidCurry:
      return "Sid Curry's ability reveals " + drawn +
             (event.triggered
                  ? ": a Barrel symbol, so the hit is avoided and countered at "
                    "the attacker's active character"
                  : ": no Barrel symbol, so the hit stands");
    case GameState::RevealReason::kBarrel:
      return "Barrel card reveals " + drawn +
             (event.triggered ? ": a Barrel symbol, so the hit is avoided"
                              : ": no Barrel symbol, so the hit stands");
    case GameState::RevealReason::kDynamite:
      return "Dynamite reveals " + drawn +
             (event.triggered ? ": it explodes for 3 life"
                              : ": no Dynamite symbol, so it passes on");
    case GameState::RevealReason::kRattlesnake:
      return "Rattlesnake reveals " + drawn +
             (event.triggered ? ": a Snake symbol, so it bites for 1 life"
                              : ": no Snake symbol, so no bite");
  }
  return "Reveals " + drawn;
}

// The side whose effect it was: Sid Curry reveals from the attacker's deck;
// every other reveal comes off the affected side's own deck.
int RevealOwner(const GameState::RevealEvent& event) {
  return event.reason == GameState::RevealReason::kSidCurry
             ? 1 - event.player
             : event.player;
}

std::string ReadFile(const std::filesystem::path& path);

struct LogEntry {
  int player = -1;
  std::string text;
  std::string engine_text;
  // Monotonic across the session, unlike the entry's position in a log the
  // server trims, so the browser can tell exactly which entries are new.
  std::uint64_t sequence = 0;
};

struct CharacterSnapshot {
  int player = -1;
  int character_id = -1;
  int life = 0;
};

std::vector<CharacterSnapshot> CharacterSnapshots(const GameState& state) {
  std::vector<CharacterSnapshot> result;
  result.reserve(4);
  for (int player = 0; player < kSideCount; ++player) {
    for (const CharacterState& character :
         state.Players()[static_cast<std::size_t>(player)].positions) {
      if (character.character_id >= 0) {
        result.push_back({player, character.character_id, character.life});
      }
    }
  }
  return result;
}

struct WebSession {
  std::string id;
  std::uint64_t seed = 1;
  int human_player = 0;
  int characters_per_side = 4;
  GameState state;
  FastRng bot_rng;
  std::vector<LogEntry> log;
  std::uint64_t next_log_sequence = 0;
  // Every action applied to `state`, human and bot alike. Replaying this
  // list against a fresh GameState with the same seed reproduces the game,
  // which is how sessions are restored after a server restart.
  std::vector<int> history;
  std::uint64_t last_touch = 0;

  WebSession(std::string session_id, std::uint64_t session_seed,
             int human, int characters)
      : id(std::move(session_id)),
        seed(session_seed),
        human_player(human),
        characters_per_side(characters),
        state(GameConfig{.seed = session_seed,
                         .characters_per_side = characters}),
        bot_rng(session_seed ^ 0xa0761d6478bd642fULL) {}
};

bool ValidSessionId(const std::string& id) {
  if (id.empty() || id.size() > 64) return false;
  return std::all_of(id.begin(), id.end(), [](unsigned char ch) {
    return std::isxdigit(ch) != 0;
  });
}

class BangWebApp {
 public:
  explicit BangWebApp(WebConfig config)
      : config_(std::move(config)), session_rng_(WebEntropy()) {
    if (config_.max_sessions <= 0) {
      throw std::invalid_argument("max_sessions must be positive");
    }
    if (!config_.static_policy.empty() &&
        (!config_.checkpoint.empty() || !config_.run_dir.empty())) {
      throw std::invalid_argument(
          "static policy and LibTorch checkpoint options are mutually exclusive");
    }
    if (!config_.static_policy.empty()) {
      try {
        static_policy_ = LoadStaticPolicyFile(config_.static_policy);
        trained_ = true;
        policy_iteration_ = static_policy_->iteration;
        policy_path_ = config_.static_policy;
      } catch (const std::exception& error) {
        policy_error_ = error.what();
        if (config_.require_trained_policy) throw;
      }
    }
#ifdef BANG_ENABLE_TORCH
    std::filesystem::path checkpoint = config_.checkpoint;
    if (checkpoint.empty() && !config_.run_dir.empty()) {
      for (const auto& candidate : {
               config_.run_dir / "champion" / "policy.pt",
               config_.run_dir / "main" / "latest.pt",
               config_.run_dir / "latest.pt"}) {
        if (std::filesystem::is_regular_file(candidate)) {
          checkpoint = candidate;
          break;
        }
      }
    }
    if (!checkpoint.empty() && !trained_) {
      try {
        torch::set_num_threads(std::max(1, config_.torch_threads));
        trained_policy_ = LoadPolicyFromCheckpoint(checkpoint);
        trained_policy_->eval();
        const auto metadata = LoadCheckpointMetadata(checkpoint);
        trained_ = true;
        policy_iteration_ = metadata.iteration;
        policy_path_ = checkpoint;
      } catch (const std::exception& error) {
        policy_error_ = error.what();
        trained_policy_ = nullptr;
        if (config_.require_trained_policy) throw;
      }
    }
#else
    if (!trained_ &&
        (config_.require_trained_policy || !config_.checkpoint.empty() ||
         !config_.run_dir.empty())) {
      throw std::runtime_error(
          "this binary was built without LibTorch policy support");
    }
#endif
    if (config_.require_trained_policy && !trained_) {
      throw std::runtime_error("a compatible trained checkpoint is required");
    }
    if (!config_.session_dir.empty()) {
      std::error_code error;
      std::filesystem::create_directories(config_.session_dir, error);
      if (error) {
        std::cerr << "session persistence disabled: " << error.message() << '\n';
        config_.session_dir.clear();
      }
    }
  }

  JsonValue HealthJson() const {
    return JsonValue::Object{
        {"ok", true},
        {"engine", "bang_duel"},
        {"ai", trained_ ? "trained_checkpoint" : "frontier_heuristic"},
        {"trained", trained_},
        {"sessions", static_cast<int>(sessions_.size())},
    };
  }

  bool IsTrained() const {
    return trained_;
  }

  JsonValue NewSession(std::optional<int> requested_seed,
                       std::optional<int> requested_human,
                       std::optional<int> requested_characters) {
    EvictIfFull();
    const int human = requested_human.value_or(0);
    if (human < 0 || human >= kSideCount) {
      throw std::invalid_argument("humanPlayer must be 0 (Law) or 1 (Outlaw)");
    }
    const int characters = requested_characters.value_or(4);
    if (characters < 2 || characters > kCharactersPerSide) {
      throw std::invalid_argument("charactersPerSide must be between 2 and 12");
    }
    const std::uint64_t seed = requested_seed.has_value() && *requested_seed > 0
                                   ? static_cast<std::uint64_t>(*requested_seed)
                                   : session_rng_.NextU64() & 0x7fffffffULL;
    auto session = std::make_unique<WebSession>(NewSessionId(), seed, human, characters);
    AppendLog(*session, -1,
              "New duel. You command the " +
                  std::string(SideName(static_cast<Side>(human))) + " side.",
              "");
    AdvanceAutomaticActions(*session);
    session->last_touch = ++touch_counter_;
    SaveSession(*session);
    const std::string id = session->id;
    sessions_[id] = std::move(session);
    return Serialize(*sessions_.at(id));
  }

  JsonValue SessionJson(const std::string& id) {
    WebSession& session = GetSession(id);
    const std::size_t history_size = session.history.size();
    AdvanceAutomaticActions(session);
    if (session.history.size() != history_size) SaveSession(session);
    return Serialize(session);
  }

  JsonValue ApplyHumanAction(const std::string& id, int action_id) {
    WebSession& session = GetSession(id);
    AdvanceAutomaticActions(session);
    if (session.state.IsTerminal()) throw std::runtime_error("game is terminal");
    if (session.state.CurrentPlayer() != session.human_player) {
      throw std::runtime_error("it is not the human player's decision");
    }
    const std::vector<int> legal = session.state.LegalActions();
    if (std::find(legal.begin(), legal.end(), action_id) == legal.end()) {
      throw std::invalid_argument("action is not legal in the current state");
    }
    ApplyLoggedAction(session, session.human_player, action_id, false);
    // The player's view after their own move and after each automatic one, so
    // the browser can play the opponent's turn back a move at a time instead
    // of jumping straight to where it ended.
    JsonValue::Array replay{Serialize(session)};
    AdvanceAutomaticActions(session, &replay);
    TrimLog(session);
    SaveSession(session);
    if (replay.size() <= 1) return Serialize(session);
    JsonValue::Object result =
        std::get<JsonValue::Object>(Serialize(session).storage());
    result["replay"] = std::move(replay);
    return result;
  }

  WebSession& SessionForTest(const std::string& id) { return GetSession(id); }

 private:
  std::string NewSessionId() {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16)
        << session_rng_.NextU64() << std::setw(16) << session_rng_.NextU64();
    return out.str();
  }

  WebSession& GetSession(const std::string& id) {
    auto found = sessions_.find(id);
    if (found == sessions_.end()) {
      auto restored = LoadSession(id);
      if (restored == nullptr) throw std::runtime_error("unknown session");
      EvictIfFull();
      found = sessions_.emplace(id, std::move(restored)).first;
    }
    found->second->last_touch = ++touch_counter_;
    return *found->second;
  }

  void EvictIfFull() {
    while (sessions_.size() >= static_cast<std::size_t>(config_.max_sessions)) {
      // Least-recently-touched goes first; its journal stays on disk, so a
      // returning player can still restore it.
      const auto oldest = std::min_element(
          sessions_.begin(), sessions_.end(), [](const auto& a, const auto& b) {
            return a.second->last_touch < b.second->last_touch;
          });
      sessions_.erase(oldest);
    }
  }

  std::filesystem::path SessionPath(const std::string& id) const {
    return config_.session_dir / (id + ".json");
  }

  void SaveSession(const WebSession& session) {
    if (config_.session_dir.empty()) return;
    JsonValue::Array actions;
    actions.reserve(session.history.size());
    for (const int action : session.history) actions.emplace_back(action);
    JsonValue::Array log;
    for (const LogEntry& entry : session.log) {
      log.emplace_back(JsonValue::Object{
          {"player", entry.player},
          {"text", entry.text},
          {"engine", entry.engine_text},
          {"sequence", static_cast<double>(entry.sequence)},
      });
    }
    const JsonValue journal = JsonValue::Object{
        {"id", session.id},
        {"seed", static_cast<double>(session.seed)},
        {"humanPlayer", session.human_player},
        {"charactersPerSide", session.characters_per_side},
        {"actions", std::move(actions)},
        {"log", std::move(log)},
    };
    const std::filesystem::path path = SessionPath(session.id);
    const std::filesystem::path temp = path.string() + ".tmp";
    try {
      {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("failed to open " + temp.string());
        output << journal.Dump();
        if (!output) throw std::runtime_error("failed to write " + temp.string());
      }
      std::filesystem::rename(temp, path);
      PruneSessionFiles();
    } catch (const std::exception& error) {
      // Persistence is best-effort; the in-memory session stays playable.
      std::cerr << "session save failed: " << error.what() << '\n';
      std::error_code ignored;
      std::filesystem::remove(temp, ignored);
    }
  }

  void PruneSessionFiles() {
    constexpr std::size_t kMaxSessionFiles = 512;
    std::error_code error;
    std::vector<std::pair<std::filesystem::file_time_type,
                          std::filesystem::path>> files;
    for (const auto& entry :
         std::filesystem::directory_iterator(config_.session_dir, error)) {
      if (entry.is_regular_file() && entry.path().extension() == ".json") {
        files.emplace_back(entry.last_write_time(error), entry.path());
      }
    }
    if (files.size() <= kMaxSessionFiles) return;
    std::sort(files.begin(), files.end());
    for (std::size_t index = 0; index < files.size() - kMaxSessionFiles;
         ++index) {
      std::filesystem::remove(files[index].second, error);
    }
  }

  // Rebuild a session from its journal by replaying the recorded actions
  // against a fresh engine state. Returns nullptr when there is no journal or
  // the journal no longer replays cleanly (e.g. the engine rules changed).
  std::unique_ptr<WebSession> LoadSession(const std::string& id) {
    if (config_.session_dir.empty() || !ValidSessionId(id)) return nullptr;
    const std::filesystem::path path = SessionPath(id);
    std::error_code exists_error;
    if (!std::filesystem::is_regular_file(path, exists_error)) return nullptr;
    try {
      const auto parsed = ParseJson(ReadFile(path));
      if (!parsed.has_value()) throw std::runtime_error("journal is not JSON");
      const auto& journal = JsonObject(*parsed, "session journal");
      const auto* seed = std::get_if<double>(&journal.at("seed").storage());
      const std::optional<int> human = OptionalInt(journal, "humanPlayer");
      const std::optional<int> characters =
          OptionalInt(journal, "charactersPerSide");
      if (seed == nullptr || !human.has_value() || !characters.has_value()) {
        throw std::runtime_error("journal is missing fields");
      }
      auto session = std::make_unique<WebSession>(
          id, static_cast<std::uint64_t>(*seed), *human, *characters);
      // Perturb the bot RNG so a restored bot does not replay the exact
      // random stream it already consumed before the restart.
      session->bot_rng = FastRng(session->seed ^ 0x9e3779b97f4a7c15ULL ^
                                 WebEntropy());
      for (const JsonValue& value :
           JsonArray(journal.at("actions"), "journal actions")) {
        const auto* action = std::get_if<double>(&value.storage());
        if (action == nullptr) throw std::runtime_error("bad journal action");
        const int action_id = static_cast<int>(*action);
        const std::vector<int> legal = session->state.LegalActions();
        if (std::find(legal.begin(), legal.end(), action_id) == legal.end()) {
          throw std::runtime_error("journal action is no longer legal");
        }
        session->state.ApplyAction(action_id);
        session->history.push_back(action_id);
      }
      const auto log_found = journal.find("log");
      if (log_found != journal.end()) {
        for (const JsonValue& value : JsonArray(log_found->second, "journal log")) {
          const auto& entry = JsonObject(value, "journal log entry");
          LogEntry restored;
          restored.player = OptionalInt(entry, "player").value_or(-1);
          if (const auto* text =
                  std::get_if<std::string>(&entry.at("text").storage())) {
            restored.text = *text;
          }
          if (const auto found = entry.find("engine"); found != entry.end()) {
            if (const auto* engine =
                    std::get_if<std::string>(&found->second.storage())) {
              restored.engine_text = *engine;
            }
          }
          restored.sequence = session->next_log_sequence;
          if (const auto found = entry.find("sequence"); found != entry.end()) {
            if (const auto* number = std::get_if<double>(&found->second.storage())) {
              restored.sequence = static_cast<std::uint64_t>(*number);
            }
          }
          session->next_log_sequence =
              std::max(session->next_log_sequence, restored.sequence + 1);
          session->log.push_back(std::move(restored));
        }
      }
      return session;
    } catch (const std::exception& error) {
      std::cerr << "session restore failed for " << id << ": " << error.what()
                << '\n';
      std::error_code ignored;
      std::filesystem::remove(path, ignored);
      return nullptr;
    }
  }

  static void AppendLog(WebSession& session, int player, std::string text,
                        std::string engine_text) {
    session.log.push_back({player, std::move(text), std::move(engine_text),
                           session.next_log_sequence++});
  }

  static void TrimLog(WebSession& session) {
    if (session.log.size() > kMaxLogEntries) {
      session.log.erase(session.log.begin(),
                        session.log.end() -
                            static_cast<std::ptrdiff_t>(kMaxLogEntries));
    }
  }

  static void ApplyLoggedAction(WebSession& session, int player, int action_id,
                                bool hidden_setup_choice) {
    const JsonValue action = ActionJson(session.state, action_id);
    const auto& object = JsonObject(action, "logged action");
    const auto* label = std::get_if<std::string>(&object.at("label").storage());
    const std::string action_text =
        label == nullptr ? ActionToString(action_id) : *label;
    const int character_id = session.state.ActingCharacterId(action_id);
    const std::string text =
        hidden_setup_choice
            ? "Chooses an active character face down"
            : character_id >= 0
                  ? std::string(GetCharacter(character_id).display_name) +
                        ": " + action_text
                  : action_text;
    const std::vector<CharacterSnapshot> before =
        CharacterSnapshots(session.state);
    const auto& reveals = session.state.RevealEvents();
    const int last_reveal = reveals.empty() ? 0 : reveals.back().sequence;

    AppendLog(session, player, text, ActionToString(action_id));
    session.state.ApplyAction(action_id);
    session.history.push_back(action_id);

    // Reveals the engine resolved during this action, in the order it did.
    for (const auto& event : session.state.RevealEvents()) {
      if (event.sequence <= last_reveal) continue;
      AppendLog(session, RevealOwner(event), RevealText(event),
                "reveal(" + RevealReasonName(event.reason) + "," +
                    std::string(GetCard(GetCardInstance(event.card_instance).name).key) +
                    "," + (event.triggered ? "triggered" : "missed") + ")");
    }

    const std::vector<CharacterSnapshot> after =
        CharacterSnapshots(session.state);
    for (const CharacterSnapshot& fallen : before) {
      if (fallen.life <= 0) continue;
      const auto current = std::find_if(
          after.begin(), after.end(), [&](const CharacterSnapshot& candidate) {
            return candidate.character_id == fallen.character_id;
          });
      if (current != after.end() && current->life > 0) continue;
      const auto& character = GetCharacter(fallen.character_id);
      AppendLog(session, fallen.player,
                std::string(character.display_name) + " died.",
                "death(" + std::string(character.key) + ")");
    }
  }

  int ChooseBotPolicyAction(WebSession& session) {
    if (static_policy_.has_value()) {
      const std::vector<int> legal = session.state.LegalActions();
      if (legal.empty()) {
        throw std::runtime_error(
            "static policy cannot act without legal actions");
      }
      if (legal.size() == 1) return legal.front();
      std::vector<ActionFeature> features;
      features.reserve(legal.size());
      for (const int action : legal) {
        features.push_back(EncodeAction(action));
      }
      const std::vector<float> logits = StaticPolicyLogits(
          *static_policy_,
          session.state.TrainingObservationTensor(
              session.state.CurrentPlayer()),
          features);
      int selected = 0;
      if (!config_.stochastic_policy) {
        for (int index = 1; index < static_cast<int>(logits.size()); ++index) {
          if (logits[static_cast<std::size_t>(index)] >
              logits[static_cast<std::size_t>(selected)]) {
            selected = index;
          }
        }
      } else {
        const float maximum =
            *std::max_element(logits.begin(), logits.end());
        std::vector<double> probabilities(logits.size());
        double total = 0.0;
        for (std::size_t index = 0; index < logits.size(); ++index) {
          probabilities[index] = std::exp(logits[index] - maximum);
          total += probabilities[index];
        }
        double choice =
            static_cast<double>(session.bot_rng.NextU64() >> 11U) /
            static_cast<double>(1ULL << 53U) * total;
        selected = static_cast<int>(legal.size()) - 1;
        for (std::size_t index = 0; index < probabilities.size(); ++index) {
          choice -= probabilities[index];
          if (choice <= 0.0) {
            selected = static_cast<int>(index);
            break;
          }
        }
      }
      return legal[static_cast<std::size_t>(selected)];
    }
#ifdef BANG_ENABLE_TORCH
    if (trained_policy_) {
      return SelectPolicyAction(
          trained_policy_,
          session.state,
          session.bot_rng,
          !config_.stochastic_policy);
    }
#endif
    return ChooseHeuristicAction(session.state, session.bot_rng);
  }

  // `frames`, when given, receives the player-scoped state after each move.
  void AdvanceAutomaticActions(WebSession& session,
                               JsonValue::Array* frames = nullptr) {
    int steps = 0;
    while (!session.state.IsTerminal()) {
      const std::optional<int> forced_human_action =
          ForcedHitResponseAction(session.state, session.human_player);
      if (session.state.CurrentPlayer() == session.human_player &&
          !forced_human_action.has_value()) {
        break;
      }
      if (++steps > kMaxBotSteps) {
        throw std::runtime_error(
            "automatic game advance exceeded its safety limit");
      }
      const int player = session.state.CurrentPlayer();
      const int action_id = forced_human_action.has_value()
          ? *forced_human_action
          : ChooseBotPolicyAction(session);
      const bool hidden_setup_choice =
          session.state.CurrentPhase() == Phase::kChooseActive &&
          !session.state.SetupComplete();
      ApplyLoggedAction(session, player, action_id, hidden_setup_choice);
      if (frames != nullptr) frames->push_back(Serialize(session));
    }
    TrimLog(session);
  }

  JsonValue PlayerJson(const WebSession& session, int player) const {
    const PlayerState& state =
        session.state.Players()[static_cast<std::size_t>(player)];
    const bool reveal_characters =
        session.state.SetupComplete() || player == session.human_player;
    JsonValue::Array positions;
    positions.reserve(2);
    for (int position = 0; position < 2; ++position) {
      const CharacterState& character =
          state.positions[static_cast<std::size_t>(position)];
      JsonValue::Object item{
          {"position", position},
          {"role", position == state.active_position ? "active" : "rearguard"},
          {"equipment", reveal_characters ? CardsJson(character.equipment)
                                           : JsonValue(JsonValue::Array{})},
      };
      if (reveal_characters && character.character_id >= 0) {
        item["character"] = CharacterJson(character.character_id);
        item["life"] = character.life;
      } else {
        item["character"] = nullptr;
        item["life"] = nullptr;
      }
      positions.emplace_back(std::move(item));
    }

    JsonValue::Array reserve;
    if (player == session.human_player) {
      std::vector<int> known_reserve = state.reserve;
      std::sort(known_reserve.begin(), known_reserve.end());
      for (const int character : known_reserve) {
        reserve.emplace_back(CharacterJson(character));
      }
    }
    const std::vector<int> known =
        session.state.KnownHandCards(session.human_player, player);
    return JsonValue::Object{
        {"player", player},
        {"side", std::string(SideName(state.side))},
        {"isHuman", player == session.human_player},
        {"activePosition", reveal_characters ? JsonValue(state.active_position)
                                              : JsonValue(nullptr)},
        {"positions", std::move(positions)},
        {"handCount", static_cast<int>(state.hand.size())},
        {"hand", player == session.human_player ? CardsJson(state.hand)
                                                  : JsonValue(JsonValue::Array{})},
        {"knownHand", CardsJson(known)},
        {"deckCount", static_cast<int>(state.deck.size())},
        {"reserveCount", static_cast<int>(state.reserve.size())},
        {"reserve", std::move(reserve)},
        {"deckExhausted", state.personal_deck_exhausted},
    };
  }

  JsonValue Serialize(const WebSession& session) const {
    const AbilityPrompt ability = AbilityPromptFor(session.state);
    JsonValue::Array players;
    for (int player = 0; player < kSideCount; ++player) {
      players.emplace_back(PlayerJson(session, player));
    }

    JsonValue::Array legal_actions;
    if (!session.state.IsTerminal() &&
        session.state.CurrentPlayer() == session.human_player) {
      for (const int action : session.state.LegalActions()) {
        legal_actions.emplace_back(ActionJson(session.state, action));
      }
    }

    JsonValue::Array log;
    for (const LogEntry& entry : session.log) {
      log.emplace_back(JsonValue::Object{
          {"player", entry.player < 0 ? JsonValue(nullptr) : JsonValue(entry.player)},
          {"side", entry.player < 0
                       ? JsonValue("system")
                       : JsonValue(std::string(
                             SideName(static_cast<Side>(entry.player))))},
          {"text", entry.text},
          {"engineText", entry.engine_text},
          {"sequence", static_cast<double>(entry.sequence)},
      });
    }

    JsonValue::Array choice_cards;
    if (session.state.ChoiceCardsVisibleTo(session.human_player)) {
      for (const int card : session.state.ChoiceCards()) {
        choice_cards.emplace_back(CardJson(card));
      }
    }
    JsonValue::Array revealed_cards;
    for (const int card : session.state.RevealedCards()) {
      revealed_cards.emplace_back(CardJson(card));
    }
    JsonValue::Array reveal_events;
    for (const auto& event : session.state.RevealEvents()) {
      reveal_events.emplace_back(JsonValue::Object{
          {"sequence", event.sequence},
          {"reason", RevealReasonName(event.reason)},
          {"player", event.player},
          {"card", CardJson(event.card_instance)},
          {"triggered", event.triggered},
      });
    }

    JsonValue::Object result{
        {"sessionId", session.id},
        {"seed", static_cast<double>(session.seed)},
        {"humanPlayer", session.human_player},
        {"charactersPerSide", session.characters_per_side},
        {"currentPlayer", session.state.IsTerminal()
                              ? JsonValue(nullptr)
                              : JsonValue(session.state.CurrentPlayer())},
        {"turnPlayer", session.state.TurnPlayer()},
        {"turnNumber", session.state.TurnNumber()},
        {"phase", PhaseName(session.state.CurrentPhase())},
        {"phaseLabel", ability.label.empty()
                           ? PhaseLabel(session.state.CurrentPhase())
                           : ability.label},
        {"phaseHint", ability.hint.empty()
                          ? PhaseHint(session.state.CurrentPhase())
                          : ability.hint},
        {"phaseCharacter",
         session.state.PendingAbilityCharacter() >= 0
             ? CharacterJson(session.state.PendingAbilityCharacter())
             : JsonValue(nullptr)},
        {"setupComplete", session.state.SetupComplete()},
        {"terminal", session.state.IsTerminal()},
        {"winner", session.state.IsTerminal()
                       ? JsonValue(session.state.Winner())
                       : JsonValue(nullptr)},
        {"players", std::move(players)},
        {"discard", CardsJson(session.state.DiscardPile())},
        {"discardCount", static_cast<int>(session.state.DiscardPile().size())},
        {"commonDeckCount", static_cast<int>(session.state.CommonDeck().size())},
        {"commonDeckCreated", session.state.CommonDeckCreated()},
        {"choiceCards", std::move(choice_cards)},
        {"revealedCards", std::move(revealed_cards)},
        {"revealEvents", std::move(reveal_events)},
        {"legalActions", std::move(legal_actions)},
        {"log", std::move(log)},
        {"ai", JsonValue::Object{
                   {"id", trained_ ? "trained_checkpoint"
                                   : "frontier_heuristic"},
                   {"name", trained_ ? "League PPO checkpoint"
                                     : "Frontier heuristic"},
                   {"trained", trained_},
                   {"iteration", trained_ ? JsonValue(policy_iteration_)
                                          : JsonValue(nullptr)},
                   {"checkpoint", trained_ ? JsonValue(policy_path_.string())
                                           : JsonValue(nullptr)},
                   {"error", policy_error_.empty()
                                 ? JsonValue(nullptr)
                                 : JsonValue(policy_error_)},
               }},
    };
    return result;
  }

  WebConfig config_;
  FastRng session_rng_;
  std::map<std::string, std::unique_ptr<WebSession>> sessions_;
  std::uint64_t touch_counter_ = 0;
  bool trained_ = false;
  int policy_iteration_ = 0;
  std::filesystem::path policy_path_;
  std::string policy_error_;
  std::optional<StaticPolicyNetwork> static_policy_;
#ifdef BANG_ENABLE_TORCH
  BangActorCritic trained_policy_{nullptr};
#endif
};

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("failed to open " + path.string());
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}

#ifndef __EMSCRIPTEN__
struct HttpRequest {
  std::string method;
  std::string path;
  std::string body;
};

std::optional<std::size_t> ContentLength(const std::string& headers) {
  std::string lower = headers;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  const std::string key = "content-length:";
  const std::size_t position = lower.find(key);
  if (position == std::string::npos) return std::nullopt;
  std::size_t start = position + key.size();
  while (start < lower.size() &&
         std::isspace(static_cast<unsigned char>(lower[start])) != 0) {
    ++start;
  }
  std::size_t end = start;
  while (end < lower.size() &&
         std::isdigit(static_cast<unsigned char>(lower[end])) != 0) {
    ++end;
  }
  if (start == end) throw std::invalid_argument("invalid Content-Length");
  return static_cast<std::size_t>(std::stoull(lower.substr(start, end - start)));
}

HttpRequest ReadRequest(SocketHandle client) {
  std::string buffer;
  std::array<char, 4096> chunk{};
  std::size_t header_end = std::string::npos;
  while ((header_end = buffer.find("\r\n\r\n")) == std::string::npos) {
    const long long count = ReceiveSome(client, chunk.data(), chunk.size());
    if (count <= 0) throw std::runtime_error("connection closed during headers");
    buffer.append(chunk.data(), static_cast<std::size_t>(count));
    if (buffer.size() > kMaxRequestBytes) {
      throw std::invalid_argument("request headers are too large");
    }
  }
  const std::string headers = buffer.substr(0, header_end + 4);
  const std::size_t body_start = header_end + 4;
  const std::size_t body_size = ContentLength(headers).value_or(0);
  if (body_size > kMaxRequestBytes) {
    throw std::invalid_argument("request body is too large");
  }
  while (buffer.size() < body_start + body_size) {
    const long long count = ReceiveSome(client, chunk.data(), chunk.size());
    if (count <= 0) throw std::runtime_error("connection closed during body");
    buffer.append(chunk.data(), static_cast<std::size_t>(count));
  }
  std::istringstream first_line(headers.substr(0, headers.find("\r\n")));
  HttpRequest request;
  first_line >> request.method >> request.path;
  const std::size_t query = request.path.find('?');
  if (query != std::string::npos) request.path.erase(query);
  request.body = buffer.substr(body_start, body_size);
  return request;
}

void SendAll(SocketHandle client, std::string_view data) {
  while (!data.empty()) {
    const long long count = SendSome(client, data.data(), data.size());
    if (count <= 0) {
      throw std::runtime_error("send failed: " + SocketErrorText());
    }
    data.remove_prefix(static_cast<std::size_t>(count));
  }
}

std::string StatusText(int status) {
  if (status == 200) return "OK";
  if (status == 400) return "Bad Request";
  if (status == 404) return "Not Found";
  if (status == 405) return "Method Not Allowed";
  return "Internal Server Error";
}

void SendResponse(SocketHandle client, int status, std::string_view content_type,
                  const std::string& body) {
  std::ostringstream headers;
  headers << "HTTP/1.1 " << status << ' ' << StatusText(status) << "\r\n"
          << "Content-Type: " << content_type << "\r\n"
          << "Content-Length: " << body.size() << "\r\n"
          << "Cache-Control: no-cache\r\n"
          << "X-Content-Type-Options: nosniff\r\n"
          << "Connection: close\r\n\r\n";
  SendAll(client, headers.str());
  SendAll(client, body);
}

void SendJson(SocketHandle client, int status, const JsonValue& value) {
  SendResponse(client, status, "application/json; charset=utf-8", value.Dump());
}

JsonValue::Object RequestBody(const std::string& body) {
  if (body.empty()) return {};
  const auto parsed = ParseJson(body);
  if (!parsed.has_value()) throw std::invalid_argument("body is not valid JSON");
  return JsonObject(*parsed, "request body");
}

std::string SessionId(const std::string& path, std::string_view suffix = {}) {
  constexpr std::string_view prefix = "/api/session/";
  if (!StartsWith(path, prefix)) throw std::invalid_argument("invalid session path");
  std::string result = path.substr(prefix.size());
  if (!suffix.empty()) {
    if (!EndsWith(result, suffix)) throw std::invalid_argument("invalid session path");
    result.erase(result.size() - suffix.size());
    if (!result.empty() && result.back() == '/') result.pop_back();
  }
  if (result.empty() || result.find('/') != std::string::npos) {
    throw std::invalid_argument("invalid session id");
  }
  return result;
}

std::optional<std::filesystem::path> StaticPath(const WebConfig& config,
                                                const std::string& request_path) {
  if (request_path.empty() || request_path.front() != '/') return std::nullopt;
  const std::string relative = request_path == "/" ? "index.html" :
                                                      request_path.substr(1);
  const std::filesystem::path normalized =
      std::filesystem::path(relative).lexically_normal();
  for (const auto& component : normalized) {
    if (component == "..") return std::nullopt;
  }
  const std::filesystem::path result = config.static_dir / normalized;
  if (!std::filesystem::is_regular_file(result)) return std::nullopt;
  return result;
}

std::string ContentType(const std::filesystem::path& path) {
  const std::string extension = path.extension().string();
  if (extension == ".html") return "text/html; charset=utf-8";
  if (extension == ".css") return "text/css; charset=utf-8";
  if (extension == ".js") return "text/javascript; charset=utf-8";
  if (extension == ".json") return "application/json; charset=utf-8";
  if (extension == ".svg") return "image/svg+xml";
  if (extension == ".png") return "image/png";
  return "application/octet-stream";
}

void HandleClient(SocketHandle client, BangWebApp& app, const WebConfig& config) {
  try {
    const HttpRequest request = ReadRequest(client);
    if (request.method == "GET" && request.path == "/api/health") {
      SendJson(client, 200, app.HealthJson());
      return;
    }
    if (request.method == "GET" && StartsWith(request.path, "/api/session/")) {
      SendJson(client, 200, app.SessionJson(SessionId(request.path)));
      return;
    }
    if (request.method == "POST" && request.path == "/api/new") {
      const JsonValue::Object body = RequestBody(request.body);
      SendJson(client, 200,
               app.NewSession(OptionalInt(body, "seed"),
                              OptionalInt(body, "humanPlayer"),
                              OptionalInt(body, "charactersPerSide")));
      return;
    }
    if (request.method == "POST" && EndsWith(request.path, "/action")) {
      const JsonValue::Object body = RequestBody(request.body);
      const std::optional<int> action = OptionalInt(body, "action");
      if (!action.has_value()) throw std::invalid_argument("action is required");
      SendJson(client, 200,
               app.ApplyHumanAction(SessionId(request.path, "/action"), *action));
      return;
    }
    if (request.method == "GET") {
      const auto path = StaticPath(config, request.path);
      if (path.has_value()) {
        SendResponse(client, 200, ContentType(*path), ReadFile(*path));
      } else {
        SendJson(client, 404, JsonValue::Object{{"error", "not found"}});
      }
      return;
    }
    SendJson(client, 405, JsonValue::Object{{"error", "method not allowed"}});
  } catch (const std::invalid_argument& error) {
    SendJson(client, 400, JsonValue::Object{{"error", error.what()}});
  } catch (const std::exception& error) {
    SendJson(client, 500, JsonValue::Object{{"error", error.what()}});
  }
}

SocketHandle BindServer(const WebConfig& config) {
  const SocketHandle server = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server == kInvalidSocket) throw std::runtime_error("socket creation failed");
#ifndef _WIN32
  // On Windows SO_REUSEADDR lets a second process steal a bound port, and a
  // just-closed listener is rebindable by default, so it is POSIX-only.
  const int reuse = 1;
  ::setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<std::uint16_t>(config.port));
  if (::inet_pton(AF_INET, config.host.c_str(), &address.sin_addr) != 1) {
    CloseSocket(server);
    throw std::invalid_argument("host must be an IPv4 address");
  }
  if (::bind(server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    const std::string message = SocketErrorText();
    CloseSocket(server);
    throw std::runtime_error("bind failed: " + message);
  }
  if (::listen(server, 16) != 0) {
    const std::string message = SocketErrorText();
    CloseSocket(server);
    throw std::runtime_error("listen failed: " + message);
  }
  return server;
}
#endif

}  // namespace

JsonValue RunWebSmokeTest(const WebConfig& config) {
  WebConfig smoke_config = config;
  smoke_config.session_dir.clear();  // The smoke test never touches disk.
  BangWebApp app(smoke_config);
  JsonValue initial = app.NewSession(4242, 1, 4);
  const auto& initial_object = JsonObject(initial, "initial web state");
  const auto& players = JsonArray(initial_object.at("players"), "players");
  const auto& opponent = JsonObject(players[0], "opponent");
  if (!JsonArray(opponent.at("hand"), "opponent hand").empty()) {
    throw std::runtime_error("web view leaked the opponent hand");
  }
  const auto& opponent_positions =
      JsonArray(opponent.at("positions"), "opponent positions");
  const auto& hidden_opponent =
      JsonObject(opponent_positions.front(), "hidden opponent");
  if (!std::holds_alternative<std::nullptr_t>(
          hidden_opponent.at("character").storage())) {
    throw std::runtime_error(
        "web view leaked an opponent ability during setup");
  }
  const auto& human = JsonObject(players[1], "human");
  const auto& hand = JsonArray(human.at("hand"), "human hand");
  if (hand.empty() ||
      JsonObject(hand.front(), "visible hand card").find("rulesText") ==
          JsonObject(hand.front(), "visible hand card").end()) {
    throw std::runtime_error("visible card is missing rules text");
  }
  const auto& human_positions =
      JsonArray(human.at("positions"), "human positions");
  const auto& visible_character = JsonObject(
      JsonObject(human_positions.front(), "visible position").at("character"),
      "visible character");
  if (visible_character.find("abilityText") == visible_character.end()) {
    throw std::runtime_error("visible character is missing ability text");
  }
  const auto& log = JsonArray(initial_object.at("log"), "initial log");
  for (const JsonValue& entry : log) {
    const auto& object = JsonObject(entry, "initial log entry");
    const auto* text = std::get_if<std::string>(&object.at("text").storage());
    if (text != nullptr && StartsWith(*text, "Make ")) {
      throw std::runtime_error("web log leaked the opponent setup choice");
    }
  }
  const auto& actions = JsonArray(initial_object.at("legalActions"), "legal actions");
  if (actions.empty()) throw std::runtime_error("web setup exposed no human action");
  if (initial_object.find("revealEvents") == initial_object.end()) {
    throw std::runtime_error("web state is missing reveal events");
  }
  const auto& action = JsonObject(actions.front(), "first legal action");
  const auto* action_id = std::get_if<double>(&action.at("id").storage());
  if (action_id == nullptr) throw std::runtime_error("web action has no numeric id");
  const auto& action_character =
      JsonObject(action.at("character"), "setup action character");
  const auto* action_character_name =
      std::get_if<std::string>(&action_character.at("name").storage());
  if (action_character_name == nullptr) {
    throw std::runtime_error("web setup action has no character name");
  }
  const auto* session_id =
      std::get_if<std::string>(&initial_object.at("sessionId").storage());
  if (session_id == nullptr) throw std::runtime_error("web state has no session id");
  JsonValue advanced = app.ApplyHumanAction(*session_id, static_cast<int>(*action_id));
  const auto& advanced_object = JsonObject(advanced, "advanced web state");
  if (advanced_object.find("phase") == advanced_object.end() ||
      advanced_object.find("log") == advanced_object.end()) {
    throw std::runtime_error("advanced web state is incomplete");
  }
  const auto& advanced_log =
      JsonArray(advanced_object.at("log"), "advanced web log");
  const bool attributed_action =
      std::any_of(advanced_log.begin(), advanced_log.end(),
                  [&](const JsonValue& value) {
                    const auto& entry = JsonObject(value, "web log entry");
                    const auto* player =
                        std::get_if<double>(&entry.at("player").storage());
                    const auto* text =
                        std::get_if<std::string>(&entry.at("text").storage());
                    return player != nullptr &&
                           static_cast<int>(*player) == 1 && text != nullptr &&
                           StartsWith(*text, *action_character_name + ": ");
                  });
  if (!attributed_action) {
    throw std::runtime_error("web log action is missing character attribution");
  }

  // The opponent's turn comes back as one player-view frame per move, and
  // log sequence numbers keep counting so the browser can diff past the
  // trimmed log. Play on, ending turns where possible, until a reply has one.
  const auto max_sequence = [](const JsonValue::Object& state) {
    double best = -1.0;
    double previous = -1.0;
    for (const JsonValue& value : JsonArray(state.at("log"), "sequenced log")) {
      const auto* sequence = std::get_if<double>(
          &JsonObject(value, "sequenced entry").at("sequence").storage());
      if (sequence == nullptr || *sequence <= previous) {
        throw std::runtime_error("web log sequences must strictly increase");
      }
      previous = *sequence;
      best = std::max(best, *sequence);
    }
    return best;
  };
  JsonValue current = advanced;
  bool saw_replay = false;
  for (int step = 0; step < 200 && !saw_replay; ++step) {
    const auto& state = JsonObject(current, "stepped web state");
    if (const auto found = state.find("replay"); found != state.end()) {
      const auto& frames = JsonArray(found->second, "replay frames");
      if (frames.size() < 2) throw std::runtime_error("replay must hold several moves");
      for (const JsonValue& frame : frames) {
        const auto& view = JsonObject(frame, "replay frame");
        if (view.find("players") == view.end() || view.find("replay") != view.end()) {
          throw std::runtime_error("replay frame is not a plain player view");
        }
        max_sequence(view);
      }
      if (max_sequence(JsonObject(frames.back(), "last frame")) != max_sequence(state)) {
        throw std::runtime_error("replay does not end where the returned state does");
      }
      saw_replay = true;
      break;
    }
    const auto* terminal = std::get_if<bool>(&state.at("terminal").storage());
    if (terminal != nullptr && *terminal) break;
    const auto& legal = JsonArray(state.at("legalActions"), "stepped legal actions");
    if (legal.empty()) break;
    const auto end_turn = std::find_if(legal.begin(), legal.end(), [](const JsonValue& value) {
      const auto* kind = std::get_if<std::string>(
          &JsonObject(value, "stepped action").at("kind").storage());
      return kind != nullptr && *kind == "end_turn";
    });
    const auto& chosen = JsonObject(end_turn != legal.end() ? *end_turn : legal.front(),
                                    "chosen action");
    current = app.ApplyHumanAction(
        *session_id,
        static_cast<int>(std::get<double>(chosen.at("id").storage())));
  }
  if (!saw_replay) throw std::runtime_error("the opponent's turn produced no replay");

  return JsonValue::Object{
      {"ok", true},
      {"sessionId", *session_id},
      {"phase", advanced_object.at("phase")},
      {"ai", advanced_object.at("ai")},
  };
}

JsonValue RunWebForcedResponseSmokeTest(const WebConfig& config) {
  WebConfig smoke_config = config;
  smoke_config.session_dir.clear();
  BangWebApp app(smoke_config);

  const auto response_state = [&](CardName source, bool has_defense) {
    GameConfig game_config{
        .seed = 77,
        .characters_per_side = 2,
        .shuffle_decks = false,
        .shuffle_characters = false,
    };
    game_config.character_rosters[0] = {3, 9};
    game_config.character_rosters[1] = {12, 16};
    game_config.starting_decks[0] = DeckWithTop(
        Side::kLaw,
        {source, CardName::kBank, CardName::kWater, CardName::kWagon});
    game_config.starting_decks[1] = has_defense
        ? DeckWithTop(
              Side::kOutlaw,
              {CardName::kMissed, CardName::kWater, CardName::kWagon,
               CardName::kStealing, CardName::kBeer})
        : DeckWithTop(
              Side::kOutlaw,
              {CardName::kWater, CardName::kWagon, CardName::kStealing,
               CardName::kBeer, CardName::kGeneralStore});
    GameState state(game_config);
    state.ApplyAction(kChooseActiveLeftAction);
    state.ApplyAction(kChooseActiveLeftAction);
    const auto& hand = state.Players()[0].hand;
    const auto found = std::find_if(
        hand.begin(), hand.end(), [source](int instance) {
          return GetCardInstance(instance).name == source;
        });
    if (found == hand.end()) {
      throw std::runtime_error(
          "forced-response smoke attacker did not draw its source card");
    }
    const Target target = source == CardName::kIndians
        ? Target::kOwnActive
        : Target::kOpponentActive;
    state.ApplyAction(PlayCardAction(*found, target));
    return state;
  };

  JsonValue::Array verified;
  for (const auto& [source, expected_phase, expected_action] :
       std::array{
           std::tuple{CardName::kColt, Phase::kRespondToHit, kTakeHitAction},
           std::tuple{CardName::kIndians, Phase::kRespondToHit, kTakeHitAction},
           std::tuple{
               CardName::kDuel, Phase::kDuelResponse, kSkipOptionalAction},
       }) {
    const JsonValue initial = app.NewSession(
        9000 + static_cast<int>(source), 1, 2);
    const auto& initial_object = JsonObject(initial, "forced-response session");
    const std::string session_id =
        std::get<std::string>(initial_object.at("sessionId").storage());
    WebSession& session = app.SessionForTest(session_id);
    session.state = response_state(source, false);
    session.history.clear();
    session.log.clear();
    if (session.state.CurrentPlayer() != session.human_player ||
        session.state.CurrentPhase() != expected_phase ||
        session.state.LegalActions() != std::vector<int>{expected_action}) {
      throw std::runtime_error(
          "forced-response smoke setup did not create a forced human hit");
    }
    const int responding_character =
        session.state.ActingCharacterId(expected_action);
    if (responding_character < 0) {
      throw std::runtime_error(
          "forced-response smoke could not identify the acting character");
    }

    app.SessionJson(session_id);
    if (session.history.empty() || session.history.front() != expected_action) {
      throw std::runtime_error(
          "web app did not automatically resolve the forced human hit");
    }
    if (ForcedHitResponseAction(
            session.state, session.human_player).has_value()) {
      throw std::runtime_error(
          "web app stopped on another forced human hit response");
    }
    const std::string actor_prefix =
        std::string(GetCharacter(responding_character).display_name) + ": ";
    const bool attributed =
        std::any_of(session.log.begin(), session.log.end(),
                    [&](const LogEntry& entry) {
                      return entry.player == session.human_player &&
                             StartsWith(entry.text, actor_prefix);
                    });
    if (!attributed) {
      throw std::runtime_error(
          "forced-response log is missing character attribution");
    }
    verified.emplace_back(std::string(GetCard(source).display_name));
  }

  const JsonValue optional_initial = app.NewSession(9999, 1, 2);
  const auto& optional_object =
      JsonObject(optional_initial, "optional-response session");
  const std::string optional_session_id =
      std::get<std::string>(optional_object.at("sessionId").storage());
  WebSession& optional_session = app.SessionForTest(optional_session_id);
  optional_session.state = response_state(CardName::kColt, true);
  optional_session.history.clear();
  optional_session.log.clear();
  if (optional_session.state.LegalActions().size() <= 1) {
    throw std::runtime_error(
        "optional-response smoke setup did not offer a defense");
  }
  app.SessionJson(optional_session_id);
  if (!optional_session.history.empty() ||
      optional_session.state.CurrentPhase() != Phase::kRespondToHit) {
    throw std::runtime_error(
        "web app automatically resolved a hit that the human could avoid");
  }

  GameConfig death_config{
      .seed = 8080,
      .characters_per_side = 2,
      .shuffle_decks = false,
      .shuffle_characters = false,
  };
  death_config.character_rosters[0] = {
      static_cast<int>(CharacterName::kTheStranger),
      static_cast<int>(CharacterName::kTexKiller)};
  death_config.character_rosters[1] = {
      static_cast<int>(CharacterName::kDaltonBros),
      static_cast<int>(CharacterName::kJackRansome)};
  death_config.starting_decks[0] = DeckWithTop(
      Side::kLaw,
      {CardName::kColt, CardName::kDerringer, CardName::kDerringer,
       CardName::kBank});
  death_config.starting_decks[1] = DeckWithTop(
      Side::kOutlaw,
      {CardName::kWater, CardName::kWagon, CardName::kStealing,
       CardName::kBeer});
  GameState death_state(death_config);
  death_state.ApplyAction(kChooseActiveLeftAction);
  death_state.ApplyAction(kChooseActiveLeftAction);
  const auto play_from_hand = [&](CardName name) {
    const auto& hand = death_state.Players()[0].hand;
    const auto found = std::find_if(hand.begin(), hand.end(), [name](int card) {
      return GetCardInstance(card).name == name;
    });
    if (found == hand.end()) {
      throw std::runtime_error("death-log smoke is missing an attack card");
    }
    death_state.ApplyAction(
        PlayCardAction(*found, Target::kOpponentActive));
  };
  play_from_hand(CardName::kColt);
  death_state.ApplyAction(kTakeHitAction);
  play_from_hand(CardName::kDerringer);
  death_state.ApplyAction(kTakeHitAction);
  play_from_hand(CardName::kDerringer);

  const JsonValue death_initial = app.NewSession(10001, 1, 2);
  const auto& death_initial_object =
      JsonObject(death_initial, "death-log session");
  const std::string death_session_id =
      std::get<std::string>(death_initial_object.at("sessionId").storage());
  WebSession& death_session = app.SessionForTest(death_session_id);
  death_session.state = std::move(death_state);
  death_session.history.clear();
  death_session.log.clear();
  const JsonValue after_death =
      app.ApplyHumanAction(death_session_id, kTakeHitAction);
  const auto& death_log =
      JsonArray(JsonObject(after_death, "post-death state").at("log"),
                "post-death log");
  const std::string dead_name(GetCharacter(
      static_cast<int>(CharacterName::kDaltonBros)).display_name);
  bool saw_final_action = false;
  bool saw_death = false;
  for (const JsonValue& value : death_log) {
    const auto& entry = JsonObject(value, "post-death log entry");
    const auto* text = std::get_if<std::string>(&entry.at("text").storage());
    if (text == nullptr) continue;
    saw_final_action |= *text == dead_name + ": Take the hit";
    saw_death |= *text == dead_name + " died.";
  }
  if (!saw_final_action || !saw_death) {
    throw std::runtime_error(
        "web log did not record the acting character and their death");
  }

  return JsonValue::Object{
      {"ok", true},
      {"verified", std::move(verified)},
  };
}

// Phase::kChooseAbility used to render as a bare "Resolve a character ability"
// with unlabelled ✓/✗ buttons. The view must now name the character, explain
// the decision, and say what each option does.
JsonValue RunWebAbilityPromptSmokeTest(const WebConfig& config) {
  WebConfig smoke_config = config;
  smoke_config.session_dir.clear();
  BangWebApp app(smoke_config);

  const auto text_field = [](const JsonValue::Object& object,
                             const std::string& key,
                             std::string_view context) {
    const auto found = object.find(key);
    const auto* value = found == object.end()
                            ? nullptr
                            : std::get_if<std::string>(&found->second.storage());
    if (value == nullptr) {
      throw std::runtime_error(std::string(context) + " has no " + key);
    }
    return *value;
  };
  // Hands the crafted state to a session and returns the browser's view of it.
  const auto view_of = [&](int seed, int human, GameState state) {
    const JsonValue initial = app.NewSession(seed, human, 2);
    const std::string session_id = std::get<std::string>(
        JsonObject(initial, "ability session").at("sessionId").storage());
    WebSession& session = app.SessionForTest(session_id);
    session.state = std::move(state);
    session.history.clear();
    session.log.clear();
    if (session.state.CurrentPhase() != Phase::kChooseAbility ||
        session.state.CurrentPlayer() != human) {
      throw std::runtime_error(
          "ability-prompt smoke setup did not stop on a human ability choice");
    }
    return std::pair{session_id, app.SessionJson(session_id)};
  };
  // Plays out whatever is on the table until the wanted player is free to act.
  const auto advance_to_play = [](GameState& state, int player) {
    for (int step = 0; step < 200; ++step) {
      if (state.CurrentPhase() == Phase::kPlay &&
          state.CurrentPlayer() == player) {
        return;
      }
      const std::vector<int> legal = state.LegalActions();
      if (legal.empty()) break;
      const auto end = std::find(legal.begin(), legal.end(), kEndPlayAction);
      state.ApplyAction(end == legal.end() ? legal.front() : kEndPlayAction);
    }
    throw std::runtime_error("ability-prompt smoke could not reach a play phase");
  };
  const auto ability_state = [&](CharacterName law, CharacterName outlaw) {
    GameConfig game_config{
        .seed = 4242,
        .characters_per_side = 2,
        .shuffle_decks = false,
        .shuffle_characters = false,
    };
    const auto other = [](CharacterName wanted, CharacterName first,
                          CharacterName second) {
      return static_cast<int>(wanted == first ? second : first);
    };
    game_config.character_rosters[0] = {
        static_cast<int>(law),
        other(law, CharacterName::kTexKiller, CharacterName::kTheStranger)};
    game_config.character_rosters[1] = {
        static_cast<int>(outlaw),
        other(outlaw, CharacterName::kJackRansome,
              CharacterName::kTocoRamirez)};
    GameState state(game_config);
    state.ApplyAction(kChooseActiveLeftAction);
    state.ApplyAction(kChooseActiveLeftAction);
    return state;
  };

  JsonValue::Array verified;

  // Jango: an accept/skip prompt, triggered by the opponent's swap.
  {
    GameState state = ability_state(CharacterName::kJango,
                                    CharacterName::kDaltonBros);
    advance_to_play(state, 1);
    state.ApplyAction(kSwapAction);
    const auto [session_id, view] = view_of(11001, 0, std::move(state));
    const auto& object = JsonObject(view, "Jango view");
    const std::string label = text_field(object, "phaseLabel", "Jango view");
    const std::string hint = text_field(object, "phaseHint", "Jango view");
    if (label.find("Jango") == std::string::npos || hint.size() < 20) {
      throw std::runtime_error("the ability prompt does not explain Jango");
    }
    const auto& character =
        JsonObject(object.at("phaseCharacter"), "Jango prompt character");
    if (text_field(character, "name", "Jango prompt character") != "Jango") {
      throw std::runtime_error("the ability prompt names the wrong character");
    }
    bool saw_accept = false;
    bool saw_skip = false;
    for (const JsonValue& value :
         JsonArray(object.at("legalActions"), "Jango actions")) {
      const auto& action = JsonObject(value, "Jango action");
      const std::string action_label = text_field(action, "label", "Jango action");
      if (text_field(action, "detail", "Jango action").empty()) {
        throw std::runtime_error("an ability option carries no explanation");
      }
      if (action_label == "Use the effect" || action_label == "Skip the effect") {
        throw std::runtime_error("an ability option kept its generic label");
      }
      const auto* accept =
          std::get_if<bool>(&action.at("accept").storage());
      if (accept == nullptr) {
        throw std::runtime_error("an optional action has no accept flag");
      }
      saw_accept |= *accept;
      saw_skip |= !*accept;
    }
    if (!saw_accept || !saw_skip) {
      throw std::runtime_error("the ability prompt lost one of its options");
    }
    verified.emplace_back(JsonValue::Object{{"label", label}, {"hint", hint}});
  }

  // Soundance Kid: an activated ability that asks twice — the cost first, then
  // which of the two drawn cards to keep.
  {
    GameState state = ability_state(CharacterName::kTexKiller,
                                    CharacterName::kSoundanceKid);
    advance_to_play(state, 1);
    state.ApplyAction(UseAbilityAction(CharacterName::kSoundanceKid));
    const auto [session_id, view] = view_of(11002, 1, std::move(state));
    const auto& object = JsonObject(view, "Soundance Kid view");
    const std::string cost_label =
        text_field(object, "phaseLabel", "Soundance Kid view");
    if (cost_label.find("Soundance Kid") == std::string::npos) {
      throw std::runtime_error("the cost step does not name Soundance Kid");
    }
    const auto& cost_actions =
        JsonArray(object.at("legalActions"), "Soundance Kid cost actions");
    if (cost_actions.empty()) {
      throw std::runtime_error("the cost step offers no card to discard");
    }
    const auto& first_cost = JsonObject(cost_actions.front(), "cost action");
    if (text_field(first_cost, "detail", "cost action").empty()) {
      throw std::runtime_error("a card pick carries no explanation");
    }
    const JsonValue after_cost = app.ApplyHumanAction(
        session_id,
        static_cast<int>(std::get<double>(first_cost.at("id").storage())));
    const auto& keep_object = JsonObject(after_cost, "Soundance Kid keep view");
    const std::string keep_label =
        text_field(keep_object, "phaseLabel", "Soundance Kid keep view");
    if (keep_label == cost_label ||
        keep_label.find("Soundance Kid") == std::string::npos) {
      throw std::runtime_error(
          "the second ability step reuses the first step's prompt");
    }
    if (JsonArray(keep_object.at("choiceCards"), "Soundance Kid draws").empty()) {
      throw std::runtime_error("the keep step shows no drawn cards");
    }
    verified.emplace_back(JsonValue::Object{
        {"label", cost_label},
        {"hint", text_field(object, "phaseHint", "Soundance Kid view")}});
    verified.emplace_back(JsonValue::Object{
        {"label", keep_label},
        {"hint", text_field(keep_object, "phaseHint", "Soundance Kid keep view")}});
  }

  // Pearl Hat: both halves of her ability have to name the card she copies off
  // the discard pile, so "pay a card" says what the card buys.
  {
    GameConfig pearl_config{
        .seed = 2024,
        .characters_per_side = 2,
        .shuffle_decks = false,
        .shuffle_characters = false,
    };
    pearl_config.character_rosters[0] = {
        static_cast<int>(CharacterName::kTexKiller),
        static_cast<int>(CharacterName::kTheStranger)};
    pearl_config.character_rosters[1] = {
        static_cast<int>(CharacterName::kPearlHat),
        static_cast<int>(CharacterName::kJackRansome)};
    pearl_config.starting_decks[0] = DeckWithTop(Side::kLaw, {CardName::kWater});
    GameState state(pearl_config);
    state.ApplyAction(kChooseActiveLeftAction);
    state.ApplyAction(kChooseActiveLeftAction);
    // Law leaves an Action card on top of the discard pile, then hands over the
    // turn — including what it sheds at the hand limit, which must stay an
    // Action for Pearl Hat to have anything to copy.
    const auto& law_hand = state.Players()[0].hand;
    const auto water = std::find_if(
        law_hand.begin(), law_hand.end(), [](int instance) {
          return GetCardInstance(instance).name == CardName::kWater;
        });
    if (water == law_hand.end()) {
      throw std::runtime_error("ability-prompt smoke did not draw its Action card");
    }
    state.ApplyAction(PlayCardAction(*water, Target::kOwnActive));
    for (int step = 0; step < 200; ++step) {
      if (state.CurrentPhase() == Phase::kPlay && state.CurrentPlayer() == 1) break;
      const std::vector<int> legal = state.LegalActions();
      if (legal.empty()) {
        throw std::runtime_error("ability-prompt smoke ran out of moves");
      }
      int chosen = legal.front();
      if (state.CurrentPhase() == Phase::kDiscard) {
        for (const int action : legal) {
          const ActionKey key = DecodeAction(action);
          if (GetCard(GetCardInstance(key.card_instance).name).kind ==
              CardKind::kAction) {
            chosen = action;
            break;
          }
        }
      } else if (std::find(legal.begin(), legal.end(), kEndPlayAction) !=
                 legal.end()) {
        chosen = kEndPlayAction;
      }
      state.ApplyAction(chosen);
    }
    const std::string copied(
        GetCard(GetCardInstance(state.DiscardPile().back()).name).display_name);
    state.ApplyAction(UseAbilityAction(CharacterName::kPearlHat));
    const auto [session_id, view] = view_of(11003, 1, std::move(state));
    const auto& object = JsonObject(view, "Pearl Hat view");
    const std::string cost_label =
        text_field(object, "phaseLabel", "Pearl Hat view");
    const std::string cost_hint =
        text_field(object, "phaseHint", "Pearl Hat view");
    if (cost_label.find("Pearl Hat") == std::string::npos ||
        cost_hint.find(copied) == std::string::npos) {
      throw std::runtime_error(
          "the Pearl Hat cost prompt does not name the copied card");
    }
    const auto& cost_actions =
        JsonArray(object.at("legalActions"), "Pearl Hat cost actions");
    if (cost_actions.empty()) {
      throw std::runtime_error("Pearl Hat has no card to pay with");
    }
    const JsonValue after_cost = app.ApplyHumanAction(
        session_id,
        static_cast<int>(std::get<double>(
            JsonObject(cost_actions.front(), "cost action").at("id").storage())));
    const auto& aim_object = JsonObject(after_cost, "Pearl Hat aim view");
    const std::string aim_label =
        text_field(aim_object, "phaseLabel", "Pearl Hat aim view");
    if (aim_label.find(copied) == std::string::npos) {
      throw std::runtime_error(
          "the Pearl Hat targeting prompt does not name the copied card");
    }
    for (const JsonValue& value :
         JsonArray(aim_object.at("legalActions"), "Pearl Hat aim actions")) {
      const auto& action = JsonObject(value, "Pearl Hat aim action");
      if (text_field(action, "label", "Pearl Hat aim action") !=
          "Copy " + copied) {
        throw std::runtime_error(
            "a replicated Action is offered as a normal play");
      }
    }
    verified.emplace_back(
        JsonValue::Object{{"label", cost_label}, {"hint", cost_hint}});
    verified.emplace_back(JsonValue::Object{
        {"label", aim_label},
        {"hint", text_field(aim_object, "phaseHint", "Pearl Hat aim view")}});
  }

  return JsonValue::Object{
      {"ok", true},
      {"verified", std::move(verified)},
  };
}

JsonValue RunWebPersistenceSmokeTest(const WebConfig& config) {
  if (config.session_dir.empty()) {
    throw std::invalid_argument("persistence smoke test needs a session dir");
  }
  std::string session_id;
  int actions_taken = 0;
  {
    BangWebApp first(config);
    // A full-width 31-bit seed: it must survive the journal's JSON round
    // trip exactly, or the replayed game diverges and restore fails.
    const JsonValue initial = first.NewSession(2147480009, 0, 4);
    const auto& object = JsonObject(initial, "initial state");
    session_id = std::get<std::string>(object.at("sessionId").storage());
    // Play a couple of decisions so the journal holds real history.
    for (int step = 0; step < 2; ++step) {
      const JsonValue state = first.SessionJson(session_id);
      const auto& state_object = JsonObject(state, "state");
      const auto& actions =
          JsonArray(state_object.at("legalActions"), "legal actions");
      if (actions.empty()) break;
      const auto& action = JsonObject(actions.front(), "action");
      first.ApplyHumanAction(
          session_id,
          static_cast<int>(std::get<double>(action.at("id").storage())));
      ++actions_taken;
    }
  }
  // A fresh app (as after a checkpoint-reload restart) must restore the
  // session from disk and keep it playable.
  BangWebApp second(config);
  const JsonValue restored = second.SessionJson(session_id);
  const auto& restored_object = JsonObject(restored, "restored state");
  if (std::get<std::string>(restored_object.at("sessionId").storage()) !=
      session_id) {
    throw std::runtime_error("restored session id mismatch");
  }
  const auto& legal =
      JsonArray(restored_object.at("legalActions"), "restored legal actions");
  const bool terminal =
      std::get<bool>(restored_object.at("terminal").storage());
  if (!terminal && legal.empty()) {
    throw std::runtime_error("restored session offers no actions");
  }
  if (!terminal) {
    const auto& action = JsonObject(legal.front(), "restored action");
    second.ApplyHumanAction(
        session_id,
        static_cast<int>(std::get<double>(action.at("id").storage())));
  }
  return JsonValue::Object{
      {"ok", true},
      {"sessionId", session_id},
      {"actionsBeforeRestart", actions_taken},
  };
}

int RunWebServer(const WebConfig& config) {
#ifdef __EMSCRIPTEN__
  (void)config;
  throw std::runtime_error(
      "the native HTTP server is unavailable in static WASM builds");
#else
  if (config.port < 0 || config.port > 65535) {
    throw std::invalid_argument("port must be between 0 and 65535");
  }
  if (!std::filesystem::is_regular_file(config.static_dir / "index.html")) {
    throw std::runtime_error("web/index.html was not found in static directory " +
                             config.static_dir.string());
  }
  InitializeSockets();
  BangWebApp app(config);
  const SocketHandle server = BindServer(config);
  sockaddr_in actual{};
  socklen_t actual_size = sizeof(actual);
  if (::getsockname(server, reinterpret_cast<sockaddr*>(&actual), &actual_size) != 0) {
    CloseSocket(server);
    throw std::runtime_error("getsockname failed");
  }
  const int port = ntohs(actual.sin_port);
  std::cout << "BANG! The Duel serving at http://" << config.host << ':' << port
            << "\nAI: "
            << (app.IsTrained() ? "trained League PPO checkpoint"
                                : "Frontier heuristic")
            << "\n" << std::flush;
  while (true) {
    sockaddr_in client_address{};
    socklen_t client_size = sizeof(client_address);
    const SocketHandle client = ::accept(
        server, reinterpret_cast<sockaddr*>(&client_address), &client_size);
    if (client == kInvalidSocket) {
      if (SocketCallInterrupted()) continue;
      std::cerr << "accept failed: " << SocketErrorText() << '\n';
      continue;
    }
    try {
      HandleClient(client, app, config);
    } catch (const std::exception& error) {
      std::cerr << "client handling failed: " << error.what() << '\n';
    }
    CloseSocket(client);
  }
#endif
}

namespace {

std::unique_ptr<BangWebApp> g_static_web_app;
std::string g_static_web_response;

const char* StoreStaticWebResponse(JsonValue value) {
  g_static_web_response = value.Dump();
  return g_static_web_response.c_str();
}

const char* StoreStaticWebError(const std::exception& error) {
  g_static_web_response =
      JsonValue(JsonValue::Object{{"error", error.what()}}).Dump();
  return g_static_web_response.c_str();
}

BangWebApp& StaticWebApp() {
  if (!g_static_web_app) {
    throw std::runtime_error("static web engine is not initialized");
  }
  return *g_static_web_app;
}

}  // namespace

extern "C" const char* bang_static_web_init(const char* static_policy_path) {
  try {
    WebConfig config;
    config.session_dir.clear();
    config.max_sessions = 16;
    if (static_policy_path != nullptr &&
        !std::string_view(static_policy_path).empty()) {
      config.static_policy = static_policy_path;
      config.require_trained_policy = true;
      config.stochastic_policy = false;
    }
    g_static_web_app = std::make_unique<BangWebApp>(std::move(config));
    return StoreStaticWebResponse(g_static_web_app->HealthJson());
  } catch (const std::exception& error) {
    g_static_web_app.reset();
    return StoreStaticWebError(error);
  }
}

extern "C" const char* bang_static_web_new_game(
    int seed, int human_player, int characters_per_side) {
  try {
    return StoreStaticWebResponse(StaticWebApp().NewSession(
        seed > 0 ? std::optional<int>(seed) : std::nullopt,
        human_player >= 0 ? std::optional<int>(human_player) : std::nullopt,
        characters_per_side > 0
            ? std::optional<int>(characters_per_side)
            : std::nullopt));
  } catch (const std::exception& error) {
    return StoreStaticWebError(error);
  }
}

extern "C" const char* bang_static_web_current_state(const char* session_id) {
  try {
    if (session_id == nullptr || std::string_view(session_id).empty()) {
      throw std::invalid_argument("session id is required");
    }
    return StoreStaticWebResponse(StaticWebApp().SessionJson(session_id));
  } catch (const std::exception& error) {
    return StoreStaticWebError(error);
  }
}

extern "C" const char* bang_static_web_apply_action(
    const char* session_id, int action_id) {
  try {
    if (session_id == nullptr || std::string_view(session_id).empty()) {
      throw std::invalid_argument("session id is required");
    }
    return StoreStaticWebResponse(
        StaticWebApp().ApplyHumanAction(session_id, action_id));
  } catch (const std::exception& error) {
    return StoreStaticWebError(error);
  }
}

}  // namespace bang
