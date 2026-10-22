#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

#include "bang/game.hpp"
#include "bang/input_layout.hpp"
#include "bang/observation_layout.hpp"
#include "bang/policy_architecture.hpp"
#include "bang/static_policy.hpp"
#include "bang/training_state.hpp"
#include "bang/web.hpp"

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

bang::GameConfig DeterministicConfig(int characters = 2) {
  bang::GameConfig config;
  config.seed = 7;
  config.characters_per_side = characters;
  config.shuffle_decks = false;
  config.shuffle_characters = false;
  config.character_rosters[0] = characters == 2 ? std::vector<int>{1, 9}
                                                 : std::vector<int>{1, 9, 0, 3};
  config.character_rosters[1] = characters == 2 ? std::vector<int>{12, 16}
                                                 : std::vector<int>{12, 16, 14, 15};
  return config;
}

std::vector<int> DeckWithTop(bang::Side side, const std::vector<bang::CardName>& draw_order,
                             bang::GameMode mode = bang::GameMode::kBase) {
  auto deck = bang::StartingDeck(side, mode);
  std::vector<int> selected;
  for (const auto name : draw_order) {
    const auto found = std::find_if(deck.begin(), deck.end(), [name](int instance) {
      return bang::GetCardInstance(instance).name == name;
    });
    if (found == deck.end()) {
      throw std::runtime_error("requested " +
                               std::string(bang::GetCard(name).display_name) +
                               " is unavailable in the configured deck");
    }
    selected.push_back(*found);
    deck.erase(found);
  }
  for (auto iterator = selected.rbegin(); iterator != selected.rend(); ++iterator) {
    deck.push_back(*iterator);
  }
  return deck;
}

int HandCard(const bang::GameState& state, int player, bang::CardName name) {
  const auto& hand = state.Players()[static_cast<std::size_t>(player)].hand;
  const auto found = std::find_if(hand.begin(), hand.end(), [name](int instance) {
    return bang::GetCardInstance(instance).name == name;
  });
  if (found == hand.end()) {
    throw std::runtime_error(std::string(bang::GetCard(name).display_name) +
                             " not found in player " + std::to_string(player) +
                             " hand");
  }
  return *found;
}

bool HasAction(const bang::GameState& state, int action) {
  const auto legal = state.LegalActions();
  return std::find(legal.begin(), legal.end(), action) != legal.end();
}

int SelectActionFor(const bang::GameState& state, bang::CardName name) {
  for (const int action : state.LegalActions()) {
    const auto decoded = bang::DecodeAction(action);
    if (decoded.kind == bang::ActionKind::kSelectCard &&
        bang::GetCardInstance(decoded.card_instance).name == name) {
      return action;
    }
  }
  throw std::runtime_error("selectable card not found");
}

bool CharacterHasEquipment(const bang::GameState& state, int player, int position,
                           bang::CardName name) {
  const auto& equipment =
      state.Players()[static_cast<std::size_t>(player)]
          .positions[static_cast<std::size_t>(position)]
          .equipment;
  return std::any_of(equipment.begin(), equipment.end(), [name](int instance) {
    return bang::GetCardInstance(instance).name == name;
  });
}

void EndCurrentTurn(bang::GameState& state) {
  const int current_turn = state.TurnPlayer();
  state.ApplyAction(bang::kEndPlayAction);
  while (!state.IsTerminal() && state.TurnPlayer() == current_turn) {
    const auto legal = state.LegalActions();
    Require(!legal.empty(), "turn cleanup has a legal discard");
    state.ApplyAction(legal.front());
  }
}

// Ends the turn like EndCurrentTurn, but discards down to the hand limit
// without giving up any card named in `keep`.
void EndCurrentTurnKeeping(bang::GameState& state, const std::vector<bang::CardName>& keep) {
  const int current_turn = state.TurnPlayer();
  state.ApplyAction(bang::kEndPlayAction);
  while (!state.IsTerminal() && state.TurnPlayer() == current_turn) {
    const auto legal = state.LegalActions();
    Require(!legal.empty(), "turn cleanup has a legal discard");
    int chosen = legal.front();
    for (const int action : legal) {
      const auto key = bang::DecodeAction(action);
      if (key.kind == bang::ActionKind::kDiscardCard &&
          std::find(keep.begin(), keep.end(),
                    bang::GetCardInstance(key.card_instance).name) == keep.end()) {
        chosen = action;
        break;
      }
    }
    state.ApplyAction(chosen);
  }
}

void RequireCardConservation(const bang::GameState& state) {
  std::vector<int> cards;
  cards.insert(cards.end(), state.DiscardPile().begin(), state.DiscardPile().end());
  cards.insert(cards.end(), state.CommonDeck().begin(), state.CommonDeck().end());
  cards.insert(cards.end(), state.ChoiceCards().begin(), state.ChoiceCards().end());
  for (const auto& player : state.Players()) {
    cards.insert(cards.end(), player.deck.begin(), player.deck.end());
    cards.insert(cards.end(), player.hand.begin(), player.hand.end());
    for (const auto& character : player.positions) {
      cards.insert(cards.end(), character.equipment.begin(), character.equipment.end());
    }
  }
  std::sort(cards.begin(), cards.end());
  // Exactly the cards this game's mode dealt: both sides' starting decks.
  std::vector<int> dealt = bang::StartingDeck(bang::Side::kLaw, state.Mode());
  const auto outlaw = bang::StartingDeck(bang::Side::kOutlaw, state.Mode());
  dealt.insert(dealt.end(), outlaw.begin(), outlaw.end());
  std::sort(dealt.begin(), dealt.end());
  Require(cards.size() == dealt.size(),
          "all physical cards remain in exactly one zone (found " +
              std::to_string(cards.size()) + ")");
  Require(cards == dealt, "card zones contain every dealt instance exactly once");
}

void CompleteSetup(bang::GameState& state) {
  state.ApplyAction(bang::kChooseActiveLeftAction);
  state.ApplyAction(bang::kChooseActiveLeftAction);
}

void TestStaticData() {
  // Base copies (0-79) and Renegade copies (80-119) are checked separately,
  // so the base game's composition stays pinned exactly as printed.
  std::array<std::array<int, bang::kCardNameCount>, bang::kSideCount> counts{};
  std::array<std::array<int, bang::kCardNameCount>, bang::kSideCount> renegade_counts{};
  std::array<int, 4> reveal_counts{};
  std::array<int, 4> renegade_reveal_counts{};
  for (const auto& instance : bang::CardInstances()) {
    Require(instance.renegade == (instance.id >= bang::kBaseCardInstanceCount),
            "Renegade copies follow the base copies");
    auto& side_counts = instance.renegade ? renegade_counts : counts;
    ++side_counts[static_cast<std::size_t>(instance.origin)][static_cast<std::size_t>(instance.name)];
    ++(instance.renegade ? renegade_reveal_counts : reveal_counts)
        [static_cast<std::size_t>(instance.reveal)];
  }
  constexpr std::array<std::array<int, bang::kCardNameCount>, 2>
      kExpectedCounts{{
          {1, 1, 0, 2, 0, 1, 4, 1, 0, 2, 1, 1, 1, 1, 1, 0,
           5, 2, 0, 0, 1, 2, 0, 1, 2, 1, 0, 1, 2, 4, 2},
          {0, 1, 1, 1, 2, 5, 0, 1, 1, 0, 0, 1, 1, 1, 1, 3,
           7, 0, 1, 2, 0, 0, 2, 1, 2, 0, 4, 0, 1, 1, 0},
      }};
  Require(counts == kExpectedCounts,
          "all 31 Law/Outlaw card counts match the rulebook");
  for (const auto& side_counts : counts) {
    int total = 0;
    for (const int value : side_counts) total += value;
    Require(total == bang::kCardsPerSide, "each side has exactly 40 cards");
  }
  Require(reveal_counts == std::array<int, 4>{20, 20, 10, 30},
          "printed reveal symbols follow the rulebook probabilities");

  // Renegades, as transcribed in docs/renegades_cards.md.
  using CN = bang::CardName;
  const auto expect_renegades =
      [&](bang::Side side, std::initializer_list<std::pair<CN, int>> expected) {
        std::array<int, bang::kCardNameCount> want{};
        for (const auto& [name, copies] : expected) want[static_cast<std::size_t>(name)] = copies;
        return renegade_counts[static_cast<std::size_t>(side)] == want;
      };
  Require(expect_renegades(bang::Side::kLaw,
                           {{CN::kAmbush, 1}, {CN::kAgility, 1}, {CN::kFeint, 1},
                            {CN::kMissed, 3}, {CN::kRicochet, 1}, {CN::kSwitch, 2},
                            {CN::kSpurs, 1}, {CN::kCarbine, 1}, {CN::kWalker, 2},
                            {CN::kWhitney, 1}, {CN::kBuffaloRifle, 2}, {CN::kHowitzer, 1},
                            {CN::kGoldPan, 1}, {CN::kPardner, 2}}) &&
              expect_renegades(bang::Side::kOutlaw,
                               {{CN::kAmbush, 1}, {CN::kAgility, 1}, {CN::kFeint, 1},
                                {CN::kKnife, 1}, {CN::kUnionPacific, 1}, {CN::kWhiskey, 1},
                                {CN::kSidekick, 1}, {CN::kLightning, 2}, {CN::kSavage, 1},
                                {CN::kSharps, 2}, {CN::kSpencer, 2}, {CN::kIndianChief, 1},
                                {CN::kDoubleBarrel, 2}, {CN::kSlap, 2}, {CN::kLuckyPenny, 1}}),
          "all 40 Renegade card copies match the printed cards");
  Require(renegade_reveal_counts == std::array<int, 4>{10, 10, 5, 15},
          "Renegade reveal symbols match the printed cards");

  // Decks and rosters by mode: base games deal exactly the base set.
  for (const bang::Side side : {bang::Side::kLaw, bang::Side::kOutlaw}) {
    const auto base_deck = bang::StartingDeck(side);
    const auto reinforced_deck = bang::StartingDeck(side, bang::GameMode::kReinforcements);
    const bool base_only = std::all_of(base_deck.begin(), base_deck.end(), [&](int id) {
      const auto& card = bang::GetCardInstance(id);
      return card.origin == side && !card.renegade;
    });
    const bool reinforced = std::all_of(reinforced_deck.begin(), reinforced_deck.end(), [&](int id) {
      return bang::GetCardInstance(id).origin == side;
    });
    Require(base_deck.size() == 40 && base_only && reinforced_deck.size() == 60 && reinforced &&
                std::equal(base_deck.begin(), base_deck.end(), reinforced_deck.begin()),
            "Reinforcements adds the side's 20 Renegade cards to its base deck");
    const auto base_roster = bang::CharacterIds(side);
    const auto reinforced_roster = bang::CharacterIds(side, bang::GameMode::kReinforcements);
    Require(base_roster.size() == 12 && reinforced_roster.size() == 18 &&
                std::all_of(base_roster.begin(), base_roster.end(),
                            [](int id) { return !bang::GetCharacter(id).renegade; }) &&
                std::all_of(reinforced_roster.begin(), reinforced_roster.end(),
                            [&](int id) { return bang::GetCharacter(id).side == side; }),
            "Reinforcements adds the side's 6 Renegade characters to its roster");
  }

  const std::array bang_cards{
      CN::kCarbine, CN::kColt, CN::kFanning, CN::kPeacemaker,
      CN::kRemington, CN::kSchofield, CN::kShotgun, CN::kWinchester,
      CN::kLightning, CN::kSavage, CN::kSharps, CN::kSpencer,
      CN::kWalker, CN::kWhitney};
  const std::array additional_hits{
      CN::kDerringer, CN::kGatling, CN::kKnife,
      CN::kBuffaloRifle, CN::kDoubleBarrel, CN::kHowitzer, CN::kPardner};
  const std::array avoids{CN::kMissed, CN::kReturnFire, CN::kAgility,
                          CN::kFeint, CN::kLuckyPenny, CN::kPardner};
  // Stampede prints both the reload cylinder and the always-active arrows.
  const std::array reloadable{CN::kClock, CN::kScope, CN::kStampede,
                              CN::kWater, CN::kSidekick, CN::kSwitch};
  const std::array always_active{CN::kDynamite, CN::kRattlesnake, CN::kStampede};
  const std::array resources{CN::kBuffaloRifle, CN::kDoubleBarrel, CN::kGoldPan,
                             CN::kHowitzer, CN::kIndianChief, CN::kLuckyPenny,
                             CN::kPardner, CN::kSlap};
  const auto contains = [](const auto& values, auto value) {
    return std::find(values.begin(), values.end(), value) != values.end();
  };
  for (int index = 0; index < bang::kCardNameCount; ++index) {
    const auto name = static_cast<bang::CardName>(index);
    const auto& card = bang::GetCard(name);
    Require(card.name == name && !card.rules_text.empty(),
            "every card has browser-visible rules text");
    const bool is_bang = contains(bang_cards, name);
    Require((card.kind == bang::CardKind::kBang) == is_bang,
            "red BANG card classification matches the rulebook");
    Require((card.kind == bang::CardKind::kResource) == contains(resources, name),
            "green Resource classification matches the printed cards");
    Require(card.has_hit ==
                (is_bang || contains(additional_hits, name)),
            "printed hit symbols match the rulebook");
    Require(card.has_avoid == contains(avoids, name),
            "printed avoid symbols match the rulebook");
    Require(card.reloadable == contains(reloadable, name),
            "reload symbols match the printed cards");
    Require(card.always_active == contains(always_active, name),
            "always-active Equipment symbols match the printed cards");
  }

  Require(bang::Characters().size() == 36, "there are 24 base and 12 Renegade characters");
  constexpr std::array<int, bang::kCharacterCount> kMaximumLife{
      4, 4, 4, 5, 4, 5, 3, 5, 4, 4, 5, 4,
      4, 5, 4, 3, 4, 3, 4, 4, 4, 4, 4, 4,
      3, 4, 4, 3, 4, 5, 5, 4, 3, 4, 4, 3};
  const std::array marked_characters{
      bang::CharacterName::kBartMasterson,
      bang::CharacterName::kBuffaloBell,
      bang::CharacterName::kTheStranger,
      bang::CharacterName::kSoundanceKid,
      bang::CharacterName::kCharlieBasset,
      bang::CharacterName::kTheMarshal,
      bang::CharacterName::kBigSteve,
      bang::CharacterName::kDutchHarry,
      bang::CharacterName::kJimLonghair};
  for (int index = 0; index < bang::kCharacterCount; ++index) {
    const auto& character = bang::GetCharacter(index);
    Require(!character.ability_text.empty(),
            "every character has browser-visible ability text");
    Require(character.id == index &&
                character.max_life ==
                    kMaximumLife[static_cast<std::size_t>(index)],
            "character IDs and life values match the rulebook");
    const bool renegade = index >= bang::kBaseCharacterCount;
    const int in_set = renegade ? index - bang::kBaseCharacterCount : index;
    const int per_side = renegade ? bang::kRenegadeCharactersPerSide : bang::kCharactersPerSide;
    Require(character.renegade == renegade &&
                character.side == (in_set < per_side ? bang::Side::kLaw : bang::Side::kOutlaw),
            "each character belongs to the printed side");
    Require(character.always_active ==
                contains(marked_characters,
                         static_cast<bang::CharacterName>(index)),
            "character always-active symbols match the printed cards");
  }
}

void TestInputLayouts() {
  // The engine's layout is built from the same block table as the base one.
  Require(bang::kObservationSize == bang::ObservationSizeFor(bang::kLayoutDims) &&
              bang::kActionFeatureSize ==
                  bang::BlockTotal(bang::ActionFeatureBlockSizes(bang::kLayoutDims)),
          "input sizes come from the block tables");

  // Mapping a layout onto itself is the identity.
  const auto same = bang::LayoutIndexMap(
      bang::LayoutKind::kTraining, bang::kLayoutDims, bang::kLayoutDims);
  bool identity = static_cast<int>(same.size()) == bang::kTrainingObservationSize;
  for (std::size_t index = 0; identity && index < same.size(); ++index) {
    identity = same[index] == static_cast<int>(index);
  }
  Require(identity, "a layout maps onto itself unchanged");

  // Onto a larger layout every block keeps its old prefix, so an index keeps
  // its meaning: a card's name slot, a character's slot, a card copy's slot.
  bang::LayoutDims larger = bang::kBaseLayoutDims;
  larger.card_names += 5;
  larger.characters += 3;
  larger.card_instances += 10;
  larger.phases += 4;
  larger.card_kinds += 1;
  const auto obs = bang::LayoutIndexMap(
      bang::LayoutKind::kObservation, bang::kBaseLayoutDims, larger);
  const auto at = [](const bang::LayoutDims& dims, bang::ObsBlock block, int index) {
    return bang::ObsStart(dims, block) + index;
  };
  Require(static_cast<int>(obs.size()) == bang::ObservationSizeFor(bang::kBaseLayoutDims) &&
              obs[static_cast<std::size_t>(at(bang::kBaseLayoutDims, bang::ObsBlock::kPrivateHand, 30))] ==
                  at(larger, bang::ObsBlock::kPrivateHand, 30) &&
              obs[static_cast<std::size_t>(at(bang::kBaseLayoutDims, bang::ObsBlock::kOpponentRearguardCharacter, 23))] ==
                  at(larger, bang::ObsBlock::kOpponentRearguardCharacter, 23) &&
              obs[static_cast<std::size_t>(at(bang::kBaseLayoutDims, bang::ObsBlock::kTopDiscardOrigin, 1))] ==
                  at(larger, bang::ObsBlock::kTopDiscardOrigin, 1),
          "observation indices keep their block and position on a larger layout");
  const auto features = bang::LayoutIndexMap(
      bang::LayoutKind::kActionFeature, bang::kBaseLayoutDims, larger);
  Require(features[static_cast<std::size_t>(bang::ActionFeatureStart(
              bang::kBaseLayoutDims, bang::ActionFeatureBlock::kOption))] ==
              bang::ActionFeatureStart(larger, bang::ActionFeatureBlock::kOption),
          "action features keep their block on a larger layout");
  bool threw = false;
  try {
    (void)bang::LayoutIndexMap(bang::LayoutKind::kObservation, larger, bang::kBaseLayoutDims);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  Require(threw, "a layout cannot be mapped onto a smaller one");
}

void TestWebAppSmoke() {
  const std::string result = bang::RunWebSmokeTest().Dump();
  Require(result.find("\"ok\":true") != std::string::npos,
          "web app creates a scoped session and advances a human action");
}

void TestWebForcedResponses() {
  const std::string result = bang::RunWebForcedResponseSmokeTest().Dump();
  Require(result.find("\"ok\":true") != std::string::npos,
          "web app automatically resolves unavoidable human hits");
}

void TestWebAbilityPrompts() {
  const std::string result = bang::RunWebAbilityPromptSmokeTest().Dump();
  Require(result.find("\"ok\":true") != std::string::npos,
          "web ability prompts name the character and explain each option");
}

void TestWebSessionPersistence() {
  bang::WebConfig config;
  config.session_dir = std::filesystem::temp_directory_path() /
                       "bang_web_persistence_test";
  std::filesystem::remove_all(config.session_dir);
  const std::string result = bang::RunWebPersistenceSmokeTest(config).Dump();
  Require(result.find("\"ok\":true") != std::string::npos,
          "a fresh web app restores a journaled session after a restart");
  std::filesystem::remove_all(config.session_dir);
}

void TestStaticWebBridge() {
  const std::string health = bang::bang_static_web_init("");
  Require(health.find("\"ok\":true") != std::string::npos,
          "static web engine initializes");

  const std::string initial = bang::bang_static_web_new_game(4242, 0, 4);
  const auto parsed = bang::ParseJson(initial);
  Require(parsed.has_value(), "static web engine returns JSON");
  const auto* root = std::get_if<bang::JsonValue::Object>(&parsed->storage());
  Require(root != nullptr, "static web state is an object");
  const auto* session_id =
      std::get_if<std::string>(&root->at("sessionId").storage());
  const auto* legal =
      std::get_if<bang::JsonValue::Array>(&root->at("legalActions").storage());
  Require(session_id != nullptr && !session_id->empty(),
          "static web state has a session id");
  Require(legal != nullptr && !legal->empty(),
          "static web state exposes the human decision");

  const auto* action =
      std::get_if<bang::JsonValue::Object>(&legal->front().storage());
  Require(action != nullptr, "static web legal action is an object");
  const auto* action_id =
      std::get_if<double>(&action->at("id").storage());
  Require(action_id != nullptr, "static web legal action has an id");

  const std::string next = bang::bang_static_web_apply_action(
      session_id->c_str(), static_cast<int>(*action_id));
  Require(next.find("{\"error\":") != 0,
          "static web engine applies a human action");
  const std::string current =
      bang::bang_static_web_current_state(session_id->c_str());
  Require(current.find(*session_id) != std::string::npos,
          "static web engine returns the current session");
  Require(std::string(bang::bang_static_web_current_state(""))
              .find("session id is required") != std::string::npos,
          "static web engine reports invalid bridge input");
}

void TestPolicyArchitectures() {
  Require(
      bang::ResolveModelType("v2") == "bang_structured_v2" &&
          bang::ResolveModelType("v3") == "bang_structured_v3" &&
          bang::ResolveModelType("bang_structured_v3") == "bang_structured_v3",
      "model names resolve to checkpoint metadata identifiers");
  Require(
      bang::ArchitectureForModelType("bang_structured_v2") ==
              bang::PolicyArchitectureV2() &&
          bang::ArchitectureForModelType("bang_structured_v3") ==
              bang::PolicyArchitectureV3(),
      "model types resolve to their architecture preset");
  const auto large = bang::PolicyArchitectureV3();
  Require(
      large.residual_blocks == 4 && large.state_hidden == 512 &&
          large.action_hidden == 160,
      "the v3 preset keeps its published widths");
  bool rejected = false;
  try {
    bang::ArchitectureForModelType("bang_structured_v9");
  } catch (const std::exception&) {
    rejected = true;
  }
  Require(rejected, "unknown model types are rejected");

  // A network shaped like v3 must evaluate through all four residual blocks.
  const auto linear = [](int input, int output) {
    return bang::StaticPolicyLinear{
        .input_size = input,
        .output_size = output,
        .weights = std::vector<float>(
            static_cast<std::size_t>(input) *
                static_cast<std::size_t>(output),
            0.0F),
        .bias = std::vector<float>(
            static_cast<std::size_t>(output), 0.0F),
    };
  };
  bang::StaticPolicyNetwork policy;
  policy.model_type = "bang_structured_v3";
  policy.public_encoder = linear(
      bang::kTrainingObservationOwnHandOffset, large.public_hidden);
  policy.identity_encoder = linear(
      bang::kTrainingObservationSize - bang::kTrainingObservationOwnHandOffset,
      large.identity_hidden);
  policy.state_fusion = linear(
      large.public_hidden + large.identity_hidden, large.state_hidden);
  for (int block = 0; block < large.residual_blocks; ++block) {
    policy.residuals.push_back(
        linear(large.state_hidden, large.state_hidden));
  }
  policy.state_policy = linear(large.state_hidden, large.action_hidden);
  policy.action_encoder =
      linear(bang::kActionFeatureSize, large.action_hidden);
  policy.actor = linear(large.action_hidden * 4, 1);
  policy.public_encoder.weights[0] = 1.0F;
  policy.state_fusion.weights[0] = 1.0F;
  policy.state_policy.weights[0] = 1.0F;
  policy.actor.weights[0] = 1.0F;
  policy.actor.bias[0] = 0.5F;

  std::array<float, bang::kTrainingObservationSize> observation{};
  observation[0] = 1.0F;
  const std::vector<bang::ActionFeature> actions(2);
  const std::vector<float> logits =
      bang::StaticPolicyLogits(policy, observation, actions);
  // tanh once in the public encoder, once in the fusion, once per residual
  // block, and once in the state-policy projection.
  float state = std::tanh(1.0F);
  for (int layer = 0; layer < large.residual_blocks + 2; ++layer) {
    state = std::tanh(state);
  }
  Require(
      logits.size() == 2 && std::abs(logits[0] - (0.5F + state)) < 1.0e-5F,
      "a four-block static policy propagates through every residual");
}

void TestStaticPolicyInference() {
  const auto linear = [](int input, int output) {
    return bang::StaticPolicyLinear{
        .input_size = input,
        .output_size = output,
        .weights = std::vector<float>(
            static_cast<std::size_t>(input * output), 0.0F),
        .bias = std::vector<float>(
            static_cast<std::size_t>(output), 0.0F),
    };
  };
  bang::StaticPolicyNetwork policy;
  policy.public_encoder =
      linear(bang::kTrainingObservationOwnHandOffset, 128);
  policy.identity_encoder = linear(
      bang::kTrainingObservationSize -
          bang::kTrainingObservationOwnHandOffset,
      64);
  policy.state_fusion = linear(192, 128);
  policy.residuals = {linear(128, 128), linear(128, 128)};
  policy.state_policy = linear(128, 64);
  policy.action_encoder = linear(bang::kActionFeatureSize, 64);
  policy.actor = linear(256, 1);

  policy.public_encoder.weights[0] = 1.0F;
  policy.state_fusion.weights[0] = 1.0F;
  policy.state_policy.weights[0] = 1.0F;
  policy.action_encoder.weights[0] = 1.0F;
  policy.actor.weights[0] = 1.0F;
  policy.actor.weights[64] = 2.0F;
  policy.actor.bias[0] = 0.5F;

  std::array<float, bang::kTrainingObservationSize> observation{};
  observation[0] = 1.0F;
  std::vector<bang::ActionFeature> actions(2);
  actions[1].values[0] = 1.0F;
  const std::vector<float> logits =
      bang::StaticPolicyLogits(policy, observation, actions);
  float state = std::tanh(1.0F);
  for (int layer = 0; layer < 4; ++layer) state = std::tanh(state);
  Require(logits.size() == 2 &&
              std::abs(logits[0] - (0.5F + state)) < 1.0e-5F &&
              std::abs(
                  logits[1] -
                  (0.5F + state + 2.0F * std::tanh(1.0F))) <
                  1.0e-5F,
          "static policy reproduces structured actor inference");
}

void TestSetupAndHiddenSelection() {
  bang::GameState state(DeterministicConfig());
  Require(state.CurrentPhase() == bang::Phase::kChooseActive, "setup asks Law to choose active");
  Require(state.CurrentPlayer() == 0, "Law chooses first");
  Require(state.Players()[0].hand.size() == 4, "Law starting hand is four");
  Require(state.Players()[1].hand.size() == 5, "Outlaw starting hand is five");
  Require(state.LegalActions() ==
              std::vector<int>{bang::kChooseActiveLeftAction, bang::kChooseActiveRightAction},
          "either drawn character can be active");

  const auto outlaw_view = state.ObservationTensor(1);
  constexpr int kOpponentBlock =
      bang::obs_layout::kPlayerBlocksOffset + bang::obs_layout::kPlayerBlockSize;
  for (int i = 0; i < 48; ++i) {
    Require(outlaw_view[static_cast<std::size_t>(kOpponentBlock + i)] == 0.0F,
            "opponent candidates stay hidden during simultaneous setup");
  }

  state.ApplyAction(bang::kChooseActiveRightAction);
  Require(state.CurrentPlayer() == 1, "Outlaw chooses second");
  state.ApplyAction(bang::kChooseActiveLeftAction);
  Require(state.CurrentPhase() == bang::Phase::kPlay, "first turn begins after both choices");
  Require(state.CurrentPlayer() == 0, "Law takes first turn");
  Require(state.Players()[0].active_position == 1, "Law selection is retained");
  Require(state.Players()[0].hand.size() == 6, "Law draws two for its first turn");

  bang::GameState longer(DeterministicConfig(4));
  const auto law_setup = longer.ObservationTensor(0);
  Require(law_setup[bang::obs_layout::kPrivateReserveOffset +
                    static_cast<int>(bang::CharacterName::kAlanPinkertoon)] ==
              1.0F &&
              law_setup[bang::obs_layout::kPrivateReserveOffset +
                        static_cast<int>(
                            bang::CharacterName::kBillTightman)] == 1.0F,
          "a player observes the known characters remaining in their reserve");
}

void TestObservationDoesNotLeakOpponentHand() {
  auto first_config = DeterministicConfig();
  auto second_config = DeterministicConfig();
  first_config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kColt, bang::CardName::kBarrel, bang::CardName::kMissed,
       bang::CardName::kClock, bang::CardName::kGunBelt});
  second_config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kKnife, bang::CardName::kStealing, bang::CardName::kBeer,
       bang::CardName::kDynamite, bang::CardName::kRattlesnake});
  bang::GameState first(first_config);
  bang::GameState second(second_config);
  CompleteSetup(first);
  CompleteSetup(second);
  Require(first.ObservationTensor(0) == second.ObservationTensor(0),
          "observer tensor is unchanged by opponent's hidden hand identities");
  Require(first.TrainingObservationTensor(0) ==
              second.TrainingObservationTensor(0),
          "training tensor is unchanged by opponent hidden hand identities");
  Require(first.ObservationTensor(1) != second.ObservationTensor(1),
          "each player still observes their own private hand");
  Require(first.PrivilegedObservationTensor(0) !=
              second.PrivilegedObservationTensor(0),
          "training-only critic tensor contains exact hidden state");

  auto first_reserve = DeterministicConfig(4);
  auto second_reserve = DeterministicConfig(4);
  second_reserve.character_rosters[1] = {
      static_cast<int>(bang::CharacterName::kBabeLeroy),
      static_cast<int>(bang::CharacterName::kJackRansome),
      static_cast<int>(bang::CharacterName::kPearlHat),
      static_cast<int>(bang::CharacterName::kSidCurry)};
  bang::GameState first_long(first_reserve);
  bang::GameState second_long(second_reserve);
  CompleteSetup(first_long);
  CompleteSetup(second_long);
  Require(first_long.ObservationTensor(0) ==
              second_long.ObservationTensor(0),
          "an opponent's reserve composition remains hidden");
  Require(first_long.ObservationTensor(1) !=
              second_long.ObservationTensor(1),
          "each player retains private knowledge of their reserve group");
}

void TestTrainingContract() {
  Require(bang::kTrainingObservationSize > bang::kObservationSize,
          "training observation extends the browser observation");
  // 91 for the base game; the Renegades expansion's cards, characters, and
  // Resource move bring it to 128.
  Require(bang::kActionFeatureSize <= 160,
          "semantic action features stay compact for batched inference");
  for (int action = 0; action < bang::kActionCount; ++action) {
    const auto decoded = bang::DecodeAction(action);
    const auto feature = bang::EncodeAction(action);
    Require(
        feature.values[bang::kActionFeatureKindOffset +
                       static_cast<std::size_t>(decoded.kind)] == 1.0F,
        "every stable action ID encodes its semantic kind");
  }

  int first_colt = -1;
  int equivalent_colt = -1;
  for (const auto& instance : bang::CardInstances()) {
    if (instance.origin != bang::Side::kLaw ||
        instance.name != bang::CardName::kColt) {
      continue;
    }
    if (first_colt < 0) {
      first_colt = instance.id;
      continue;
    }
    if (bang::GetCardInstance(first_colt).reveal == instance.reveal) {
      equivalent_colt = instance.id;
      break;
    }
  }
  if (equivalent_colt >= 0) {
    Require(
        bang::EncodeAction(bang::PlayCardAction(
            first_colt, bang::Target::kOpponentActive)).values ==
            bang::EncodeAction(bang::PlayCardAction(
                equivalent_colt, bang::Target::kOpponentActive)).values,
        "physically equivalent card copies share action semantics");
  }

  bang::GameState state(DeterministicConfig(4));
  bang::ObserverHistory law_history(0);
  bang::ObserverHistory outlaw_history(1);
  law_history.Record(state);
  outlaw_history.Record(state);
  const auto initial_law_hash = law_history.Hash();
  state.ApplyAction(bang::kChooseActiveLeftAction);
  law_history.Record(state);
  outlaw_history.Record(state);
  Require(law_history.Hash() != initial_law_hash,
          "observer history hashes visible transitions");
  Require(outlaw_history.LatestObservation() ==
              state.TrainingObservationTensor(1),
          "history is derived from the scoped training tensor");
  Require(law_history.Events().back().legal_actions.empty(),
          "history exposes legal actions only to the decision player");

  const auto legal = state.LegalActions();
  const auto features = state.LegalActionFeatures();
  Require(legal.size() == features.size(),
          "every legal action has one semantic feature row");
  for (std::size_t index = 0; index < legal.size(); ++index) {
    Require(legal[index] == features[index].first,
            "legal action feature ordering is stable");
  }
}

void TestEquipmentPlacementAndScope() {
  auto config = DeterministicConfig();
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {bang::CardName::kBank, bang::CardName::kGunBelt, bang::CardName::kBarrel,
       bang::CardName::kScope, bang::CardName::kDerringer,
       bang::CardName::kColt});
  bang::GameState state(config);
  CompleteSetup(state);

  const int bank = HandCard(state, 0, bang::CardName::kBank);
  Require(HasAction(state, bang::PlayCardAction(bank, bang::Target::kOwnRearguard)),
          "Equipment can be placed on own RC");
  Require(HasAction(state, bang::PlayCardAction(bank, bang::Target::kOpponentActive)),
          "Equipment can be placed on opponent AC");
  Require(!HasAction(state, bang::PlayCardAction(bank, bang::Target::kOwnActive)),
          "ordinary Equipment cannot be placed on own AC");
  Require(!HasAction(state, bang::PlayCardAction(bank, bang::Target::kOpponentRearguard)),
          "ordinary Equipment cannot be placed on opponent RC");
  state.ApplyAction(bang::PlayCardAction(bank, bang::Target::kOwnRearguard));

  const int scope = HandCard(state, 0, bang::CardName::kScope);
  state.ApplyAction(bang::PlayCardAction(scope, bang::Target::kOwnRearguard));
  const int colt = HandCard(state, 0, bang::CardName::kColt);
  Require(!HasAction(state, bang::PlayCardAction(colt, bang::Target::kOpponentRearguard)),
          "Scope on RC is inactive");
  state.ApplyAction(bang::kSwapAction);
  Require(HasAction(state, bang::PlayCardAction(colt, bang::Target::kOpponentRearguard)),
          "Scope on AC permits targeting opponent RC");
  const int derringer = HandCard(state, 0, bang::CardName::kDerringer);
  Require(HasAction(state, bang::PlayCardAction(
                               derringer,
                               bang::Target::kOpponentRearguard)),
          "Scope applies to non-BANG hit cards such as Derringer");

  const auto& active = state.Players()[0].positions[state.Players()[0].active_position];
  Require(active.equipment.size() == 2, "Equipment stays with its character during swaps");
}

void TestDuplicateEquipmentReplacement() {
  auto config = DeterministicConfig();
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {bang::CardName::kBarrel, bang::CardName::kWater, bang::CardName::kMissed,
       bang::CardName::kWagon, bang::CardName::kDerringer, bang::CardName::kReturnFire});
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kBarrel, bang::CardName::kWater, bang::CardName::kMissed,
       bang::CardName::kWagon, bang::CardName::kKnife, bang::CardName::kStealing,
       bang::CardName::kBeer});
  bang::GameState state(config);
  CompleteSetup(state);
  const int old_barrel = HandCard(state, 0, bang::CardName::kBarrel);
  state.ApplyAction(bang::PlayCardAction(old_barrel, bang::Target::kOpponentActive));
  EndCurrentTurn(state);
  state.ApplyAction(bang::kSwapAction);
  const int new_barrel = HandCard(state, 1, bang::CardName::kBarrel);
  state.ApplyAction(bang::PlayCardAction(new_barrel, bang::Target::kOwnRearguard));

  const auto& equipment = state.Players()[1].positions[0].equipment;
  Require(equipment == std::vector<int>{new_barrel},
          "same-name Equipment replaces the copy on that character");
  Require(std::find(state.DiscardPile().begin(), state.DiscardPile().end(), old_barrel) !=
              state.DiscardPile().end(),
          "replaced Equipment enters the common discard pile");
}

void TestRearguardEquipmentWakesWhenTargeted() {
  auto config = DeterministicConfig();
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {bang::CardName::kScope, bang::CardName::kWater, bang::CardName::kWagon,
       bang::CardName::kDerringer, bang::CardName::kReturnFire, bang::CardName::kColt});
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kHat, bang::CardName::kWater, bang::CardName::kMissed,
       bang::CardName::kWagon, bang::CardName::kKnife, bang::CardName::kStealing,
       bang::CardName::kBeer});
  bang::GameState state(config);
  CompleteSetup(state);
  state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, bang::CardName::kScope),
                                         bang::Target::kOwnRearguard));
  state.ApplyAction(bang::kSwapAction);
  EndCurrentTurn(state);
  const int hat = HandCard(state, 1, bang::CardName::kHat);
  state.ApplyAction(bang::PlayCardAction(hat, bang::Target::kOwnRearguard));
  EndCurrentTurn(state);

  const int colt = HandCard(state, 0, bang::CardName::kColt);
  state.ApplyAction(bang::PlayCardAction(colt, bang::Target::kOpponentRearguard));
  Require(HasAction(state, bang::AvoidHitAction(hat)),
          "targeted RC may temporarily use its Hat");
}

void TestResourcesWaitAndUse() {
  using CN = bang::CardName;
  auto config = DeterministicConfig();
  config.mode = bang::GameMode::kReinforcements;
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {CN::kGoldPan, CN::kBuffaloRifle, CN::kBuffaloRifle, CN::kColt, CN::kMissed, CN::kMissed},
      bang::GameMode::kReinforcements);
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw, {CN::kMissed, CN::kMissed, CN::kBeer, CN::kWagon, CN::kMissed},
      bang::GameMode::kReinforcements);
  bang::GameState state(config);
  CompleteSetup(state);
  const int gold_pan = HandCard(state, 0, CN::kGoldPan);
  const int rifle = HandCard(state, 0, CN::kBuffaloRifle);
  state.ApplyAction(bang::PlayCardAction(gold_pan, bang::Target::kOwnRearguard));
  state.ApplyAction(bang::PlayCardAction(rifle, bang::Target::kOwnRearguard));
  state.ApplyAction(bang::kSwapAction);
  Require(!HasAction(state, bang::UseResourceAction(gold_pan, bang::Target::kOwnActive)) &&
              !HasAction(state, bang::UseResourceAction(rifle, bang::Target::kOpponentActive)),
          "a Resource cannot be used on the turn it was put into play");
  EndCurrentTurn(state);
  EndCurrentTurn(state);

  Require(HasAction(state, bang::UseResourceAction(rifle, bang::Target::kOpponentActive)) &&
              HasAction(state, bang::UseResourceAction(rifle, bang::Target::kOpponentRearguard)),
          "Buffalo Rifle may target the opponent's active or rearguard from the next turn");
  const int outlaw_rear = 1 - state.Players()[1].active_position;
  const int rear_life = state.Players()[1].positions[static_cast<std::size_t>(outlaw_rear)].life;
  const auto hand_before = state.Players()[0].hand.size();
  state.ApplyAction(bang::UseResourceAction(rifle, bang::Target::kOpponentRearguard));
  Require(state.Players()[0].hand.size() == hand_before + 1 &&
              state.CurrentPhase() == bang::Phase::kRespondToHit && state.CurrentPlayer() == 1,
          "Buffalo Rifle draws immediately, then the opponent answers its hit");
  state.ApplyAction(bang::kTakeHitAction);
  Require(state.Players()[1].positions[static_cast<std::size_t>(outlaw_rear)].life == rear_life - 1,
          "Buffalo Rifle hits the rearguard it targeted");
  const int colt = HandCard(state, 0, CN::kColt);
  Require(state.CurrentPlayer() == 0 && HasAction(state, bang::PlayCardAction(colt)),
          "a hit Resource is not a BANG! card and leaves the BANG! limit untouched");
  state.ApplyAction(bang::PlayCardAction(colt));
  state.ApplyAction(bang::kTakeHitAction);
  Require(state.DiscardPile().back() == colt, "the Colt tops the discard pile");
  state.ApplyAction(bang::UseResourceAction(gold_pan, bang::Target::kOwnActive));
  const auto& law_hand = state.Players()[0].hand;
  Require(std::find(law_hand.begin(), law_hand.end(), colt) != law_hand.end() &&
              state.DiscardPile().back() == gold_pan,
          "Gold Pan takes the top discard before being discarded itself");

  // A Resource on the rearguard waits until its character becomes active.
  const int second_rifle = HandCard(state, 0, CN::kBuffaloRifle);
  state.ApplyAction(bang::PlayCardAction(second_rifle, bang::Target::kOwnRearguard));
  EndCurrentTurn(state);
  EndCurrentTurn(state);
  Require(!HasAction(state, bang::UseResourceAction(second_rifle, bang::Target::kOpponentActive)),
          "a Resource on the rearguard cannot be used");
  state.ApplyAction(bang::kSwapAction);
  Require(HasAction(state, bang::UseResourceAction(second_rifle, bang::Target::kOpponentActive)),
          "the Resource works once its character is active");
}

void TestAvoidResources() {
  using CN = bang::CardName;
  auto config = DeterministicConfig();
  config.mode = bang::GameMode::kReinforcements;
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {CN::kPardner, CN::kMissed, CN::kColt, CN::kMissed, CN::kMissed, CN::kMissed},
      bang::GameMode::kReinforcements);
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw, {CN::kLuckyPenny, CN::kColt, CN::kMissed, CN::kMissed, CN::kMissed},
      bang::GameMode::kReinforcements);
  bang::GameState state(config);
  CompleteSetup(state);
  const int pardner = HandCard(state, 0, CN::kPardner);
  state.ApplyAction(bang::PlayCardAction(pardner, bang::Target::kOwnRearguard));
  state.ApplyAction(bang::kSwapAction);
  EndCurrentTurn(state);

  // Pardner, put into play last turn, answers a hit on the opponent's turn.
  const int law_active = state.Players()[0].active_position;
  const int law_life = state.Players()[0].positions[static_cast<std::size_t>(law_active)].life;
  state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, CN::kColt)));
  Require(HasAction(state, bang::AvoidHitAction(pardner)), "Pardner may avoid as a Resource in play");
  const auto law_hand = state.Players()[0].hand.size();
  state.ApplyAction(bang::AvoidHitAction(pardner));
  Require(state.Players()[0].positions[static_cast<std::size_t>(law_active)].life == law_life &&
              state.Players()[0].hand.size() == law_hand,
          "Pardner avoids the hit without drawing");

  const int penny = HandCard(state, 1, CN::kLuckyPenny);
  state.ApplyAction(bang::PlayCardAction(penny, bang::Target::kOwnRearguard));
  state.ApplyAction(bang::kSwapAction);
  EndCurrentTurn(state);

  const int outlaw_active = state.Players()[1].active_position;
  const int outlaw_life =
      state.Players()[1].positions[static_cast<std::size_t>(outlaw_active)].life;
  state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kColt)));
  Require(HasAction(state, bang::AvoidHitAction(penny)), "Lucky Penny may avoid as a Resource in play");
  const auto outlaw_hand = state.Players()[1].hand.size();
  state.ApplyAction(bang::AvoidHitAction(penny));
  Require(state.Players()[1].positions[static_cast<std::size_t>(outlaw_active)].life == outlaw_life &&
              state.Players()[1].hand.size() == outlaw_hand + 1 &&
              state.DiscardPile().back() == penny,
          "Lucky Penny avoids the hit, draws 1 card, and is discarded");

  // Renegade characters are only dealt in Reinforcements.
  auto base = DeterministicConfig();
  base.character_rosters[0] = {1, static_cast<int>(bang::CharacterName::kTheMarshal)};
  bool rejected = false;
  try {
    bang::GameState invalid(base);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  Require(rejected, "a base game cannot be configured with Renegade characters");
}

void TestReinforcementsRandomPlay() {
  // Whole Reinforcements games of random legal moves: every game must end,
  // every move must be legal, and no card may be lost or duplicated, while
  // Renegade cards and Resources pass through hands and play.
  int finished = 0;
  int resources_used = 0;
  for (std::uint64_t seed = 1; seed <= 60; ++seed) {
    bang::GameConfig config;
    config.seed = seed;
    config.mode = bang::GameMode::kReinforcements;
    config.characters_per_side = 4;
    bang::GameState state(config);
    bang::FastRng rng(seed * 7919U);
    for (int step = 0; step < 5000 && !state.IsTerminal(); ++step) {
      const auto legal = state.LegalActions();
      Require(!legal.empty(), "a Reinforcements game always has a legal move");
      const int action = legal[rng.USize(legal.size())];
      if (bang::DecodeAction(action).kind == bang::ActionKind::kUseResource) ++resources_used;
      state.ApplyAction(action);
      RequireCardConservation(state);
    }
    if (state.IsTerminal()) ++finished;
  }
  Require(finished >= 55, "random Reinforcements games run to completion");
  Require(resources_used > 0, "random play exercises the Resource moves");
}

bool InHand(const bang::GameState& state, int player, int instance) {
  const auto& hand = state.Players()[static_cast<std::size_t>(player)].hand;
  return std::find(hand.begin(), hand.end(), instance) != hand.end();
}

void TestRenegadeBangAndAvoidCards() {
  using CN = bang::CardName;
  constexpr auto kMode = bang::GameMode::kReinforcements;
  {
    // The Outlaw attacks: Lightning, Savage, and Spencer against Feint and Agility.
    auto config = DeterministicConfig();
    config.mode = kMode;
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {CN::kMissed, CN::kMissed, CN::kFeint, CN::kAgility, CN::kMissed, CN::kMissed}, kMode);
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {CN::kLightning, CN::kSavage, CN::kSpencer, CN::kSpencer, CN::kMissed}, kMode);
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);  // the Law keeps Feint, Agility, and two Missed!

    const int law_active = state.Players()[0].active_position;
    const auto law_life = [&] {
      return state.Players()[0].positions[static_cast<std::size_t>(law_active)].life;
    };
    const int lightning = HandCard(state, 1, CN::kLightning);
    state.ApplyAction(bang::PlayCardAction(lightning));
    const int feint = HandCard(state, 0, CN::kFeint);
    Require(HasAction(state, bang::AvoidHitAction(feint)), "Feint avoids like Missed!");
    state.ApplyAction(bang::AvoidHitAction(feint));
    Require(InHand(state, 0, lightning) && !InHand(state, 1, lightning),
            "Feint takes the BANG! card it avoided");

    const int savage = HandCard(state, 1, CN::kSavage);
    const auto outlaw_hand = state.Players()[1].hand.size();
    state.ApplyAction(bang::PlayCardAction(savage));
    Require(state.Players()[1].hand.size() == outlaw_hand,
            "Savage draws its card before the opponent reacts");
    const int agility = HandCard(state, 0, CN::kAgility);
    const auto law_hand = state.Players()[0].hand.size();
    const int life_before = law_life();
    state.ApplyAction(bang::AvoidHitAction(agility));
    Require(state.Players()[0].hand.size() == law_hand && law_life() == life_before,
            "Agility avoids the hit and draws 1 card");
    Require(!HasAction(state, bang::PlayCardAction(HandCard(state, 1, CN::kSpencer))),
            "Lightning allows exactly one more BANG! card");
    EndCurrentTurnKeeping(state, {CN::kSpencer});
    EndCurrentTurn(state);

    Require(law_life() == 4, "the Law's active is unhurt");
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, CN::kSpencer)));
    state.ApplyAction(bang::kTakeHitAction);
    Require(law_life() == 2, "Spencer deals 2 to a character with 4 or more life");
    EndCurrentTurnKeeping(state, {CN::kSpencer});
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, CN::kSpencer)));
    state.ApplyAction(bang::kTakeHitAction);
    Require(law_life() == 1, "Spencer deals 1 to a character below 4 life");
  }
  {
    // The Law attacks: Walker and Whitney against Feint and a Colt.
    auto config = DeterministicConfig();
    config.mode = kMode;
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {CN::kWalker, CN::kWhitney, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed}, kMode);
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw, {CN::kFeint, CN::kColt, CN::kMissed, CN::kMissed, CN::kMissed}, kMode);
    bang::GameState state(config);
    CompleteSetup(state);
    const int walker = HandCard(state, 0, CN::kWalker);
    state.ApplyAction(bang::PlayCardAction(walker));
    state.ApplyAction(bang::AvoidHitAction(HandCard(state, 1, CN::kFeint)));
    Require(InHand(state, 0, walker) && !InHand(state, 1, walker),
            "an avoided Walker returns to its owner, even against Feint");

    const int outlaw_active = state.Players()[1].active_position;
    const int outlaw_life =
        state.Players()[1].positions[static_cast<std::size_t>(outlaw_active)].life;
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kWhitney)));
    const int colt = HandCard(state, 1, CN::kColt);
    Require(!HasAction(state, bang::AvoidHitAction(HandCard(state, 1, CN::kMissed))) &&
                HasAction(state, bang::AvoidHitAction(colt)),
            "Whitney can only be avoided with a hit card");
    state.ApplyAction(bang::AvoidHitAction(colt));
    Require(state.Players()[1].positions[static_cast<std::size_t>(outlaw_active)].life ==
                outlaw_life,
            "a hit card avoids Whitney");
  }
}

void TestRenegadeActionCards() {
  using CN = bang::CardName;
  constexpr auto kMode = bang::GameMode::kReinforcements;
  const auto make = [&](std::vector<CN> law, std::vector<CN> outlaw) {
    auto config = DeterministicConfig();
    config.mode = kMode;
    config.starting_decks[0] = DeckWithTop(bang::Side::kLaw, law, kMode);
    config.starting_decks[1] = DeckWithTop(bang::Side::kOutlaw, outlaw, kMode);
    bang::GameState state(config);
    CompleteSetup(state);
    return state;
  };
  {
    // Switch: up to two sides, each once, in the order chosen.
    auto state = make({CN::kSwitch, CN::kSwitch, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed},
                      {CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed});
    const int law_active = state.Players()[0].active_position;
    const int outlaw_active = state.Players()[1].active_position;
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kSwitch), bang::Target::kOwnActive));
    Require(state.CurrentPhase() == bang::Phase::kChooseSwitch &&
                HasAction(state, bang::kChooseOwnSwapAction) &&
                HasAction(state, bang::kChooseOpponentSwapAction),
            "Switch may start with either side");
    state.ApplyAction(bang::kChooseOpponentSwapAction);
    Require(state.Players()[1].active_position != outlaw_active &&
                HasAction(state, bang::kAcceptOptionalAction) &&
                !HasAction(state, bang::kChooseOpponentSwapAction),
            "the first Switch swap happens, then the other side is optional");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(state.Players()[0].active_position != law_active &&
                state.CurrentPhase() == bang::Phase::kPlay,
            "Switch swaps both sides when chosen");
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kSwitch), bang::Target::kOwnActive));
    state.ApplyAction(bang::kChooseOwnSwapAction);
    state.ApplyAction(bang::kSkipOptionalAction);
    Require(state.Players()[0].active_position == law_active &&
                state.Players()[1].active_position != outlaw_active &&
                HasAction(state, bang::kSwapAction),
            "a single Switch swap is allowed and does not use the voluntary swap");
  }
  {
    // Union Pacific: 2 cards in total, from either deck.
    auto state = make({CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed},
                      {CN::kUnionPacific, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed});
    EndCurrentTurn(state);
    const auto law_deck = state.Players()[0].deck.size();
    const auto outlaw_deck = state.Players()[1].deck.size();
    const auto outlaw_hand = state.Players()[1].hand.size();
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, CN::kUnionPacific),
                                           bang::Target::kOwnActive));
    Require(state.CurrentPhase() == bang::Phase::kChooseUnionPacific, "Union Pacific asks for decks");
    state.ApplyAction(bang::kChooseOwnSwapAction);
    state.ApplyAction(bang::kChooseOpponentSwapAction);
    Require(state.Players()[1].hand.size() == outlaw_hand + 1 &&
                state.Players()[1].deck.size() == outlaw_deck - 1 &&
                state.Players()[0].deck.size() == law_deck - 1 &&
                state.CurrentPhase() == bang::Phase::kPlay,
            "Union Pacific draws one card from each chosen deck");
  }
  {
    // Ambush: 1 card from your own AC, then 1 from any AC.
    auto state = make({CN::kHat, CN::kBarrel, CN::kAmbush, CN::kMissed, CN::kMissed, CN::kMissed},
                      {CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed});
    const int hat = HandCard(state, 0, CN::kHat);
    const int barrel = HandCard(state, 0, CN::kBarrel);
    state.ApplyAction(bang::PlayCardAction(hat, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::PlayCardAction(barrel, bang::Target::kOpponentActive));
    state.ApplyAction(bang::kSwapAction);
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kAmbush), bang::Target::kOwnActive));
    Require(HasAction(state, bang::SelectCardAction(hat)) &&
                !HasAction(state, bang::SelectCardAction(barrel)),
            "Ambush first picks from your own active character");
    state.ApplyAction(bang::SelectCardAction(hat));
    Require(HasAction(state, bang::SelectCardAction(barrel)), "then from any active character");
    state.ApplyAction(bang::SelectCardAction(barrel));
    const auto& discard = state.DiscardPile();
    Require(std::find(discard.begin(), discard.end(), hat) != discard.end() &&
                std::find(discard.begin(), discard.end(), barrel) != discard.end() &&
                state.CurrentPhase() == bang::Phase::kPlay,
            "Ambush discards both picks from play");
  }
  for (const bool horseshoe : {true, false}) {
    // Whiskey: reveal; a Horseshoe heals the active 2, anything else 1.
    auto state = make({CN::kColt, CN::kWinchester, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed},
                      {CN::kWhiskey, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed,
                       CN::kMissed, CN::kMissed, horseshoe ? CN::kClock : CN::kBeer});
    const int outlaw_active = state.Players()[1].active_position;
    const auto outlaw_life = [&] {
      return state.Players()[1].positions[static_cast<std::size_t>(outlaw_active)].life;
    };
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kColt)));
    state.ApplyAction(bang::kTakeHitAction);
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kWinchester)));
    state.ApplyAction(bang::kTakeHitAction);
    Require(outlaw_life() == 2, "the Outlaw's active is down to 2 life");
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, CN::kWhiskey), bang::Target::kOwnActive));
    Require(outlaw_life() == (horseshoe ? 4 : 3) && state.CurrentPhase() == bang::Phase::kPlay,
            "Whiskey heals 2 on a Horseshoe, otherwise 1");
  }
}

void TestSpursSidekickAndSharps() {
  using CN = bang::CardName;
  constexpr auto kMode = bang::GameMode::kReinforcements;
  const auto make = [&](std::vector<CN> law, std::vector<CN> outlaw) {
    auto config = DeterministicConfig();
    config.mode = kMode;
    config.starting_decks[0] = DeckWithTop(bang::Side::kLaw, law, kMode);
    config.starting_decks[1] = DeckWithTop(bang::Side::kOutlaw, outlaw, kMode);
    bang::GameState state(config);
    CompleteSetup(state);
    return state;
  };
  {
    // The Law empties its hand behind Spurs.
    auto state = make({CN::kSpurs, CN::kHat, CN::kBarrel, CN::kScope, CN::kGunBelt, CN::kBank},
                      {CN::kColt, CN::kSharps, CN::kSidekick, CN::kMissed, CN::kMissed});
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kSpurs), bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    for (const CN name : {CN::kHat, CN::kBarrel, CN::kScope, CN::kGunBelt, CN::kBank}) {
      state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, name), bang::Target::kOpponentActive));
    }
    Require(state.Players()[0].hand.empty(), "the Law holds no cards");
    EndCurrentTurn(state);
    // The Outlaw drew 2: the Law's hand is still empty on the Outlaw's turn.
    const int law_active = state.Players()[0].active_position;
    const auto law_life = [&] {
      return state.Players()[0].positions[static_cast<std::size_t>(law_active)].life;
    };
    Require(!HasAction(state, bang::PlayCardAction(HandCard(state, 1, CN::kColt))),
            "Spurs: a character whose owner holds no cards cannot be shot");

    // Sidekick's automatic hit is blocked by Spurs too.
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, CN::kSidekick),
                                           bang::Target::kOwnRearguard));
    const int life_before = law_life();
    state.ApplyAction(bang::kSwapAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay && law_life() == life_before,
            "Spurs stops Sidekick's automatic hit");

    const int sharps = HandCard(state, 1, CN::kSharps);
    Require(HasAction(state, bang::PlayCardAction(sharps)), "Sharps ignores Spurs");
    state.ApplyAction(bang::PlayCardAction(sharps));
    state.ApplyAction(bang::kTakeHitAction);
    Require(law_life() == life_before - 1, "Sharps hits through Spurs");
  }
  {
    // Sidekick: becoming active fires a hit any hand card avoids. Sharps:
    // the target's Hat cannot help.
    auto state = make({CN::kHat, CN::kMissed, CN::kWater, CN::kMissed, CN::kMissed, CN::kMissed},
                      {CN::kSidekick, CN::kSharps, CN::kMissed, CN::kMissed, CN::kMissed});
    const int hat = HandCard(state, 0, CN::kHat);
    state.ApplyAction(bang::PlayCardAction(hat, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, CN::kSidekick),
                                           bang::Target::kOwnRearguard));
    Require(state.CurrentPhase() == bang::Phase::kPlay,
            "placing Sidekick does not fire it; its character is not becoming active");
    state.ApplyAction(bang::kSwapAction);
    const int water = HandCard(state, 0, CN::kWater);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit && state.CurrentPlayer() == 0 &&
                HasAction(state, bang::AvoidHitAction(water)),
            "Sidekick fires when its character becomes active; any hand card avoids it");
    state.ApplyAction(bang::AvoidHitAction(water));
    Require(state.CurrentPhase() == bang::Phase::kPlay && state.CurrentPlayer() == 1,
            "play resumes after Sidekick's hit");

    state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, CN::kSharps)));
    Require(!HasAction(state, bang::AvoidHitAction(hat)) &&
                HasAction(state, bang::AvoidHitAction(HandCard(state, 0, CN::kMissed))),
            "Sharps: the target's Equipment cannot avoid it, but Missed! can");
  }
}

void TestRicochetAndHitResources() {
  using CN = bang::CardName;
  constexpr auto kMode = bang::GameMode::kReinforcements;
  const auto make = [&](std::vector<CN> law, std::vector<CN> outlaw) {
    auto config = DeterministicConfig();
    config.mode = kMode;
    config.starting_decks[0] = DeckWithTop(bang::Side::kLaw, law, kMode);
    config.starting_decks[1] = DeckWithTop(bang::Side::kOutlaw, outlaw, kMode);
    bang::GameState state(config);
    CompleteSetup(state);
    return state;
  };
  const auto life = [](const bang::GameState& state, int player, bool active) {
    const auto& side = state.Players()[static_cast<std::size_t>(player)];
    const int position = active ? side.active_position : 1 - side.active_position;
    return side.positions[static_cast<std::size_t>(position)].life;
  };
  {
    // Ricochet: avoid cards back and forth, Hat included; the first to stop loses 1.
    auto state = make({CN::kHat, CN::kRicochet, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed},
                      {CN::kMissed, CN::kColt, CN::kMissed, CN::kMissed, CN::kMissed});
    const int hat = HandCard(state, 0, CN::kHat);
    state.ApplyAction(bang::PlayCardAction(hat, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kRicochet), bang::Target::kOwnActive));
    Require(state.CurrentPhase() == bang::Phase::kDuelResponse && state.CurrentPlayer() == 1 &&
                state.RicochetActive() &&
                !HasAction(state, bang::DiscardCardAction(HandCard(state, 1, CN::kColt))),
            "Ricochet starts with the opponent and takes avoid cards, not hit cards");
    state.ApplyAction(bang::DiscardCardAction(HandCard(state, 1, CN::kMissed)));
    Require(HasAction(state, bang::DiscardCardAction(hat)), "Hat in play answers a Ricochet");
    state.ApplyAction(bang::DiscardCardAction(hat));
    const int outlaw_life = life(state, 1, true);
    state.ApplyAction(bang::kSkipOptionalAction);
    Require(life(state, 1, true) == outlaw_life - 1 && !state.RicochetActive() &&
                state.CurrentPhase() == bang::Phase::kPlay && state.CurrentPlayer() == 0,
            "the active character of the first player who stops loses 1 life");
  }
  {
    // Double Barrel: avoided, it targets the other character too.
    auto state = make({CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed},
                      {CN::kDoubleBarrel, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed});
    EndCurrentTurn(state);
    const int barrel = HandCard(state, 1, CN::kDoubleBarrel);
    state.ApplyAction(bang::PlayCardAction(barrel, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    EndCurrentTurn(state);
    EndCurrentTurn(state);
    const int law_active = life(state, 0, true);
    const int law_rear = life(state, 0, false);
    state.ApplyAction(bang::UseResourceAction(barrel, bang::Target::kOpponentActive));
    state.ApplyAction(bang::AvoidHitAction(HandCard(state, 0, CN::kMissed)));
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit && state.CurrentPlayer() == 0,
            "an avoided Double Barrel turns on the other character");
    state.ApplyAction(bang::kTakeHitAction);
    Require(life(state, 0, true) == law_active && life(state, 0, false) == law_rear - 1,
            "the rearguard takes Double Barrel's second hit");
  }
  {
    // Howitzer: not avoided, the active and the rearguard each lose 1.
    auto state = make({CN::kHowitzer, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed},
                      {CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed});
    const int howitzer = HandCard(state, 0, CN::kHowitzer);
    state.ApplyAction(bang::PlayCardAction(howitzer, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    EndCurrentTurn(state);
    EndCurrentTurn(state);
    const int active = life(state, 1, true);
    const int rear = life(state, 1, false);
    state.ApplyAction(bang::UseResourceAction(howitzer, bang::Target::kOpponentActive));
    state.ApplyAction(bang::kTakeHitAction);
    Require(life(state, 1, true) == active - 1 && life(state, 1, false) == rear - 1 &&
                state.CurrentPhase() == bang::Phase::kPlay,
            "Howitzer hits both opposing characters when it is not avoided");
  }
  {
    // Indian Chief: each opposing character discards a hit card or loses 1;
    // a hit Resource on the active may be thrown away instead.
    auto state = make({CN::kBuffaloRifle, CN::kColt, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed},
                      {CN::kIndianChief, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed});
    const int rifle = HandCard(state, 0, CN::kBuffaloRifle);
    state.ApplyAction(bang::PlayCardAction(rifle, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    EndCurrentTurnKeeping(state, {CN::kColt});
    const int chief = HandCard(state, 1, CN::kIndianChief);
    state.ApplyAction(bang::PlayCardAction(chief, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    EndCurrentTurn(state);
    EndCurrentTurnKeeping(state, {CN::kColt});
    const int active = life(state, 0, true);
    const int rear = life(state, 0, false);
    state.ApplyAction(bang::UseResourceAction(chief, bang::Target::kOwnActive));
    Require(HasAction(state, bang::AvoidHitAction(rifle)) &&
                HasAction(state, bang::AvoidHitAction(HandCard(state, 0, CN::kColt))),
            "a hit Resource in play answers Indian Chief like a hit card");
    state.ApplyAction(bang::AvoidHitAction(rifle));
    state.ApplyAction(bang::kTakeHitAction);
    Require(life(state, 0, true) == active && life(state, 0, false) == rear - 1,
            "Indian Chief costs each character a hit card or 1 life");
  }
  {
    // Spurs does not stop Indians!.
    auto state = make({CN::kSpurs, CN::kHat, CN::kBarrel, CN::kScope, CN::kGunBelt, CN::kBank},
                      {CN::kIndians, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed});
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kSpurs), bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    for (const CN name : {CN::kHat, CN::kBarrel, CN::kScope, CN::kGunBelt, CN::kBank}) {
      state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, name), bang::Target::kOpponentActive));
    }
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, CN::kIndians), bang::Target::kOwnActive));
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit && state.CurrentPlayer() == 0,
            "Indians! still reaches a character Spurs protects");
  }
}

void TestSlap() {
  using CN = bang::CardName;
  constexpr auto kMode = bang::GameMode::kReinforcements;
  auto config = DeterministicConfig();
  config.mode = kMode;
  // Wild Bill leads the Law: each of his cards Slap removes hits back.
  config.character_rosters[0] = {static_cast<int>(bang::CharacterName::kWildBill),
                                 static_cast<int>(bang::CharacterName::kTheStranger)};
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed}, kMode);
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw, {CN::kSlap, CN::kHat, CN::kMissed, CN::kMissed, CN::kMissed}, kMode);
  bang::GameState state(config);
  CompleteSetup(state);
  EndCurrentTurn(state);
  const int slap = HandCard(state, 1, CN::kSlap);
  const int hat = HandCard(state, 1, CN::kHat);
  state.ApplyAction(bang::PlayCardAction(slap, bang::Target::kOwnRearguard));
  state.ApplyAction(bang::kSwapAction);
  state.ApplyAction(bang::PlayCardAction(hat, bang::Target::kOpponentActive));
  EndCurrentTurn(state);
  EndCurrentTurn(state);

  const auto law_hand = state.Players()[0].hand.size();
  const int outlaw_active = state.Players()[1].active_position;
  const int outlaw_life =
      state.Players()[1].positions[static_cast<std::size_t>(outlaw_active)].life;
  state.ApplyAction(bang::UseResourceAction(slap, bang::Target::kOwnActive));
  Require(state.CurrentPhase() == bang::Phase::kChooseSlap &&
              HasAction(state, bang::SelectCardAction(hat)),
          "Slap first takes a card in play from any active character");
  state.ApplyAction(bang::SelectCardAction(hat));
  Require(HasAction(state, bang::kChooseLawBackAction) &&
              HasAction(state, bang::DiscardCardAction(HandCard(state, 1, CN::kMissed))),
          "then a hand card: the opponent's by card back, or your own");
  state.ApplyAction(bang::kChooseLawBackAction);
  Require(state.Players()[0].hand.size() == law_hand - 1 &&
              std::find(state.DiscardPile().begin(), state.DiscardPile().end(), hat) !=
                  state.DiscardPile().end(),
          "Slap discards the Hat and a random Law hand card");
  Require(state.CurrentPhase() == bang::Phase::kRespondToHit && state.CurrentPlayer() == 1,
          "Wild Bill hits back for the card taken from him");
  state.ApplyAction(bang::kTakeHitAction);
  Require(state.CurrentPhase() == bang::Phase::kRespondToHit && state.CurrentPlayer() == 1,
          "and again for the hand card");
  state.ApplyAction(bang::kTakeHitAction);
  Require(state.Players()[1].positions[static_cast<std::size_t>(outlaw_active)].life ==
                  outlaw_life - 2 &&
              state.CurrentPhase() == bang::Phase::kPlay,
          "both Wild Bill hits land on the slapper's active character");
}

void TestRenegadeCharactersResources() {
  using CN = bang::CardName;
  using CH = bang::CharacterName;
  constexpr auto kMode = bang::GameMode::kReinforcements;
  const auto id = [](CH name) { return static_cast<int>(name); };
  const auto make = [&](std::vector<int> law_roster, std::vector<int> outlaw_roster,
                        std::vector<CN> law, std::vector<CN> outlaw) {
    auto config = DeterministicConfig();
    config.mode = kMode;
    config.character_rosters[0] = law_roster;
    config.character_rosters[1] = outlaw_roster;
    config.starting_decks[0] = DeckWithTop(bang::Side::kLaw, law, kMode);
    config.starting_decks[1] = DeckWithTop(bang::Side::kOutlaw, outlaw, kMode);
    bang::GameState state(config);
    CompleteSetup(state);
    return state;
  };
  const std::vector<CN> fillers{CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed};
  {
    // Big Steve: a 6-card hand limit whatever his life.
    auto state = make({1, 9}, {id(CH::kBigSteve), 16}, {CN::kMissed, CN::kMissed, CN::kMissed,
                                                        CN::kMissed, CN::kMissed, CN::kMissed},
                      fillers);
    EndCurrentTurn(state);
    EndCurrentTurn(state);  // 7 cards; at 5 life the usual limit would be 5
    Require(state.Players()[1].hand.size() == 6,
            "Big Steve keeps 6 cards at the end of his turn");
  }
  {
    // Charlie Basset: once per turn, using a Resource draws 1 more card.
    auto state = make({id(CH::kCharlieBasset), 9}, {12, 16},
                      {CN::kGoldPan, CN::kBuffaloRifle, CN::kColt, CN::kMissed, CN::kMissed, CN::kMissed},
                      fillers);
    const int gold_pan = HandCard(state, 0, CN::kGoldPan);
    const int rifle = HandCard(state, 0, CN::kBuffaloRifle);
    state.ApplyAction(bang::PlayCardAction(gold_pan, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::PlayCardAction(rifle, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);  // Charlie is always active, even as rearguard
    EndCurrentTurnKeeping(state, {CN::kColt});
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kColt)));
    state.ApplyAction(bang::kTakeHitAction);
    const auto before = state.Players()[0].hand.size();
    state.ApplyAction(bang::UseResourceAction(rifle, bang::Target::kOpponentActive));
    Require(state.Players()[0].hand.size() == before + 2,
            "Buffalo Rifle draws 1 and Charlie Basset 1 more");
    state.ApplyAction(bang::kTakeHitAction);
    const auto after_rifle = state.Players()[0].hand.size();
    state.ApplyAction(bang::UseResourceAction(gold_pan, bang::Target::kOwnActive));
    Require(state.Players()[0].hand.size() == after_rifle + 1,
            "Charlie Basset draws only once per turn");
  }
  {
    // Hottie Delou: one of her Resources the turn it entered play, once per turn.
    auto state = make({9, id(CH::kHottieDelou)}, {12, 16},
                      {CN::kBuffaloRifle, CN::kGoldPan, CN::kColt, CN::kMissed, CN::kMissed, CN::kMissed},
                      fillers);
    const int rifle = HandCard(state, 0, CN::kBuffaloRifle);
    const int gold_pan = HandCard(state, 0, CN::kGoldPan);
    state.ApplyAction(bang::PlayCardAction(rifle, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::PlayCardAction(gold_pan, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kColt)));
    state.ApplyAction(bang::kTakeHitAction);
    Require(HasAction(state, bang::UseResourceAction(rifle, bang::Target::kOpponentActive)) &&
                HasAction(state, bang::UseResourceAction(gold_pan, bang::Target::kOwnActive)),
            "Hottie Delou's Resources work the turn they enter play");
    state.ApplyAction(bang::UseResourceAction(gold_pan, bang::Target::kOwnActive));
    Require(!HasAction(state, bang::UseResourceAction(rifle, bang::Target::kOpponentActive)),
            "but only one of them per turn");
  }
  {
    // King Fishers: each character's first Resource of the turn draws him 1.
    auto state = make({1, 9}, {id(CH::kKingFishers), 16},
                      {CN::kBuffaloRifle, CN::kBuffaloRifle, CN::kGoldPan, CN::kMissed, CN::kMissed, CN::kMissed},
                      fillers);
    const auto outlaw_hand = [&] { return state.Players()[1].hand.size(); };
    const auto start = outlaw_hand();
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kBuffaloRifle),
                                           bang::Target::kOwnRearguard));
    Require(outlaw_hand() == start + 1, "a character's first Resource draws King Fishers 1");
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kBuffaloRifle),
                                           bang::Target::kOwnRearguard));
    Require(outlaw_hand() == start + 1, "a second Resource on that character does not");
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kGoldPan),
                                           bang::Target::kOpponentActive));
    Require(outlaw_hand() == start + 2, "another character's first Resource does");
  }
}

void TestTheMarshalAndPokerAlicia() {
  using CN = bang::CardName;
  using CH = bang::CharacterName;
  constexpr auto kMode = bang::GameMode::kReinforcements;
  const auto id = [](CH name) { return static_cast<int>(name); };
  const std::vector<CN> fillers{CN::kMissed, CN::kMissed, CN::kMissed,
                                CN::kMissed, CN::kMissed, CN::kMissed};
  {
    // The Marshal in the starting pair: 3 cards, each from a chosen deck,
    // on top of the starting hand and before the first turn.
    auto config = DeterministicConfig();
    config.mode = kMode;
    config.character_rosters[0] = {id(CH::kTheMarshal), 9};
    config.starting_decks[0] = DeckWithTop(bang::Side::kLaw, fillers, kMode);
    config.starting_decks[1] = DeckWithTop(bang::Side::kOutlaw, fillers, kMode);
    bang::GameState state(config);
    const auto outlaw_deck = state.Players()[1].deck.size();
    CompleteSetup(state);
    Require(state.CurrentPhase() == bang::Phase::kChooseUnionPacific &&
                state.CurrentPlayer() == 0 && state.Players()[0].hand.size() == 4,
            "The Marshal draws before the first turn");
    state.ApplyAction(bang::kChooseOwnSwapAction);
    state.ApplyAction(bang::kChooseOpponentSwapAction);
    state.ApplyAction(bang::kChooseOwnSwapAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.Players()[0].hand.size() == 4 + 3 + 2 &&
                state.Players()[1].deck.size() == outlaw_deck - 1,
            "3 Marshal cards, then the Law's usual turn draw");
  }
  {
    // The Marshal from the reserve: his cards come before the elimination ends.
    bang::GameConfig config;
    config.seed = 7;
    config.mode = kMode;
    config.characters_per_side = 3;
    config.shuffle_decks = false;
    config.shuffle_characters = false;
    config.character_rosters[0] = {id(CH::kJango), 9, id(CH::kTheMarshal)};
    config.character_rosters[1] = {12, 16, 14};
    config.starting_decks[0] = DeckWithTop(bang::Side::kLaw, fillers, kMode);
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw, {CN::kColt, CN::kColt, CN::kColt, CN::kColt, CN::kColt}, kMode);
    bang::GameState state(config);
    CompleteSetup(state);
    for (int shot = 0; shot < 3; ++shot) {
      EndCurrentTurn(state);
      state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, CN::kColt)));
      state.ApplyAction(bang::kTakeHitAction);
      if (shot < 2) EndCurrentTurnKeeping(state, {CN::kColt});
    }
    Require(state.CurrentPhase() == bang::Phase::kChooseUnionPacific && state.CurrentPlayer() == 0,
            "The Marshal draws when he replaces an eliminated character");
    const auto law_hand = state.Players()[0].hand.size();
    state.ApplyAction(bang::kChooseOwnSwapAction);
    state.ApplyAction(bang::kChooseOwnSwapAction);
    state.ApplyAction(bang::kChooseOwnSwapAction);
    const auto& law = state.Players()[0];
    Require(law.hand.size() == law_hand + 3 &&
                law.positions[static_cast<std::size_t>(law.active_position)].character_id ==
                    id(CH::kTheMarshal) &&
                state.CurrentPhase() == bang::Phase::kPlay && state.CurrentPlayer() == 1,
            "the elimination finishes after his draws and the Outlaw plays on");
  }
  {
    // Poker Alicia: instead of drawing, take 1 Equipment card from any AC.
    auto config = DeterministicConfig();
    config.mode = kMode;
    config.character_rosters[0] = {id(CH::kPokerAlicia), 9};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw, {CN::kBarrel, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed},
        kMode);
    config.starting_decks[1] = DeckWithTop(bang::Side::kOutlaw, fillers, kMode);
    bang::GameState state(config);
    CompleteSetup(state);
    const int barrel = HandCard(state, 0, CN::kBarrel);
    state.ApplyAction(bang::PlayCardAction(barrel, bang::Target::kOpponentActive));
    EndCurrentTurn(state);
    EndCurrentTurn(state);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                HasAction(state, bang::SelectCardAction(barrel)) &&
                HasAction(state, bang::kSkipOptionalAction),
            "Poker Alicia may take Equipment from any AC instead of drawing");
    const auto hand = state.Players()[0].hand.size();
    state.ApplyAction(bang::SelectCardAction(barrel));
    Require(InHand(state, 0, barrel) && state.Players()[0].hand.size() == hand + 1 &&
                state.CurrentPhase() == bang::Phase::kPlay,
            "she takes the card instead of the usual 2");
    state.ApplyAction(bang::PlayCardAction(barrel, bang::Target::kOpponentActive));
    EndCurrentTurn(state);
    EndCurrentTurn(state);
    const auto next_hand = state.Players()[0].hand.size();
    state.ApplyAction(bang::kSkipOptionalAction);
    Require(state.Players()[0].hand.size() == next_hand + 2 &&
                state.CurrentPhase() == bang::Phase::kPlay,
            "declining draws the usual 2 cards");
  }
}

void TestJimLonghairAndMysticMaid() {
  using CN = bang::CardName;
  using CH = bang::CharacterName;
  constexpr auto kMode = bang::GameMode::kReinforcements;
  const auto id = [](CH name) { return static_cast<int>(name); };
  const auto make = [&](std::vector<int> law_roster, std::vector<int> outlaw_roster,
                        std::vector<CN> law, std::vector<CN> outlaw) {
    auto config = DeterministicConfig();
    config.mode = kMode;
    config.character_rosters[0] = law_roster;
    config.character_rosters[1] = outlaw_roster;
    config.starting_decks[0] = DeckWithTop(bang::Side::kLaw, law, kMode);
    config.starting_decks[1] = DeckWithTop(bang::Side::kOutlaw, outlaw, kMode);
    bang::GameState state(config);
    CompleteSetup(state);
    return state;
  };
  const std::vector<CN> fillers{CN::kMissed, CN::kMissed, CN::kMissed,
                                CN::kMissed, CN::kMissed, CN::kMissed};
  const auto active_id = [](const bang::GameState& state, int player) {
    const auto& side = state.Players()[static_cast<std::size_t>(player)];
    return side.positions[static_cast<std::size_t>(side.active_position)].character_id;
  };
  {
    // Jim Longhair: when his side swaps, he may swap the opponent's too.
    auto state = make({1, 9}, {id(CH::kJimLonghair), 16}, fillers, fillers);
    EndCurrentTurn(state);
    state.ApplyAction(bang::kSwapAction);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility && state.CurrentPlayer() == 1 &&
                HasAction(state, bang::kAcceptOptionalAction),
            "Jim Longhair offers to swap the opponent");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(active_id(state, 0) == 9 && state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1,
            "the opponent's characters swapped too");
    EndCurrentTurn(state);
    EndCurrentTurn(state);
    state.ApplyAction(bang::kSwapAction);
    state.ApplyAction(bang::kSkipOptionalAction);
    Require(active_id(state, 0) == 9 && active_id(state, 1) == id(CH::kJimLonghair),
            "declining leaves the opponent in place");
  }
  {
    // A Switch swapping Jim's side first: his swap of the Law replaces the
    // Switch's second swap, so no side swaps twice.
    auto state = make({1, 9}, {id(CH::kJimLonghair), 16},
                      {CN::kSwitch, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed},
                      fillers);
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, CN::kSwitch), bang::Target::kOwnActive));
    state.ApplyAction(bang::kChooseOpponentSwapAction);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility && state.CurrentPlayer() == 1,
            "Jim Longhair answers the Switch");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(active_id(state, 0) == 9 && active_id(state, 1) == 16 &&
                state.CurrentPhase() == bang::Phase::kPlay && state.CurrentPlayer() == 0,
            "both sides swapped once and the Law plays on");
  }
  {
    // Mystic Maid: 1 of her Equipment as a Colt (the turn's BANG!), then draw 1.
    auto state = make({1, id(CH::kMysticMaid)}, {12, 16},
                      {CN::kBarrel, CN::kHat, CN::kColt, CN::kMissed, CN::kMissed, CN::kMissed},
                      {CN::kColt, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed, CN::kMissed});
    const int barrel = HandCard(state, 0, CN::kBarrel);
    const int hat = HandCard(state, 0, CN::kHat);
    const int colt = HandCard(state, 0, CN::kColt);
    state.ApplyAction(bang::PlayCardAction(barrel, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::PlayCardAction(hat, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    Require(HasAction(state, bang::PlayAsColtAction(barrel, bang::Target::kOpponentActive)),
            "Mystic Maid may discard her Equipment as a Colt");
    const auto hand = state.Players()[0].hand.size();
    state.ApplyAction(bang::PlayAsColtAction(barrel, bang::Target::kOpponentActive));
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.Players()[0].hand.size() == hand + 1,
            "the Colt comes with 1 card");
    state.ApplyAction(bang::kTakeHitAction);
    Require(!HasAction(state, bang::PlayCardAction(colt, bang::Target::kOpponentActive)),
            "the Colt was the turn's BANG!");
    EndCurrentTurnKeeping(state, {CN::kColt});
    const auto life = state.Players()[0].positions[1].life;
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, CN::kColt)));
    Require(HasAction(state, bang::SelectCardAction(hat)) &&
                HasAction(state, bang::AvoidHitAction(hat)),
            "she may also discard Equipment as a Missed!");
    const auto law_hand = state.Players()[0].hand.size();
    state.ApplyAction(bang::SelectCardAction(hat));
    Require(state.Players()[0].positions[1].life == life &&
                state.Players()[0].hand.size() == law_hand + 1 &&
                state.CurrentPhase() == bang::Phase::kPlay && state.CurrentPlayer() == 1,
            "the hit is avoided and she draws 1");
  }
}

void TestHatAvoidsAndIsDiscarded() {
  auto config = DeterministicConfig();
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {bang::CardName::kHat, bang::CardName::kWater, bang::CardName::kMissed,
       bang::CardName::kWagon, bang::CardName::kDerringer, bang::CardName::kReturnFire});
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kColt, bang::CardName::kBarrel, bang::CardName::kMissed,
       bang::CardName::kClock, bang::CardName::kGunBelt, bang::CardName::kWater,
       bang::CardName::kKnife});
  bang::GameState state(config);
  CompleteSetup(state);
  const int hat = HandCard(state, 0, bang::CardName::kHat);
  state.ApplyAction(bang::PlayCardAction(hat, bang::Target::kOwnRearguard));
  state.ApplyAction(bang::kSwapAction);
  EndCurrentTurn(state);

  const int target_position = state.Players()[0].active_position;
  const int life_before = state.Players()[0].positions[target_position].life;
  const int colt = HandCard(state, 1, bang::CardName::kColt);
  state.ApplyAction(bang::PlayCardAction(colt));
  Require(HasAction(state, bang::AvoidHitAction(hat)), "target character may use Hat");
  state.ApplyAction(bang::AvoidHitAction(hat));
  Require(state.Players()[0].positions[target_position].life == life_before,
          "Hat cancels the hit");
  Require(std::find(state.DiscardPile().begin(), state.DiscardPile().end(), hat) !=
              state.DiscardPile().end(),
          "used Hat is discarded from play");
}

void TestBarrelRevealSuccessAndFailure() {
  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kBarrel, bang::CardName::kMissed, bang::CardName::kWater,
         bang::CardName::kHat, bang::CardName::kDerringer, bang::CardName::kReturnFire,
         bang::CardName::kWagon});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kColt, bang::CardName::kBarrel, bang::CardName::kMissed,
         bang::CardName::kClock, bang::CardName::kGunBelt, bang::CardName::kWater,
         bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    const int barrel = HandCard(state, 0, bang::CardName::kBarrel);
    state.ApplyAction(bang::PlayCardAction(barrel, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    EndCurrentTurn(state);
    const int target = state.Players()[0].active_position;
    const int life = state.Players()[0].positions[target].life;
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, bang::CardName::kColt)));
    state.ApplyAction(bang::AvoidHitAction(barrel));
    Require(state.CurrentPhase() == bang::Phase::kPlay,
            "a Barrel reveal with the Barrel symbol cancels immediately");
    Require(state.Players()[0].positions[target].life == life,
            "successful Barrel prevents life loss");
  }

  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kBarrel, bang::CardName::kWater, bang::CardName::kHat,
         bang::CardName::kDerringer, bang::CardName::kReturnFire, bang::CardName::kMissed,
         bang::CardName::kGeneralStore});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kColt, bang::CardName::kBarrel, bang::CardName::kMissed,
         bang::CardName::kClock, bang::CardName::kGunBelt, bang::CardName::kWater,
         bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    const int barrel = HandCard(state, 0, bang::CardName::kBarrel);
    const int missed = HandCard(state, 0, bang::CardName::kMissed);
    state.ApplyAction(bang::PlayCardAction(barrel, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, bang::CardName::kColt)));
    state.ApplyAction(bang::AvoidHitAction(barrel));
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit,
            "failed Barrel returns to the same hit response");
    Require(!HasAction(state, bang::AvoidHitAction(barrel)),
            "Barrel cannot be retried for the same hit");
    Require(HasAction(state, bang::AvoidHitAction(missed)),
            "a hand avoid remains available after failed Barrel");
  }
}

void TestReturnFireCreatesCounterHit() {
  auto config = DeterministicConfig();
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {bang::CardName::kWater, bang::CardName::kMissed, bang::CardName::kWagon,
       bang::CardName::kDerringer, bang::CardName::kHat, bang::CardName::kReturnFire});
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kColt, bang::CardName::kWater, bang::CardName::kMissed,
       bang::CardName::kWagon, bang::CardName::kKnife});
  bang::GameState state(config);
  CompleteSetup(state);
  EndCurrentTurn(state);
  const int law_position = state.Players()[0].active_position;
  const int outlaw_position = state.Players()[1].active_position;
  const int law_life = state.Players()[0].positions[law_position].life;
  const int outlaw_life = state.Players()[1].positions[outlaw_position].life;
  state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, bang::CardName::kColt)));
  const int return_fire = HandCard(state, 0, bang::CardName::kReturnFire);
  Require(HasAction(state, bang::AvoidHitAction(return_fire)),
          "Return Fire is a legal avoid response");
  state.ApplyAction(bang::AvoidHitAction(return_fire));
  Require(state.CurrentPhase() == bang::Phase::kRespondToHit && state.CurrentPlayer() == 1,
          "Return Fire creates a counter-hit on the original attacker");
  state.ApplyAction(bang::kTakeHitAction);
  Require(state.Players()[1].positions[outlaw_position].life == outlaw_life - 1,
          "unavoided Return Fire costs the attacker one life");
  Require(state.Players()[0].positions[law_position].life == law_life,
          "Return Fire cancels the original hit");
  Require(state.CurrentPhase() == bang::Phase::kPlay && state.CurrentPlayer() == 1,
          "resolution returns control to the original turn player");
}

void TestRemingtonChoicePrecedesReturnFireCounter() {
  auto config = DeterministicConfig();
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {bang::CardName::kWater, bang::CardName::kMissed, bang::CardName::kWagon,
       bang::CardName::kDerringer, bang::CardName::kHat, bang::CardName::kReturnFire});
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kRemington, bang::CardName::kWater, bang::CardName::kMissed,
       bang::CardName::kWagon, bang::CardName::kKnife});
  bang::GameState state(config);
  CompleteSetup(state);
  EndCurrentTurn(state);
  state.ApplyAction(
      bang::PlayCardAction(HandCard(state, 1, bang::CardName::kRemington)));
  state.ApplyAction(
      bang::AvoidHitAction(HandCard(state, 0, bang::CardName::kReturnFire)));
  Require(state.CurrentPhase() == bang::Phase::kChooseRemington && state.CurrentPlayer() == 1,
          "Remington owner resolves its optional draw before Return Fire");
  const auto attacker_observation = state.ObservationTensor(1);
  Require(attacker_observation[bang::obs_layout::kQueuedHitSourceOffset +
                               static_cast<int>(bang::CardName::kReturnFire)] ==
              1.0F &&
              attacker_observation[bang::obs_layout::kQueuedHitTargetObserverOffset] ==
                  1.0F &&
              attacker_observation[
                  bang::obs_layout::kQueuedHitAttackerObserverOffset] == 0.0F,
          "the policy observation exposes the queued counter-hit and relative roles");
  const auto defender_observation = state.ObservationTensor(0);
  Require(defender_observation[bang::obs_layout::kQueuedHitTargetObserverOffset] ==
              0.0F &&
              defender_observation[
                  bang::obs_layout::kQueuedHitAttackerObserverOffset] == 1.0F,
          "queued counter-hit roles are observer-relative");
  state.ApplyAction(bang::kSkipOptionalAction);
  Require(state.CurrentPhase() == bang::Phase::kRespondToHit && state.CurrentPlayer() == 1,
          "queued Return Fire begins after the optional effect resolves");
}

void TestSupportedWeaponEffects() {
  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kWinchester, bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kDerringer, bang::CardName::kHat});
    bang::GameState state(config);
    CompleteSetup(state);
    const int rear = 1 - state.Players()[1].active_position;
    const int life = state.Players()[1].positions[rear].life;
    const int winchester = HandCard(state, 0, bang::CardName::kWinchester);
    Require(HasAction(state,
                      bang::PlayCardAction(winchester, bang::Target::kOpponentRearguard)),
            "Winchester targets RC without Scope");
    state.ApplyAction(
        bang::PlayCardAction(winchester, bang::Target::kOpponentRearguard));
    const auto observation = state.ObservationTensor(1);
    Require(observation[bang::obs_layout::kPendingHitSourceOffset +
                        static_cast<int>(bang::CardName::kWinchester)] == 1.0F,
            "pending hit source is explicit in the policy observation");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.Players()[1].positions[rear].life == life - 1,
            "Winchester hit resolves against the selected RC");
  }

  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kWater, bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kReturnFire, bang::CardName::kMissed});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kRemington, bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    const auto deck_before = state.Players()[1].deck.size();
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 1, bang::CardName::kRemington)));
    state.ApplyAction(bang::AvoidHitAction(HandCard(state, 0, bang::CardName::kMissed)));
    Require(state.CurrentPhase() == bang::Phase::kChooseRemington &&
                state.LegalActions() ==
                    std::vector<int>{bang::kSkipOptionalAction,
                                     bang::kAcceptOptionalAction},
            "Remington draw is an explicit optional decision");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(state.Players()[1].deck.size() + 1 == deck_before,
            "avoided Remington draws a card for its player");
  }

  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kColt, bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kDerringer, bang::CardName::kHat, bang::CardName::kMissed});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kSchofield, bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    const int outlaw_active = state.Players()[1].active_position;
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, bang::CardName::kColt)));
    state.ApplyAction(bang::kTakeHitAction);
    EndCurrentTurn(state);
    const int damaged_life = state.Players()[1].positions[outlaw_active].life;
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 1, bang::CardName::kSchofield)));
    state.ApplyAction(bang::AvoidHitAction(HandCard(state, 0, bang::CardName::kMissed)));
    Require(state.Players()[1].positions[outlaw_active].life == damaged_life + 1,
            "avoided Schofield heals its player's AC before later effects");
  }
}

void TestPeacemakerCarbineAndShotgunChoices() {
  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kPeacemaker, bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kDerringer, bang::CardName::kHat});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kMissed, bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kKnife, bang::CardName::kStealing});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kPeacemaker)));
    const int missed = HandCard(state, 1, bang::CardName::kMissed);
    state.ApplyAction(bang::AvoidHitAction(missed));
    Require(state.CurrentPhase() == bang::Phase::kForcedDiscard &&
                state.CurrentPlayer() == 1,
            "avoided Peacemaker forces the opponent to choose a hand discard");
    const auto hand_before = state.Players()[1].hand.size();
    state.ApplyAction(state.LegalActions().front());
    Require(state.Players()[1].hand.size() + 1 == hand_before,
            "Peacemaker discards exactly one additional card");
    Require(state.CurrentPhase() == bang::Phase::kPlay && state.CurrentPlayer() == 0,
            "Peacemaker continuation returns to its player");
  }

  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kBarrel, bang::CardName::kCarbine, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer, bang::CardName::kHat});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kMissed, bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kKnife, bang::CardName::kStealing});
    bang::GameState state(config);
    CompleteSetup(state);
    const int barrel = HandCard(state, 0, bang::CardName::kBarrel);
    state.ApplyAction(
        bang::PlayCardAction(barrel, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kCarbine)));
    state.ApplyAction(
        bang::AvoidHitAction(HandCard(state, 1, bang::CardName::kMissed)));
    Require(state.CurrentPhase() == bang::Phase::kChooseCarbine &&
                HasAction(state, bang::DiscardCardAction(barrel)) &&
                HasAction(state, bang::kSkipOptionalAction),
            "avoided Carbine offers optional AC Equipment removal");
    state.ApplyAction(bang::DiscardCardAction(barrel));
    Require(std::find(state.DiscardPile().begin(), state.DiscardPile().end(), barrel) !=
                state.DiscardPile().end(),
            "Carbine can discard Equipment from an AC");
  }

  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kShotgun, bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kDerringer, bang::CardName::kHat});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kMissed, bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kKnife, bang::CardName::kStealing});
    bang::GameState state(config);
    CompleteSetup(state);
    const int opponent_active = state.Players()[1].active_position;
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kShotgun)));
    state.ApplyAction(
        bang::AvoidHitAction(HandCard(state, 1, bang::CardName::kMissed)));
    Require(state.CurrentPhase() == bang::Phase::kChooseShotgun &&
                HasAction(state, bang::kChooseOwnSwapAction) &&
                HasAction(state, bang::kChooseOpponentSwapAction),
            "avoided Shotgun requires choosing which side swaps");
    state.ApplyAction(bang::kChooseOpponentSwapAction);
    Require(state.Players()[1].active_position != opponent_active,
            "Shotgun swaps the selected opponent AC and RC");
  }
}

void TestAuditedRuleInteractions() {
  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kDerringer, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kMissed,
         bang::CardName::kHat, bang::CardName::kStrongAle});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kStealing, bang::CardName::kBeer,
         bang::CardName::kMissed});
    bang::GameState state(config);
    CompleteSetup(state);
    const int target = state.Players()[1].active_position;
    const int life = state.Players()[1].positions[target].life;
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kDerringer)));
    const int water = HandCard(state, 1, bang::CardName::kWater);
    Require(HasAction(state, bang::AvoidHitAction(water)),
            "Derringer may be avoided with any card from hand");
    const auto observation = state.ObservationTensor(1);
    Require(observation[bang::obs_layout::kPendingResponseKindOffset + 1] == 1.0F,
            "Derringer's any-hand-card response mode is observable");
    state.ApplyAction(bang::AvoidHitAction(water));
    Require(state.Players()[1].positions[target].life == life,
            "a card discarded against Derringer avoids its hit");
  }

  {
    auto config = DeterministicConfig();
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kKnife, bang::CardName::kColt,
         bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kStealing, bang::CardName::kMissed,
         bang::CardName::kBeer});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    const int target = state.Players()[0].active_position;
    const int life = state.Players()[0].positions[target].life;
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 1, bang::CardName::kKnife)));
    state.ApplyAction(bang::kTakeHitAction);
    const int colt = HandCard(state, 1, bang::CardName::kColt);
    Require(HasAction(state, bang::PlayCardAction(colt)),
            "Knife is a hit but does not consume the BANG card limit");
    state.ApplyAction(bang::PlayCardAction(colt));
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.Players()[0].positions[target].life == life - 2,
            "Knife and a BANG card can deal separate hits in one turn");
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kAlanPinkertoon),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kColt, bang::CardName::kCarbine,
         bang::CardName::kDerringer, bang::CardName::kDerringer,
         bang::CardName::kWater, bang::CardName::kWagon});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kColt)));
    state.ApplyAction(bang::kTakeHitAction);
    Require(!HasAction(
                state,
                bang::PlayCardAction(
                    HandCard(state, 0, bang::CardName::kCarbine))),
            "a second BANG card remains illegal without an exception");
    for (int count = 0; count < 2; ++count) {
      const int derringer =
          HandCard(state, 0, bang::CardName::kDerringer);
      Require(HasAction(state, bang::PlayCardAction(derringer)),
              "successive Derringers remain legal after the BANG limit is used");
      state.ApplyAction(bang::PlayCardAction(derringer));
      state.ApplyAction(bang::kTakeHitAction);
    }
  }

  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kFanning, bang::CardName::kBank,
         bang::CardName::kHat, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer});
    bang::GameState state(config);
    CompleteSetup(state);
    const int target = state.Players()[1].active_position;
    const int life = state.Players()[1].positions[target].life;
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kBank),
                             bang::Target::kOpponentActive));
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kHat),
                             bang::Target::kOpponentActive));
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kFanning)));
    const auto first = state.ObservationTensor(1);
    Require(first[bang::obs_layout::kHitSequenceActiveOffset] == 1.0F &&
                first[bang::obs_layout::kRemainingSequenceHitsOffset] == 1.0F / 16.0F,
            "Fanning fixes one hit per target Equipment when played");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 1,
            "each Fanning hit is resolved separately");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.Players()[1].positions[target].life == life - 2 &&
                state.CurrentPhase() == bang::Phase::kPlay,
            "Fanning resolves its complete fixed hit sequence");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kWyattEar),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kDerringer, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kMissed,
         bang::CardName::kHat, bang::CardName::kStrongAle});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kStealing, bang::CardName::kBeer,
         bang::CardName::kMissed});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kDerringer),
        bang::Target::kOpponentActive));
    state.ApplyAction(bang::AvoidHitAction(
        HandCard(state, 1, bang::CardName::kWater)));
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.CurrentPlayer() == 0,
            "Wyatt reacts when a non-BANG hit card is avoided");
    state.ApplyAction(bang::kSkipOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 0,
            "Wyatt's action-hit reaction resumes normal play");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kTexKiller),
        static_cast<int>(bang::CharacterName::kAlanPinkertoon)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kGeneralStore, bang::CardName::kColt,
         bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kDerringer, bang::CardName::kMissed,
         bang::CardName::kReturnFire, bang::CardName::kHat});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kStealing, bang::CardName::kBeer,
         bang::CardName::kMissed, bang::CardName::kKnife,
         bang::CardName::kClock});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kGeneralStore),
        bang::Target::kOwnActive));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kHat));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kReturnFire));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kKnife));
    state.ApplyAction(state.LegalActions().front());
    const int law_position = state.Players()[0].active_position;
    const int law_life = state.Players()[0].positions[law_position].life;
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kColt)));
    state.ApplyAction(bang::AvoidHitAction(
        HandCard(state, 1, bang::CardName::kMissed)));
    state.ApplyAction(bang::AvoidHitAction(
        HandCard(state, 1, bang::CardName::kReturnFire)));
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 0 &&
                state.Players()[0].positions[law_position].life == law_life,
            "Return Fire used only as Tex's additional discard has no counter "
            "effect");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kAnnieOakey),
        static_cast<int>(bang::CharacterName::kBuffaloBell)};
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kKnife, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kStealing,
         bang::CardName::kBeer});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    const int target = state.Players()[0].active_position;
    const int buffalo = 1 - target;
    const int target_life = state.Players()[0].positions[target].life;
    const int buffalo_life = state.Players()[0].positions[buffalo].life;
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kKnife),
        bang::Target::kOpponentActive));
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                HasAction(state, bang::kAcceptOptionalAction),
            "always-active Buffalo may absorb damage while he is the RC");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.Players()[0].positions[target].life == target_life &&
                state.Players()[0].positions[buffalo].life ==
                    buffalo_life - 1,
            "RC Buffalo loses the redirected point instead of the AC");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kWildBill),
        static_cast<int>(bang::CharacterName::kAlanPinkertoon)};
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kBullAnderson),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kGeneralStore, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kMissed, bang::CardName::kCarbine,
         bang::CardName::kHat, bang::CardName::kReturnFire,
         bang::CardName::kColt});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kScope, bang::CardName::kKnife,
         bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kStealing, bang::CardName::kDynamite,
         bang::CardName::kBeer});
    bang::GameState state(config);
    state.ApplyAction(bang::kChooseActiveLeftAction);
    state.ApplyAction(bang::kChooseActiveRightAction);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kGeneralStore),
        bang::Target::kOwnActive));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kDynamite));
    while (state.CurrentPhase() == bang::Phase::kChooseGeneralStore) {
      state.ApplyAction(state.LegalActions().front());
    }
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kDynamite),
        bang::Target::kOwnRearguard));
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kScope),
        bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kKnife),
        bang::Target::kOpponentRearguard));
    state.ApplyAction(bang::kTakeHitAction);
    state.ApplyAction(bang::kTakeHitAction);
    EndCurrentTurn(state);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                HasAction(state, bang::kChooseLawBackAction) &&
                state.ObservationTensor(0)
                        [bang::obs_layout::kDeferredHitCountOffset] ==
                    2.0F / 8.0F,
            "Bull resolves before Alan's deferred lethal-damage counters");
    state.ApplyAction(bang::kChooseLawBackAction);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 1,
            "Bull drawing from Wild Bill suspends the elimination choice");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                HasAction(state, bang::kSkipOptionalAction),
            "Wild Bill's nested hit resumes Bull before deferred counters");
    state.ApplyAction(bang::kSkipOptionalAction);
    for (int counter = 0; counter < 2; ++counter) {
      Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                  state.CurrentPlayer() == 1,
              "Alan's deferred counter starts after elimination cleanup");
      state.ApplyAction(bang::kTakeHitAction);
    }
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 0 &&
                state.ObservationTensor(0)
                        [bang::obs_layout::kEliminationDepthOffset] == 0.0F,
            "lethal-damage continuations leave no stale elimination");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kAnnieOakey),
        static_cast<int>(bang::CharacterName::kAlanPinkertoon)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kGunBelt, bang::CardName::kColt,
         bang::CardName::kCarbine, bang::CardName::kWater,
         bang::CardName::kShotgun, bang::CardName::kWinchester});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kGunBelt),
        bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    for (const auto name : {bang::CardName::kColt,
                            bang::CardName::kCarbine}) {
      state.ApplyAction(
          bang::PlayCardAction(HandCard(state, 0, name)));
      state.ApplyAction(bang::kTakeHitAction);
    }
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kWater),
        bang::Target::kOwnActive));
    const int shotgun = HandCard(state, 0, bang::CardName::kShotgun);
    Require(HasAction(state, bang::PlayCardAction(shotgun)),
            "BANG cards fired with Gun Belt do not consume the new AC's limit");
    state.ApplyAction(bang::PlayCardAction(shotgun));
    state.ApplyAction(bang::kTakeHitAction);
    Require(!HasAction(state, bang::PlayCardAction(
                                  HandCard(state, 0,
                                           bang::CardName::kWinchester))),
            "after swapping away from Gun Belt the new AC gets exactly one "
            "normal BANG");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kWyattEar),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kSidCurry),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kGatling, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kStrongAle,
         bang::CardName::kBank});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kGatling),
        bang::Target::kOwnActive));
    state.ApplyAction(
        bang::UseAbilityAction(bang::CharacterName::kSidCurry));
    state.ApplyAction(bang::kTakeHitAction);
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.CurrentPlayer() == 0,
            "current-player Wyatt resolves before Sid's deferred counter");
    state.ApplyAction(bang::kSkipOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 0,
            "Sid's counter follows Wyatt after the multi-target card");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 0,
            "ordered Wyatt and Sid consequences resume the original turn");
    RequireCardConservation(state);
  }
}

void TestTexKillerRequiresAnAvoidAndAdditionalCard() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kTexKiller),
        static_cast<int>(bang::CharacterName::kAlanPinkertoon)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kColt, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kStrongAle});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kMissed, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kStealing,
         bang::CardName::kBeer});
    bang::GameState state(config);
    CompleteSetup(state);
    const int target = state.Players()[1].active_position;
    const int life = state.Players()[1].positions[target].life;
    const int missed = HandCard(state, 1, bang::CardName::kMissed);
    const int water = HandCard(state, 1, bang::CardName::kWater);
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kColt)));
    const auto initial = state.ObservationTensor(1);
    Require(initial[bang::obs_layout::kPendingAvoidsRequiredOffset] == 2.0F &&
                initial[bang::obs_layout::kPendingAvoidsPlayedOffset] == 0.0F,
            "Tex Killer's BANG exposes its two-card response requirement");
    Require(HasAction(state, bang::AvoidHitAction(missed)) &&
                !HasAction(state, bang::AvoidHitAction(water)),
            "Tex first requires a real avoid rather than an arbitrary card");
    state.ApplyAction(bang::AvoidHitAction(missed));
    const auto after_avoid = state.ObservationTensor(1);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                after_avoid[bang::obs_layout::kPendingAvoidsPlayedOffset] == 1.0F &&
                HasAction(state, bang::AvoidHitAction(water)),
            "after avoiding Tex's hit any hand card pays the additional cost");
    state.ApplyAction(bang::AvoidHitAction(water));
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.Players()[1].positions[target].life == life,
            "both payments completely avoid Tex Killer's BANG");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kTexKiller),
        static_cast<int>(bang::CharacterName::kAlanPinkertoon)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kColt, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kStrongAle});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kMissed, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kStealing,
         bang::CardName::kBeer});
    bang::GameState state(config);
    CompleteSetup(state);
    const int target = state.Players()[1].active_position;
    const int life = state.Players()[1].positions[target].life;
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kColt)));
    state.ApplyAction(
        bang::AvoidHitAction(HandCard(state, 1, bang::CardName::kMissed)));
    Require(HasAction(state, bang::kTakeHitAction),
            "a defender may decline Tex's additional payment");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.Players()[1].positions[target].life == life - 1,
            "declining the additional card loses life despite the spent avoid");
    RequireCardConservation(state);
  }
}

void TestTexKillerBarrelAndReturnFireTiming() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kTexKiller),
        static_cast<int>(bang::CharacterName::kAlanPinkertoon)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kBarrel, bang::CardName::kColt,
         bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kDerringer, bang::CardName::kHat});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kStealing, bang::CardName::kBeer,
         bang::CardName::kKnife, bang::CardName::kWater});
    bang::GameState state(config);
    CompleteSetup(state);
    const int target = state.Players()[1].active_position;
    const int barrel = HandCard(state, 0, bang::CardName::kBarrel);
    state.ApplyAction(
        bang::PlayCardAction(barrel, bang::Target::kOpponentActive));
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kColt)));
    state.ApplyAction(bang::AvoidHitAction(barrel));
    const int additional = HandCard(state, 1, bang::CardName::kMissed);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                HasAction(state, bang::AvoidHitAction(additional)) &&
                CharacterHasEquipment(state, 1, target, bang::CardName::kBarrel),
            "a successful Barrel counts once and remains in play against Tex");
    state.ApplyAction(bang::AvoidHitAction(additional));
    Require(state.CurrentPhase() == bang::Phase::kPlay,
            "a card from hand completes a successful Barrel response to Tex");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kTexKiller),
        static_cast<int>(bang::CharacterName::kAlanPinkertoon)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kGeneralStore, bang::CardName::kColt,
         bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kDerringer, bang::CardName::kMissed,
         bang::CardName::kReturnFire, bang::CardName::kHat});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kStealing, bang::CardName::kBeer,
         bang::CardName::kMissed, bang::CardName::kKnife,
         bang::CardName::kClock});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kGeneralStore),
        bang::Target::kOwnActive));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kHat));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kReturnFire));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kKnife));
    state.ApplyAction(state.LegalActions().front());

    const int law_position = state.Players()[0].active_position;
    const int outlaw_position = state.Players()[1].active_position;
    const int law_life = state.Players()[0].positions[law_position].life;
    const int outlaw_life = state.Players()[1].positions[outlaw_position].life;
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kColt)));
    const int return_fire = HandCard(state, 1, bang::CardName::kReturnFire);
    state.ApplyAction(bang::AvoidHitAction(return_fire));
    const auto pending = state.ObservationTensor(1);
    Require(state.CurrentPlayer() == 1 &&
                pending[bang::obs_layout::kPendingReturnFireOffset] == 1.0F &&
                pending[bang::obs_layout::kPendingAvoidsPlayedOffset] == 1.0F,
            "Return Fire waits for Tex's additional card before countering");
    const int additional = state.Players()[1].hand.front();
    state.ApplyAction(bang::AvoidHitAction(additional));
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 0,
            "Return Fire creates its counter only after Tex's hit is fully avoided");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.Players()[0].positions[law_position].life == law_life - 1 &&
                state.Players()[1].positions[outlaw_position].life == outlaw_life,
            "the delayed Return Fire counter resolves and cancels the original hit");
    RequireCardConservation(state);
  }
}

void TestTexKillerFanningRequiresTwoCardsPerHit() {
  auto config = DeterministicConfig();
  config.character_rosters[0] = {
      static_cast<int>(bang::CharacterName::kTexKiller),
      static_cast<int>(bang::CharacterName::kAlanPinkertoon)};
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {bang::CardName::kFanning, bang::CardName::kBank,
       bang::CardName::kHat, bang::CardName::kWater,
       bang::CardName::kWagon, bang::CardName::kDerringer});
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kMissed, bang::CardName::kMissed,
       bang::CardName::kWater, bang::CardName::kWagon,
       bang::CardName::kStealing});
  bang::GameState state(config);
  CompleteSetup(state);
  state.ApplyAction(bang::PlayCardAction(
      HandCard(state, 0, bang::CardName::kBank),
      bang::Target::kOpponentActive));
  state.ApplyAction(bang::PlayCardAction(
      HandCard(state, 0, bang::CardName::kHat),
      bang::Target::kOpponentActive));
  state.ApplyAction(
      bang::PlayCardAction(HandCard(state, 0, bang::CardName::kFanning)));
  const std::array additional_cards = {bang::CardName::kWater,
                                       bang::CardName::kWagon};
  for (int hit = 0; hit < 2; ++hit) {
    const int missed = HandCard(state, 1, bang::CardName::kMissed);
    state.ApplyAction(bang::AvoidHitAction(missed));
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit,
            "each Tex Fanning hit remains pending after its avoid");
    state.ApplyAction(bang::AvoidHitAction(
        HandCard(state, 1, additional_cards[static_cast<std::size_t>(hit)])));
  }
  Require(state.CurrentPhase() == bang::Phase::kPlay &&
              state.CurrentPlayer() == 0,
          "each Fanning hit independently consumes an avoid and another card");
  RequireCardConservation(state);
}

void TestGatlingOrderAndDeferredElimination() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {6, 0};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kMissed, bang::CardName::kGatling,
         bang::CardName::kHat, bang::CardName::kDerringer,
         bang::CardName::kStrongAle, bang::CardName::kBank});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kKnife, bang::CardName::kColt,
         bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kStealing, bang::CardName::kMissed,
         bang::CardName::kBeer});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 1, bang::CardName::kKnife)));
    state.ApplyAction(bang::kTakeHitAction);
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 1, bang::CardName::kColt)));
    state.ApplyAction(bang::kTakeHitAction);
    EndCurrentTurn(state);
    const int jango_position = state.Players()[0].active_position;
    Require(state.Players()[0].positions[jango_position].life == 1,
            "deferred-elimination scenario starts with Jango at one life");
    state.ApplyAction(bang::kSwapAction);
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kGatling),
                             bang::Target::kOwnActive));
    state.ApplyAction(bang::kTakeHitAction);  // Opponent AC.
    state.ApplyAction(bang::kTakeHitAction);  // Jango RC reaches zero.
    Require(state.Players()[0].positions[jango_position].character_id == 6 &&
                state.Players()[0].positions[jango_position].life == 0,
            "a zero-life character remains in place until Gatling finishes");
    const auto before_last_target = state.ObservationTensor(1);
    Require(before_last_target[bang::obs_layout::kDeferredEliminationOffset] == 1.0F,
            "deferred elimination is explicit in the policy observation");
    const auto law_hand_before_reward = state.Players()[0].hand.size();
    state.ApplyAction(bang::kTakeHitAction);  // Opponent RC; sequence ends.
    Require(state.Players()[0].positions[jango_position].character_id == -1 &&
                state.Players()[0].hand.size() == law_hand_before_reward + 2,
            "elimination cleanup and its two-card reward occur after all targets");
    RequireCardConservation(state);
  }
}

void TestIndiansResponseAndOrder() {
  auto config = DeterministicConfig();
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {bang::CardName::kIndians, bang::CardName::kWater,
       bang::CardName::kWagon, bang::CardName::kMissed,
       bang::CardName::kHat, bang::CardName::kDerringer});
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kKnife, bang::CardName::kMissed,
       bang::CardName::kWater, bang::CardName::kWagon,
       bang::CardName::kStealing});
  bang::GameState state(config);
  CompleteSetup(state);
  const int opponent_active = state.Players()[1].active_position;
  const int own_rear = 1 - state.Players()[0].active_position;
  const int opponent_rear = 1 - opponent_active;
  const int opponent_active_life =
      state.Players()[1].positions[opponent_active].life;
  const int own_rear_life = state.Players()[0].positions[own_rear].life;
  const int opponent_rear_life = state.Players()[1].positions[opponent_rear].life;
  state.ApplyAction(
      bang::PlayCardAction(HandCard(state, 0, bang::CardName::kIndians),
                           bang::Target::kOwnActive));
  const int knife = HandCard(state, 1, bang::CardName::kKnife);
  const int missed = HandCard(state, 1, bang::CardName::kMissed);
  Require(HasAction(state, bang::AvoidHitAction(knife)) &&
              !HasAction(state, bang::AvoidHitAction(missed)),
          "Indians! accepts a hit card but not an avoid card");
  const auto response = state.ObservationTensor(1);
  Require(response[bang::obs_layout::kPendingResponseKindOffset + 2] == 1.0F,
          "Indians!' hit-card response mode is observable");
  state.ApplyAction(bang::AvoidHitAction(knife));
  Require(state.CurrentPlayer() == 0,
          "Indians! proceeds from opponent AC to the turn player's RC");
  state.ApplyAction(bang::kTakeHitAction);
  Require(state.CurrentPlayer() == 1,
          "Indians! proceeds from the turn player's RC to opponent RC");
  state.ApplyAction(bang::kTakeHitAction);
  Require(state.Players()[1].positions[opponent_active].life ==
                  opponent_active_life &&
              state.Players()[0].positions[own_rear].life == own_rear_life - 1 &&
              state.Players()[1].positions[opponent_rear].life ==
                  opponent_rear_life - 1,
          "Indians! resolves each non-AC target in rulebook order");
  RequireCardConservation(state);
}

void TestDuelAlternatingResponse() {
  auto config = DeterministicConfig();
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {bang::CardName::kDuel, bang::CardName::kDerringer,
       bang::CardName::kWater, bang::CardName::kMissed,
       bang::CardName::kWagon, bang::CardName::kHat});
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kKnife, bang::CardName::kMissed,
       bang::CardName::kWater, bang::CardName::kWagon,
       bang::CardName::kStealing});
  bang::GameState state(config);
  CompleteSetup(state);
  const int target = state.Players()[1].active_position;
  const int life = state.Players()[1].positions[target].life;
  state.ApplyAction(
      bang::PlayCardAction(HandCard(state, 0, bang::CardName::kDuel)));
  const int knife = HandCard(state, 1, bang::CardName::kKnife);
  const int missed = HandCard(state, 1, bang::CardName::kMissed);
  Require(state.CurrentPhase() == bang::Phase::kDuelResponse &&
              HasAction(state, bang::DiscardCardAction(knife)) &&
              !HasAction(state, bang::DiscardCardAction(missed)),
          "Duel accepts hit cards but not ordinary avoid cards");
  state.ApplyAction(bang::DiscardCardAction(knife));
  Require(state.CurrentPlayer() == 0,
          "Duel alternates to the player who played it after a response");
  state.ApplyAction(bang::DiscardCardAction(
      HandCard(state, 0, bang::CardName::kDerringer)));
  Require(state.CurrentPlayer() == 1,
          "Duel alternates back after each discarded hit card");
  state.ApplyAction(bang::kSkipOptionalAction);
  Require(state.Players()[1].positions[target].life == life - 1 &&
              state.CurrentPhase() == bang::Phase::kPlay &&
              state.CurrentPlayer() == 0,
          "the first player to stop responding loses one unavoidable life");
}

void TestGeneralStoreAndWagonChoices() {
  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kGeneralStore, bang::CardName::kWater,
         bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kHat, bang::CardName::kDerringer,
         bang::CardName::kBank, bang::CardName::kStrongAle});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kKnife,
         bang::CardName::kStealing, bang::CardName::kBeer,
         bang::CardName::kColt});
    bang::GameState state(config);
    CompleteSetup(state);
    const auto law_before = state.Players()[0].hand.size();
    const auto outlaw_before = state.Players()[1].hand.size();
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kGeneralStore),
        bang::Target::kOwnActive));
    Require(state.CurrentPhase() == bang::Phase::kChooseGeneralStore &&
                state.CurrentPlayer() == 0 && state.LegalActions().size() == 4,
            "General Store publicly displays four cards and starts with its player");
    const auto public_view = state.ObservationTensor(1);
    Require(public_view[bang::obs_layout::kChoiceCardsPublicOffset] == 1.0F &&
                public_view[bang::obs_layout::kChoiceCardCountOffset] == 1.0F,
            "both policies can observe the complete General Store display");
    state.ApplyAction(SelectActionFor(state, bang::CardName::kBank));
    Require(state.CurrentPlayer() == 1 && state.LegalActions().size() == 3,
            "General Store alternates picks after the first choice");
    state.ApplyAction(SelectActionFor(state, bang::CardName::kBeer));
    Require(state.CurrentPlayer() == 0 && state.LegalActions().size() == 2,
            "General Store returns the next pick to its player");
    state.ApplyAction(SelectActionFor(state, bang::CardName::kStrongAle));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kColt));
    Require(state.CurrentPhase() == bang::Phase::kPlay && state.CurrentPlayer() == 0 &&
                state.Players()[0].hand.size() == law_before + 1 &&
                state.Players()[1].hand.size() == outlaw_before + 2,
            "each player keeps two draft cards and play resumes with the actor");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kWagon, bang::CardName::kWater,
         bang::CardName::kMissed, bang::CardName::kHat,
         bang::CardName::kDerringer, bang::CardName::kReturnFire,
         bang::CardName::kBank, bang::CardName::kStrongAle,
         bang::CardName::kGeneralStore});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kWagon), bang::Target::kOwnActive));
    Require(state.CurrentPhase() == bang::Phase::kChooseWagonDiscard &&
                state.LegalActions().size() == 3,
            "Wagon draws three cards and requires one discard");
    const auto owner_view = state.ObservationTensor(0);
    const auto opponent_view = state.ObservationTensor(1);
    Require(owner_view[bang::obs_layout::kChoiceCardCountOffset] == 0.75F &&
                owner_view[bang::obs_layout::kChoiceCardNamesOffset +
                           static_cast<int>(bang::CardName::kBank)] > 0.0F &&
                opponent_view[bang::obs_layout::kChoiceCardNamesOffset +
                              static_cast<int>(bang::CardName::kBank)] == 0.0F,
            "Wagon choices are visible to their owner but remain hidden from the opponent");
    float opponent_choice_identity = 0.0F;
    for (int index = 0; index < bang::kCardNameCount; ++index) {
      opponent_choice_identity +=
          opponent_view[bang::obs_layout::kChoiceCardNamesOffset + index];
    }
    Require(opponent_choice_identity == 0.0F,
            "no Wagon card-name identity leaks through the opponent observation");
    int discard_bank = -1;
    for (const int action : state.LegalActions()) {
      const auto decoded = bang::DecodeAction(action);
      if (bang::GetCardInstance(decoded.card_instance).name == bang::CardName::kBank) {
        discard_bank = action;
      }
    }
    Require(discard_bank >= 0, "the selected Wagon card has a stable discard action");
    state.ApplyAction(discard_bank);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                std::find(state.DiscardPile().begin(), state.DiscardPile().end(),
                          bang::DecodeAction(discard_bank).card_instance) !=
                    state.DiscardPile().end(),
            "Wagon keeps two cards and discards the explicit third choice");
    RequireCardConservation(state);
  }
}

void TestSchoolmarmAndStealingChoices() {
  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kBank, bang::CardName::kSchoolmarm,
         bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kHat});
    bang::GameState state(config);
    CompleteSetup(state);
    const int bank = HandCard(state, 0, bang::CardName::kBank);
    const int rear = 1 - state.Players()[0].active_position;
    state.ApplyAction(
        bang::PlayCardAction(bank, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kSchoolmarm),
        bang::Target::kOwnActive));
    Require(state.CurrentPhase() == bang::Phase::kChooseSchoolmarm &&
                HasAction(state, bang::SelectCardAction(bank)) &&
                HasAction(state, bang::kSkipOptionalAction),
            "Schoolmarm optionally selects eligible Equipment in play");
    state.ApplyAction(bang::SelectCardAction(bank));
    Require(HandCard(state, 0, bang::CardName::kBank) == bank &&
                !CharacterHasEquipment(state, 0, rear, bang::CardName::kBank),
            "Schoolmarm returns the chosen Equipment to its player's hand");
  }

  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kHat, bang::CardName::kBank,
         bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kDerringer});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kStealing, bang::CardName::kWater,
         bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    const int hat = HandCard(state, 0, bang::CardName::kHat);
    const int bank = HandCard(state, 0, bang::CardName::kBank);
    state.ApplyAction(
        bang::PlayCardAction(hat, bang::Target::kOwnRearguard));
    state.ApplyAction(
        bang::PlayCardAction(bank, bang::Target::kOpponentActive));
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kStealing),
        bang::Target::kOwnActive));
    Require(HasAction(state, bang::SelectCardAction(bank)) &&
                !HasAction(state, bang::SelectCardAction(hat)) &&
                HasAction(state, bang::kChooseLawBackAction) &&
                !HasAction(state, bang::kChooseOutlawBackAction),
            "Stealing offers visible card backs and excludes opponent RC Equipment");
    const auto view = state.ObservationTensor(1);
    Require(view[bang::obs_layout::kOpponentLawBackCountOffset] > 0.0F &&
                view[bang::obs_layout::kOpponentOutlawBackCountOffset] == 0.0F,
            "opponent hand origins are observable without exposing card faces");
    state.ApplyAction(bang::SelectCardAction(bank));
    Require(std::find(state.DiscardPile().begin(), state.DiscardPile().end(), bank) !=
                state.DiscardPile().end(),
            "Stealing may discard eligible Equipment instead of drawing from hand");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kStealing, bang::CardName::kWater,
         bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    const auto actor_before = state.Players()[1].hand.size();
    const auto actor_cards_before = state.Players()[1].hand;
    const auto opponent_before = state.Players()[0].hand.size();
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kStealing),
        bang::Target::kOwnActive));
    state.ApplyAction(bang::kChooseLawBackAction);
    Require(state.Players()[1].hand.size() == actor_before &&
                state.Players()[0].hand.size() + 1 == opponent_before,
            "Stealing draws a random card matching the selected visible back");
    const auto moved = std::find_if(
        state.Players()[1].hand.begin(), state.Players()[1].hand.end(),
        [&](int card) {
          return std::find(actor_cards_before.begin(), actor_cards_before.end(),
                           card) == actor_cards_before.end();
        });
    Require(moved != state.Players()[1].hand.end(),
            "Stealing adds one identifiable card to the actor's hand");
    const auto owner_view = state.ObservationTensor(0);
    Require(owner_view[bang::obs_layout::kPublicHandCardsOffset +
                       bang::obs_layout::kPublicHandBlockSize +
                       static_cast<int>(
                           bang::GetCardInstance(*moved).name)] > 0.0F,
            "the former owner retains public knowledge of the stolen card");
    RequireCardConservation(state);
  }
}

void TestBartChoosesReveal() {
  auto config = DeterministicConfig();
  config.character_rosters[0] = {2, 0};
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {bang::CardName::kBarrel, bang::CardName::kMissed, bang::CardName::kWater,
       bang::CardName::kHat, bang::CardName::kDerringer, bang::CardName::kReturnFire,
       bang::CardName::kGeneralStore, bang::CardName::kWagon});
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kColt, bang::CardName::kBarrel, bang::CardName::kMissed,
       bang::CardName::kClock, bang::CardName::kGunBelt, bang::CardName::kWater,
       bang::CardName::kKnife});
  bang::GameState state(config);
  CompleteSetup(state);
  const int barrel = HandCard(state, 0, bang::CardName::kBarrel);
  state.ApplyAction(bang::PlayCardAction(barrel, bang::Target::kOwnRearguard));
  state.ApplyAction(bang::kSwapAction);  // Bart is now RC, but his marked ability remains active.
  EndCurrentTurn(state);
  state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, bang::CardName::kColt)));
  state.ApplyAction(bang::AvoidHitAction(barrel));
  Require(state.CurrentPhase() == bang::Phase::kChooseReveal,
          "Bart reveals two cards instead of one");
  Require(state.CurrentPlayer() == 0, "Bart's owner chooses the reveal result");
  Require(state.LegalActions() ==
              std::vector<int>{bang::kChooseRevealFirstAction, bang::kChooseRevealSecondAction},
          "either revealed card can be selected");
  state.ApplyAction(bang::kChooseRevealSecondAction);
  Require(state.CurrentPhase() == bang::Phase::kPlay,
          "choosing the Barrel-symbol reveal cancels the hit");
}

void TestBeginningTurnAndClockEquipment() {
  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kBank, bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kDerringer, bang::CardName::kReturnFire});
    bang::GameState state(config);
    CompleteSetup(state);
    const int bank = HandCard(state, 0, bang::CardName::kBank);
    state.ApplyAction(bang::PlayCardAction(bank, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    EndCurrentTurn(state);
    const auto deck_before = state.Players()[0].deck.size();
    EndCurrentTurn(state);
    Require(deck_before - state.Players()[0].deck.size() == 3,
            "active Bank adds one mandatory draw");
  }

  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kStampede, bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kDerringer, bang::CardName::kReturnFire});
    bang::GameState state(config);
    CompleteSetup(state);
    const int starting_active = state.Players()[0].active_position;
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, bang::CardName::kStampede),
                                           bang::Target::kOwnRearguard));
    EndCurrentTurn(state);
    EndCurrentTurn(state);
    Require(state.Players()[0].active_position != starting_active,
            "Stampede swaps its character at the beginning of its owner's turn");
    Require(HasAction(state, bang::kSwapAction),
            "triggered Stampede does not consume the voluntary turn swap");
  }

  {
    auto config = DeterministicConfig();
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kClock, bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kKnife, bang::CardName::kStealing,
         bang::CardName::kBeer});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, bang::CardName::kClock),
                                           bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    EndCurrentTurn(state);
    const auto hand_before = state.Players()[1].hand.size();
    state.ApplyAction(bang::kSwapAction);
    Require(state.Players()[1].hand.size() == hand_before + 1,
            "active Clock draws when the opponent moves its AC token");
  }
}

void TestDynamiteExplosionAndTransfer() {
  {
    auto config = DeterministicConfig();
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kDynamite, bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kKnife, bang::CardName::kStealing,
         bang::CardName::kBeer, bang::CardName::kColt});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    const int dynamite = HandCard(state, 1, bang::CardName::kDynamite);
    Require(HasAction(state,
                      bang::PlayCardAction(dynamite, bang::Target::kOwnRearguard)),
            "Dynamite can be played on own RC");
    Require(!HasAction(state,
                       bang::PlayCardAction(dynamite, bang::Target::kOpponentActive)),
            "Dynamite cannot use the ordinary opponent-AC Equipment target");
    const int rear = 1 - state.Players()[1].active_position;
    const int life = state.Players()[1].positions[rear].life;
    state.ApplyAction(
        bang::PlayCardAction(dynamite, bang::Target::kOwnRearguard));
    EndCurrentTurn(state);
    EndCurrentTurn(state);
    Require(state.Players()[1].positions[rear].life == life - 3,
            "Dynamite symbol causes three unavoidable life loss");
    Require(!CharacterHasEquipment(state, 1, rear, bang::CardName::kDynamite),
            "exploded Dynamite leaves play");
    Require(std::find(state.DiscardPile().begin(), state.DiscardPile().end(), dynamite) !=
                state.DiscardPile().end(),
            "exploded Dynamite is discarded");
  }

  {
    auto config = DeterministicConfig();
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kDynamite, bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kKnife, bang::CardName::kStealing,
         bang::CardName::kBeer, bang::CardName::kSchofield});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    const int dynamite = HandCard(state, 1, bang::CardName::kDynamite);
    state.ApplyAction(
        bang::PlayCardAction(dynamite, bang::Target::kOwnRearguard));
    EndCurrentTurn(state);
    EndCurrentTurn(state);
    const int law_rear = 1 - state.Players()[0].active_position;
    Require(CharacterHasEquipment(state, 0, law_rear, bang::CardName::kDynamite),
            "failed Dynamite reveal passes it to opponent RC");
  }
}

void TestRattlesnakeThenStampedeOrder() {
  auto config = DeterministicConfig();
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {bang::CardName::kStampede, bang::CardName::kWater, bang::CardName::kMissed,
       bang::CardName::kWagon, bang::CardName::kDerringer, bang::CardName::kReturnFire});
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kRattlesnake, bang::CardName::kWater, bang::CardName::kMissed,
       bang::CardName::kWagon, bang::CardName::kKnife, bang::CardName::kStealing,
       bang::CardName::kBeer, bang::CardName::kGunBelt});
  bang::GameState state(config);
  CompleteSetup(state);
  state.ApplyAction(bang::PlayCardAction(HandCard(state, 0, bang::CardName::kStampede),
                                         bang::Target::kOpponentActive));
  EndCurrentTurn(state);
  const int rattlesnake =
      HandCard(state, 1, bang::CardName::kRattlesnake);
  Require(HasAction(
              state,
              bang::PlayCardAction(rattlesnake, bang::Target::kOpponentActive)),
          "Rattlesnake can be placed on the opponent's active character");
  const int rattlesnake_position = 1 - state.Players()[1].active_position;
  const int life = state.Players()[1].positions[rattlesnake_position].life;
  state.ApplyAction(
      bang::PlayCardAction(rattlesnake, bang::Target::kOwnRearguard));
  EndCurrentTurn(state);
  EndCurrentTurn(state);
  Require(state.Players()[1].positions[rattlesnake_position].life == life - 1,
          "Rattlesnake resolves its Snake reveal before Stampede");
  Require(state.Players()[1].active_position == rattlesnake_position,
          "Stampede swaps positions after Rattlesnake resolves");
  Require(CharacterHasEquipment(state, 1, rattlesnake_position,
                                bang::CardName::kRattlesnake),
          "Rattlesnake remains in play after causing damage");
}

void TestBartChoosesDynamiteReveal() {
  auto config = DeterministicConfig();
  config.character_rosters[0] = {2, 0};
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kDynamite, bang::CardName::kWater, bang::CardName::kMissed,
       bang::CardName::kWagon, bang::CardName::kKnife, bang::CardName::kStealing,
       bang::CardName::kBeer, bang::CardName::kSchofield, bang::CardName::kColt});
  bang::GameState state(config);
  CompleteSetup(state);
  EndCurrentTurn(state);
  const int rear = 1 - state.Players()[1].active_position;
  const int life = state.Players()[1].positions[rear].life;
  state.ApplyAction(
      bang::PlayCardAction(HandCard(state, 1, bang::CardName::kDynamite),
                           bang::Target::kOwnRearguard));
  EndCurrentTurn(state);
  EndCurrentTurn(state);
  Require(state.CurrentPhase() == bang::Phase::kChooseReveal && state.CurrentPlayer() == 0,
          "Bart's owner chooses between two Dynamite reveals");
  const auto observation = state.ObservationTensor(0);
  Require(observation[bang::obs_layout::kRevealReasonOffset + 1] == 1.0F,
          "policy observation identifies a Dynamite reveal choice");
  state.ApplyAction(bang::kChooseRevealSecondAction);
  Require(state.Players()[1].positions[rear].life == life - 3,
          "Bart can select the Dynamite symbol as the result");
  Require(state.CurrentPhase() == bang::Phase::kPlay && state.CurrentPlayer() == 1,
          "beginning-of-turn sequence resumes through normal draw and play");
}

void TestBeerStrongAleAndWater() {
  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kStrongAle, bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kDerringer, bang::CardName::kHat});
    bang::GameState state(config);
    CompleteSetup(state);
    const int strong_ale = HandCard(state, 0, bang::CardName::kStrongAle);
    Require(HasAction(state,
                      bang::PlayCardAction(strong_ale, bang::Target::kOwnActive)) &&
                HasAction(state,
                          bang::PlayCardAction(strong_ale,
                                               bang::Target::kOwnRearguard)) &&
                HasAction(state,
                          bang::PlayCardAction(strong_ale,
                                               bang::Target::kOpponentActive)),
            "Strong Ale targets any character except opponent RC");
    Require(!HasAction(state,
                       bang::PlayCardAction(strong_ale,
                                            bang::Target::kOpponentRearguard)),
            "Strong Ale cannot target opponent RC");
    const int active = state.Players()[0].active_position;
    state.ApplyAction(
        bang::PlayCardAction(strong_ale, bang::Target::kOwnActive));
    Require(state.Players()[0].positions[active].life == 3,
            "Strong Ale sets life to exactly three");

    const int water = HandCard(state, 0, bang::CardName::kWater);
    state.ApplyAction(bang::PlayCardAction(water, bang::Target::kOwnActive));
    Require(state.Players()[0].active_position != active,
            "Water swaps the selected player's AC and RC");
    Require(HasAction(state, bang::kSwapAction),
            "card-induced Water swap does not consume voluntary swap");
  }

  {
    auto config = DeterministicConfig();
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kColt, bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kDerringer, bang::CardName::kHat});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kBeer, bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    const int target = state.Players()[1].active_position;
    const int maximum =
        bang::GetCharacter(state.Players()[1].positions[target].character_id).max_life;
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kColt)));
    state.ApplyAction(bang::kTakeHitAction);
    EndCurrentTurn(state);
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 1, bang::CardName::kBeer),
                             bang::Target::kOwnActive));
    Require(state.Players()[1].positions[target].life == maximum,
            "Beer heals both owned characters up to their printed maximum");
  }
}

void TestGunBeltAndEquipmentElimination() {
  auto config = DeterministicConfig();
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {bang::CardName::kHat, bang::CardName::kWater, bang::CardName::kMissed,
       bang::CardName::kWagon, bang::CardName::kDerringer, bang::CardName::kReturnFire});
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kGunBelt, bang::CardName::kScope, bang::CardName::kColt,
       bang::CardName::kColt, bang::CardName::kColt, bang::CardName::kColt,
       bang::CardName::kWater});
  bang::GameState state(config);
  CompleteSetup(state);
  const int hat = HandCard(state, 0, bang::CardName::kHat);
  state.ApplyAction(bang::PlayCardAction(hat, bang::Target::kOwnRearguard));
  state.ApplyAction(bang::kSwapAction);
  const int eliminated_position = state.Players()[0].active_position;
  EndCurrentTurn(state);

  state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, bang::CardName::kGunBelt),
                                         bang::Target::kOwnRearguard));
  const int scope = HandCard(state, 1, bang::CardName::kScope);
  state.ApplyAction(
      bang::PlayCardAction(scope, bang::Target::kOwnRearguard));
  state.ApplyAction(bang::kSwapAction);
  for (int hit = 0; hit < 4; ++hit) {
    const int colt = HandCard(state, 1, bang::CardName::kColt);
    Require(HasAction(state, bang::PlayCardAction(colt)),
            "active Gun Belt permits every successive BANG card");
    state.ApplyAction(bang::PlayCardAction(colt));
    state.ApplyAction(bang::kTakeHitAction);
  }
  Require(state.Players()[0].positions[eliminated_position].character_id == -1,
          "four unavoided hits eliminate the four-life character");
  Require(std::find(state.DiscardPile().begin(), state.DiscardPile().end(), hat) !=
              state.DiscardPile().end(),
          "all Equipment on an eliminated character is discarded");
  const int water = HandCard(state, 1, bang::CardName::kWater);
  Require(HasAction(state, bang::ReloadCardAction(scope)),
          "reloadable Equipment on AC can be reloaded when a roster reaches one");
  state.ApplyAction(bang::ReloadCardAction(scope));
  Require(std::find(state.DiscardPile().begin(), state.DiscardPile().end(), scope) !=
              state.DiscardPile().end(),
          "reloaded active Equipment is discarded");
  Require(HasAction(state, bang::ReloadCardAction(water)),
          "reloadable cards in hand are legal under the same condition");
  const auto hand_before_reload = state.Players()[1].hand.size();
  state.ApplyAction(bang::ReloadCardAction(water));
  Require(state.Players()[1].hand.size() == hand_before_reload,
          "hand reload discards one card and draws one card");
}

void TestBangLimitWithoutGunBelt() {
  auto config = DeterministicConfig();
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kColt, bang::CardName::kColt, bang::CardName::kMissed,
       bang::CardName::kWater, bang::CardName::kKnife, bang::CardName::kStealing,
       bang::CardName::kBeer});
  bang::GameState state(config);
  CompleteSetup(state);
  EndCurrentTurn(state);
  const int first = HandCard(state, 1, bang::CardName::kColt);
  state.ApplyAction(bang::PlayCardAction(first));
  state.ApplyAction(bang::kTakeHitAction);
  const int second = HandCard(state, 1, bang::CardName::kColt);
  Require(!HasAction(state, bang::PlayCardAction(second)),
          "without Gun Belt only one BANG card is legal per turn");
}

void TestPassiveCharacterDrawAndBangLimits() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kDaltonBros),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kBank, bang::CardName::kWater,
         bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kDerringer, bang::CardName::kHat});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kBank),
                             bang::Target::kOpponentActive));
    EndCurrentTurn(state);
    Require(state.Players()[1].hand.size() == 9,
            "active Dalton Bros draws three cards, plus one from Bank");
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kAlanPinkertoon),
        static_cast<int>(bang::CharacterName::kTheStranger)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kCarbine, bang::CardName::kFanning,
         bang::CardName::kColt, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kCarbine)));
    state.ApplyAction(bang::kTakeHitAction);
    const int fanning = HandCard(state, 0, bang::CardName::kFanning);
    Require(HasAction(state, bang::PlayCardAction(fanning)),
            "The Stranger grants a second BANG! while in the RC");
    state.ApplyAction(bang::PlayCardAction(fanning));
    Require(!HasAction(state, bang::PlayCardAction(
                                  HandCard(state, 0, bang::CardName::kColt))),
            "The Stranger raises the limit to exactly two BANG! cards");
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kWestHarding),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kColt, bang::CardName::kColt,
         bang::CardName::kColt, bang::CardName::kColt,
         bang::CardName::kColt, bang::CardName::kWater,
         bang::CardName::kMissed});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    for (int count = 0; count < 3; ++count) {
      const int colt = HandCard(state, 1, bang::CardName::kColt);
      Require(HasAction(state, bang::PlayCardAction(colt)),
              "active West Harding keeps every BANG! card legal");
      state.ApplyAction(bang::PlayCardAction(colt));
      state.ApplyAction(bang::kTakeHitAction);
    }
  }
}

void TestAnnieAndTocoCardTransformation() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kAnnieOakey),
        static_cast<int>(bang::CharacterName::kAlanPinkertoon)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kMissed, bang::CardName::kMissed,
         bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kHat, bang::CardName::kDerringer});
    bang::GameState state(config);
    CompleteSetup(state);
    const int target = state.Players()[1].active_position;
    const int life = state.Players()[1].positions[target].life;
    const int first_missed = HandCard(state, 0, bang::CardName::kMissed);
    Require(HasAction(state, bang::PlayAsColtAction(first_missed)),
            "active Annie Oakey may play Missed! as Colt");
    state.ApplyAction(bang::PlayAsColtAction(first_missed));
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.Players()[1].positions[target].life == life - 1,
            "Annie's transformed Missed! resolves as an ordinary Colt");
    const int second_missed = HandCard(state, 0, bang::CardName::kMissed);
    Require(!HasAction(state, bang::PlayAsColtAction(second_missed)),
            "Annie's transformed Colt consumes the one-BANG-card limit");
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kAlanPinkertoon),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kMissed, bang::CardName::kCarbine,
         bang::CardName::kDerringer, bang::CardName::kHat});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kScope, bang::CardName::kColt,
         bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon, bang::CardName::kKnife,
         bang::CardName::kBeer});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    const int scope = HandCard(state, 1, bang::CardName::kScope);
    state.ApplyAction(
        bang::PlayCardAction(scope, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 1, bang::CardName::kColt),
                             bang::Target::kOpponentRearguard));
    const int carbine = HandCard(state, 0, bang::CardName::kCarbine);
    const int derringer = HandCard(state, 0, bang::CardName::kDerringer);
    Require(HasAction(state, bang::AvoidHitAction(carbine)) &&
                HasAction(state, bang::AvoidHitAction(derringer)),
            "targeted RC Annie wakes and may use any hit card as an avoid");
    state.ApplyAction(bang::AvoidHitAction(derringer));
    Require(state.CurrentPhase() == bang::Phase::kPlay && state.CurrentPlayer() == 1,
            "Annie's transformed avoid cancels the targeted RC hit");
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kTocoRamirez),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kWater, bang::CardName::kStealing,
         bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kKnife, bang::CardName::kBeer,
         bang::CardName::kColt});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    const int water = HandCard(state, 1, bang::CardName::kWater);
    Require(HasAction(state, bang::PlayAsColtAction(water)) &&
                HasAction(state, bang::PlayCardAction(
                                     water, bang::Target::kOpponentActive)),
            "Toco Ramirez exposes transformed Colt and printed card actions separately");
    state.ApplyAction(bang::PlayAsColtAction(water));
    state.ApplyAction(bang::kTakeHitAction);
    const int stealing = HandCard(state, 1, bang::CardName::kStealing);
    Require(!HasAction(state, bang::PlayAsColtAction(stealing)),
            "Toco's transformed card consumes the BANG-card limit");
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kAnnieOakey),
        static_cast<int>(bang::CharacterName::kAlanPinkertoon)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kDerringer, bang::CardName::kHat,
         bang::CardName::kStrongAle, bang::CardName::kMissed});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kDuel, bang::CardName::kWater,
         bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kDuel),
        bang::Target::kOpponentActive));
    const int missed = HandCard(state, 0, bang::CardName::kMissed);
    Require(HasAction(state, bang::DiscardCardAction(missed)),
            "Annie may use Missed as a Colt when Duel requires a hit");
    state.ApplyAction(bang::DiscardCardAction(missed));
    state.ApplyAction(bang::kSkipOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1,
            "Annie's transformed Duel payment alternates normally");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kTocoRamirez),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kDuel, bang::CardName::kWater,
         bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kDerringer, bang::CardName::kHat});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kWater, bang::CardName::kStealing,
         bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kDuel),
        bang::Target::kOpponentActive));
    const int water = HandCard(state, 1, bang::CardName::kWater);
    Require(HasAction(state, bang::DiscardCardAction(water)),
            "Toco may use any hand card as a Colt during Duel");
    state.ApplyAction(bang::DiscardCardAction(water));
    state.ApplyAction(bang::kSkipOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 0,
            "Toco's transformed Duel payment alternates normally");
    RequireCardConservation(state);
  }
}

void TestJackRansomeTargetOverrides() {
  auto config = DeterministicConfig();
  config.character_rosters[1] = {
      static_cast<int>(bang::CharacterName::kJackRansome),
      static_cast<int>(bang::CharacterName::kBabeLeroy)};
  config.starting_decks[0] = DeckWithTop(
      bang::Side::kLaw,
      {bang::CardName::kHat, bang::CardName::kWater,
       bang::CardName::kMissed, bang::CardName::kWagon,
       bang::CardName::kDerringer, bang::CardName::kBank});
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kStealing, bang::CardName::kDynamite,
       bang::CardName::kKnife, bang::CardName::kColt,
       bang::CardName::kDuel, bang::CardName::kMissed,
       bang::CardName::kWagon});
  bang::GameState state(config);
  CompleteSetup(state);
  const int hat = HandCard(state, 0, bang::CardName::kHat);
  state.ApplyAction(
      bang::PlayCardAction(hat, bang::Target::kOwnRearguard));
  EndCurrentTurn(state);
  state.ApplyAction(
      bang::PlayCardAction(HandCard(state, 1, bang::CardName::kStealing),
                           bang::Target::kOwnActive));
  Require(HasAction(state, bang::SelectCardAction(hat)),
          "active Jack lets Stealing target opponent RC Equipment");
  state.ApplyAction(bang::SelectCardAction(hat));
  const int dynamite = HandCard(state, 1, bang::CardName::kDynamite);
  Require(HasAction(state, bang::PlayCardAction(
                               dynamite, bang::Target::kOpponentRearguard)) &&
              HasAction(state, bang::PlayCardAction(
                                   dynamite, bang::Target::kOwnActive)),
          "Jack may play Dynamite on any in-play character");
  state.ApplyAction(
      bang::PlayCardAction(dynamite, bang::Target::kOpponentRearguard));
  const int law_rear = 1 - state.Players()[0].active_position;
  Require(CharacterHasEquipment(state, 0, law_rear, bang::CardName::kDynamite),
          "Jack's Equipment override preserves the selected character");
  const int knife = HandCard(state, 1, bang::CardName::kKnife);
  Require(HasAction(state, bang::PlayCardAction(
                               knife, bang::Target::kOpponentRearguard)),
          "Jack may target the opponent RC with Knife");
  state.ApplyAction(
      bang::PlayCardAction(knife, bang::Target::kOpponentRearguard));
  state.ApplyAction(bang::kTakeHitAction);
  const int colt = HandCard(state, 1, bang::CardName::kColt);
  Require(HasAction(state, bang::PlayCardAction(
                               colt, bang::Target::kOpponentRearguard)),
          "Jack may target the opponent RC with a BANG! without Scope");
  state.ApplyAction(
      bang::PlayCardAction(colt, bang::Target::kOpponentRearguard));
  state.ApplyAction(bang::kTakeHitAction);
  const int rear_life = state.Players()[0].positions[law_rear].life;
  const int duel = HandCard(state, 1, bang::CardName::kDuel);
  Require(HasAction(state, bang::PlayCardAction(
                               duel, bang::Target::kOpponentRearguard)),
          "Jack may target the opponent RC with Duel");
  state.ApplyAction(
      bang::PlayCardAction(duel, bang::Target::kOpponentRearguard));
  state.ApplyAction(bang::kSkipOptionalAction);
  Require(state.Players()[0].positions[law_rear].life == rear_life - 1,
          "Jack's Duel damages the selected RC when it declines");
  RequireCardConservation(state);
}

void TestBabeLeroyAndCattleKatieAbilities() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kBabeLeroy),
        static_cast<int>(bang::CharacterName::kBullAnderson)};
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kBeer, bang::CardName::kWater,
         bang::CardName::kKnife, bang::CardName::kBarrel,
         bang::CardName::kHat, bang::CardName::kGunBelt,
         bang::CardName::kScope});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kBeer), bang::Target::kOwnActive));
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kWater),
        bang::Target::kOpponentActive));
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 1, bang::CardName::kKnife)));
    state.ApplyAction(bang::kTakeHitAction);
    for (const auto name : {bang::CardName::kBarrel, bang::CardName::kHat,
                            bang::CardName::kGunBelt, bang::CardName::kScope}) {
      state.ApplyAction(bang::PlayCardAction(HandCard(state, 1, name),
                                             bang::Target::kOwnRearguard));
    }
    Require(state.Players()[1].hand.empty() &&
                HasAction(state, bang::UseAbilityAction(
                                     bang::CharacterName::kBabeLeroy)),
            "active Babe Leroy may use her ability with an empty hand");
    state.ApplyAction(
        bang::UseAbilityAction(bang::CharacterName::kBabeLeroy));
    Require(state.Players()[1].hand.size() == 2 &&
                !HasAction(state, bang::UseAbilityAction(
                                      bang::CharacterName::kBabeLeroy)),
            "Babe Leroy draws two cards and is limited to once per turn");
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kCattleKatie),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kBarrel, bang::CardName::kRattlesnake,
         bang::CardName::kWater, bang::CardName::kMissed,
         bang::CardName::kWagon});
    bang::GameState state(config);
    state.ApplyAction(bang::kChooseActiveLeftAction);
    state.ApplyAction(bang::kChooseActiveRightAction);
    EndCurrentTurn(state);
    const int barrel = HandCard(state, 1, bang::CardName::kBarrel);
    const int rattlesnake = HandCard(state, 1, bang::CardName::kRattlesnake);
    state.ApplyAction(
        bang::PlayCardAction(barrel, bang::Target::kOwnRearguard));
    state.ApplyAction(
        bang::PlayCardAction(rattlesnake, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    Require(HasAction(state, bang::UseAbilityAction(
                                 bang::CharacterName::kCattleKatie)),
            "active Cattle Katie may activate once with eligible Equipment");
    state.ApplyAction(
        bang::UseAbilityAction(bang::CharacterName::kCattleKatie));
    Require(HasAction(state, bang::SelectCardAction(barrel)) &&
                !HasAction(state, bang::SelectCardAction(rattlesnake)),
            "Cattle Katie cannot discard always-active Equipment");
    const auto hand_before = state.Players()[1].hand.size();
    state.ApplyAction(bang::SelectCardAction(barrel));
    Require(state.Players()[1].hand.size() == hand_before + 2 &&
                !HasAction(state, bang::UseAbilityAction(
                                      bang::CharacterName::kCattleKatie)),
            "Cattle Katie discards Equipment, draws two, and is used for the turn");
    const auto observation = state.ObservationTensor(1);
    Require(observation[bang::obs_layout::kAbilityUsedOffset +
                        static_cast<int>(bang::CharacterName::kCattleKatie)] == 1.0F,
            "activated ability usage is policy-visible");
  }
}

void TestSoundanceKidAbility() {
  auto config = DeterministicConfig();
  config.character_rosters[1] = {
      static_cast<int>(bang::CharacterName::kBabeLeroy),
      static_cast<int>(bang::CharacterName::kSoundanceKid)};
  config.starting_decks[1] = DeckWithTop(
      bang::Side::kOutlaw,
      {bang::CardName::kWater, bang::CardName::kMissed,
       bang::CardName::kWagon, bang::CardName::kKnife,
       bang::CardName::kStealing, bang::CardName::kBeer,
       bang::CardName::kColt, bang::CardName::kBarrel});
  bang::GameState state(config);
  CompleteSetup(state);
  EndCurrentTurn(state);
  const auto hand_before = state.Players()[1].hand.size();
  Require(HasAction(state, bang::UseAbilityAction(
                               bang::CharacterName::kSoundanceKid)),
          "Soundance Kid's marked ability works while he is RC");
  state.ApplyAction(
      bang::UseAbilityAction(bang::CharacterName::kSoundanceKid));
  const int cost = state.Players()[1].hand.front();
  state.ApplyAction(bang::DiscardCardAction(cost));
  Require(state.ChoiceCards().size() == 2 &&
              bang::GetCardInstance(state.ChoiceCards()[0]).origin ==
                  bang::Side::kOutlaw &&
              bang::GetCardInstance(state.ChoiceCards()[1]).origin ==
                  bang::Side::kLaw,
          "Soundance draws one card from each personal deck");
  const auto owner_view = state.ObservationTensor(1);
  const auto opponent_view = state.ObservationTensor(0);
  Require(owner_view[bang::obs_layout::kPendingAbilityOffset +
                     static_cast<int>(bang::CharacterName::kSoundanceKid)] == 1.0F &&
              owner_view[bang::obs_layout::kPendingAbilityStageOffset] == 1.0F &&
              opponent_view[bang::obs_layout::kChoiceCardNamesOffset +
                            static_cast<int>(bang::GetCardInstance(
                                state.ChoiceCards()[0]).name)] == 0.0F,
          "Soundance's private keep/discard choice is policy-visible only to its owner");
  state.ApplyAction(bang::DiscardCardAction(state.ChoiceCards().front()));
  Require(state.CurrentPhase() == bang::Phase::kPlay &&
              state.Players()[1].hand.size() == hand_before &&
              !HasAction(state, bang::UseAbilityAction(
                                    bang::CharacterName::kSoundanceKid)),
          "Soundance keeps one drawn card and is limited to once per turn");
  RequireCardConservation(state);
}

void TestTomThornSwapTriggeredHitsAndContinuations() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kBabeLeroy),
        static_cast<int>(bang::CharacterName::kTomThorn)};
    bang::GameState state(config);
    state.ApplyAction(bang::kChooseActiveLeftAction);
    state.ApplyAction(bang::kChooseActiveRightAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 0 &&
                state.Players()[1]
                        .positions[state.Players()[1].active_position]
                        .character_id ==
                    static_cast<int>(bang::CharacterName::kTomThorn),
            "Tom Thorn does not fire when he first enters play as AC");
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kBabeLeroy),
        static_cast<int>(bang::CharacterName::kTomThorn)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kDerringer, bang::CardName::kHat,
         bang::CardName::kStrongAle, bang::CardName::kReturnFire});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kHat), bang::Target::kOwnRearguard));
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kWater), bang::Target::kOwnActive));
    EndCurrentTurn(state);
    const int law_rear = 1 - state.Players()[0].active_position;
    const int law_life = state.Players()[0].positions[law_rear].life;
    const int tom_position = 1 - state.Players()[1].active_position;
    const int tom_life = state.Players()[1].positions[tom_position].life;
    state.ApplyAction(bang::kSwapAction);
    const auto observation = state.ObservationTensor(1);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 0 &&
                observation[bang::obs_layout::kPendingSourceCharacterOffset +
                            static_cast<int>(bang::CharacterName::kTomThorn)] == 1.0F &&
                observation[bang::obs_layout::kPendingResumeKindOffset + 3] == 1.0F &&
                observation[bang::obs_layout::kMovementActiveOffset] == 1.0F,
            "Tom Thorn targets the opposing RC when he voluntarily becomes AC");
    state.ApplyAction(bang::AvoidHitAction(
        HandCard(state, 0, bang::CardName::kReturnFire)));
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 1,
            "Return Fire counters Tom Thorn's triggered BANG");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.Players()[0].positions[law_rear].life == law_life &&
                state.Players()[1].positions[tom_position].life == tom_life - 1 &&
                state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1,
            "Tom's Return Fire chain returns to the moving player's play phase");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kBabeLeroy),
        static_cast<int>(bang::CharacterName::kTomThorn)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kGeneralStore, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kMissed, bang::CardName::kStrongAle,
         bang::CardName::kHat, bang::CardName::kReturnFire});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kStealing, bang::CardName::kBeer,
         bang::CardName::kMissed, bang::CardName::kClock,
         bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kGeneralStore),
        bang::Target::kOwnActive));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kClock));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kReturnFire));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kHat));
    state.ApplyAction(state.LegalActions().front());
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kClock),
        bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    EndCurrentTurn(state);
    const auto hand_before = state.Players()[0].hand.size();
    state.ApplyAction(bang::kSwapAction);
    const auto clock_owner_view = state.ObservationTensor(0);
    Require(state.Players()[0].hand.size() == hand_before &&
                clock_owner_view[bang::obs_layout::kMovementActiveOffset] == 1.0F &&
                clock_owner_view[bang::obs_layout::kMovementTriggersOffset + 1] == 1.0F,
            "the current player's Tom hit precedes the opponent's simultaneous Clock draw");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.Players()[0].hand.size() == hand_before + 1,
            "the deferred Clock draw resolves after Tom's hit");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kBabeLeroy),
        static_cast<int>(bang::CharacterName::kTomThorn)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kStampede, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kStrongAle});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kStampede),
        bang::Target::kOpponentActive));
    const auto hand_before_draw = state.Players()[1].hand.size();
    EndCurrentTurn(state);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 0,
            "Stampede pauses the beginning of turn for Tom's triggered hit");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1 &&
                state.Players()[1].hand.size() == hand_before_draw + 2,
            "Tom's Stampede hit resumes and completes the normal draw phase");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kBabeLeroy),
        static_cast<int>(bang::CharacterName::kTomThorn)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kDerringer, bang::CardName::kHat,
         bang::CardName::kStrongAle, bang::CardName::kMissed});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kShotgun, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kStealing,
         bang::CardName::kBeer, bang::CardName::kKnife,
         bang::CardName::kMissed});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 1, bang::CardName::kShotgun)));
    state.ApplyAction(
        bang::AvoidHitAction(HandCard(state, 0, bang::CardName::kMissed)));
    state.ApplyAction(bang::kChooseOwnSwapAction);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 0,
            "Tom's hit interrupts the Shotgun swap continuation");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1,
            "resolving Tom's hit completes the pending Shotgun effect exactly once");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kBabeLeroy),
        static_cast<int>(bang::CharacterName::kTomThorn)};
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kClock, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kStealing,
         bang::CardName::kBeer});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kClock),
        bang::Target::kOpponentActive));
    const auto law_hand_before = state.Players()[0].hand.size();
    state.ApplyAction(bang::kSwapAction);
    const auto pending = state.ObservationTensor(0);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.Players()[0].hand.size() == law_hand_before &&
                pending[bang::obs_layout::kMovementActiveOffset] == 1.0F &&
                pending[bang::obs_layout::kMovementTriggersOffset + 1] == 1.0F,
            "current-player Tom resolves before the opponent AC's Clock");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1 &&
                state.Players()[0].hand.size() == law_hand_before + 1,
            "the opponent's Clock draws after Tom's triggered BANG resolves");
    RequireCardConservation(state);
  }
}

void TestAlanPinkertoonNonLastLifeCounters() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kAlanPinkertoon),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kColt, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kStealing,
         bang::CardName::kBeer, bang::CardName::kKnife,
         bang::CardName::kMissed});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    const int alan_position = state.Players()[0].active_position;
    const int outlaw_position = state.Players()[1].active_position;
    const int alan_life = state.Players()[0].positions[alan_position].life;
    const int outlaw_life = state.Players()[1].positions[outlaw_position].life;
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 1, bang::CardName::kColt)));
    state.ApplyAction(bang::kTakeHitAction);
    const auto observation = state.ObservationTensor(1);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 1 &&
                observation[bang::obs_layout::kPendingSourceCharacterOffset +
                            static_cast<int>(bang::CharacterName::kAlanPinkertoon)] == 1.0F,
            "Alan Pinkertoon counters each non-last life lost with a normal hit");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.Players()[0].positions[alan_position].life == alan_life - 1 &&
                state.Players()[1].positions[outlaw_position].life == outlaw_life - 1 &&
                state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1,
            "Alan's counter resolves before returning to the attacking play phase");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kAlanPinkertoon),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kGunBelt, bang::CardName::kColt,
         bang::CardName::kColt, bang::CardName::kColt,
         bang::CardName::kColt, bang::CardName::kWater,
         bang::CardName::kWagon});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kGunBelt),
        bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    const int outlaw_position = state.Players()[1].active_position;
    const int outlaw_life = state.Players()[1].positions[outlaw_position].life;
    for (int hit = 0; hit < 4; ++hit) {
      state.ApplyAction(
          bang::PlayCardAction(HandCard(state, 1, bang::CardName::kColt)));
      state.ApplyAction(bang::kTakeHitAction);
      if (hit < 3) {
        Require(state.CurrentPhase() == bang::Phase::kRespondToHit,
                "Alan counters every loss before his last life");
        state.ApplyAction(bang::kTakeHitAction);
      }
    }
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1 &&
                state.Players()[0].positions[0].character_id == -1 &&
                state.Players()[1].positions[outlaw_position].life == outlaw_life - 3,
            "Alan's last life does not create an additional counter-hit");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kAnnieOakey),
        static_cast<int>(bang::CharacterName::kAlanPinkertoon)};
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kDaltonBros),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kGeneralStore, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kMissed, bang::CardName::kCarbine,
         bang::CardName::kHat, bang::CardName::kReturnFire,
         bang::CardName::kColt});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kStealing, bang::CardName::kBeer,
         bang::CardName::kMissed, bang::CardName::kDynamite,
         bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kGeneralStore),
        bang::Target::kOwnActive));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kDynamite));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kHat));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kKnife));
    state.ApplyAction(state.LegalActions().front());
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kCarbine)));
    state.ApplyAction(bang::kTakeHitAction);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kDynamite),
        bang::Target::kOwnRearguard));
    EndCurrentTurn(state);
    EndCurrentTurn(state);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 1,
            "Alan's RC ability triggers once per non-last Dynamite damage");
    const auto observation = state.ObservationTensor(1);
    Require(observation[bang::obs_layout::kContinuationHitCountOffset] == 2.0F / 8.0F,
            "remaining Alan counter-hits are policy-visible");
    for (int counter = 0; counter < 3; ++counter) {
      state.ApplyAction(bang::kTakeHitAction);
    }
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 0 &&
                state.Players()[0].positions[1].life == 1 &&
                state.Players()[1]
                        .positions[state.Players()[1].active_position]
                        .character_id ==
                    static_cast<int>(bang::CharacterName::kBabeLeroy) &&
                state.Players()[1]
                        .positions[state.Players()[1].active_position]
                        .life == 3,
            "repeated counters retarget a replacement AC before the turn draw");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kAnnieOakey),
        static_cast<int>(bang::CharacterName::kAlanPinkertoon)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kGatling, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kStrongAle});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kGatling),
        bang::Target::kOwnActive));
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 0,
            "Gatling reaches Alan in the acting player's RC position");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPlayer() == 1,
            "Gatling continues to the opposing RC before Alan's counter");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPlayer() == 1 &&
                state.ObservationTensor(1)
                    [bang::obs_layout::kPendingSourceCharacterOffset +
                     static_cast<int>(bang::CharacterName::kAlanPinkertoon)] == 1.0F,
            "Alan's counter begins only after every Gatling target resolves");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 0,
            "the deferred Alan counter returns to the Gatling player's turn");
    RequireCardConservation(state);
  }
}

void TestJangoAndOpponentDamageReactions() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kJango),
        static_cast<int>(bang::CharacterName::kWyattEar)};
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    const auto hand_before = state.Players()[0].hand.size();
    state.ApplyAction(bang::kSwapAction);
    const auto observation = state.ObservationTensor(0);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.CurrentPlayer() == 0 &&
                observation[bang::obs_layout::kPendingAbilityOffset +
                            static_cast<int>(bang::CharacterName::kJango)] == 1.0F &&
                observation[bang::obs_layout::kMovementActiveOffset] == 1.0F,
            "Jango may react when the opponent moves its AC token");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(state.Players()[0].hand.size() == hand_before + 1 &&
                state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1,
            "Jango's optional draw resumes the moving opponent's turn");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kJango),
        static_cast<int>(bang::CharacterName::kWyattEar)};
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kBabeLeroy),
        static_cast<int>(bang::CharacterName::kTomThorn)};
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    state.ApplyAction(bang::kSwapAction);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 0,
            "current-player Tom resolves before the opposing AC's Jango reaction");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.CurrentPlayer() == 0 &&
                HasAction(state, bang::kSkipOptionalAction),
            "Jango's optional draw follows Tom's completed hit");
    state.ApplyAction(bang::kSkipOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1,
            "the Tom/Jango chain resumes the moving player's turn");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kDalonRanger),
        static_cast<int>(bang::CharacterName::kWyattEar)};
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kColt, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kStealing,
         bang::CardName::kBeer, bang::CardName::kKnife,
         bang::CardName::kMissed});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    const auto hand_before = state.Players()[0].hand.size();
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 1, bang::CardName::kColt)));
    state.ApplyAction(bang::kTakeHitAction);
    const auto observation = state.ObservationTensor(0);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.CurrentPlayer() == 0 &&
                HasAction(state, bang::kSkipOptionalAction) &&
                HasAction(state, bang::kAcceptOptionalAction) &&
                observation[bang::obs_layout::kPendingAbilityStageOffset] == 1.0F &&
                observation[bang::obs_layout::kDamageReactionResumePlayerOffset] == 0.0F,
            "Dalon Ranger gets a policy-visible optional draw after opponent damage");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(state.Players()[0].hand.size() == hand_before + 1 &&
                state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1,
            "Dalon's draw returns control to the attacking player");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kDalonRanger),
        static_cast<int>(bang::CharacterName::kWyattEar)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kGeneralStore, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kMissed, bang::CardName::kCarbine,
         bang::CardName::kStrongAle, bang::CardName::kHat});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kStealing, bang::CardName::kBeer,
         bang::CardName::kMissed, bang::CardName::kKnife,
         bang::CardName::kClock});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kGeneralStore),
        bang::Target::kOwnActive));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kHat));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kStrongAle));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kKnife));
    state.ApplyAction(state.LegalActions().front());
    EndCurrentTurn(state);
    const auto hand_before = state.Players()[0].hand.size();
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kStrongAle),
        bang::Target::kOpponentActive));
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.ObservationTensor(0)
                    [bang::obs_layout::kPendingAbilityStageOffset] == 2.0F,
            "Dalon gets one optional reaction for each life Strong Ale removes");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.ObservationTensor(0)
                    [bang::obs_layout::kPendingAbilityStageOffset] == 1.0F,
            "multi-life reactions are resolved as separate policy choices");
    state.ApplyAction(bang::kSkipOptionalAction);
    Require(state.Players()[0].hand.size() == hand_before + 1 &&
                state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1,
            "accepting then skipping Dalon's reactions resumes Strong Ale once");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kSlimPoet),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kColt, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kStrongAle});
    bang::GameState state(config);
    CompleteSetup(state);
    const auto hand_before = state.Players()[0].hand.size();
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kColt)));
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.CurrentPlayer() == 1,
            "Slim Poet may react to a non-last life lost to the opponent");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(HasAction(state, bang::kChooseLawBackAction) &&
                state.ObservationTensor(1)
                        [bang::obs_layout::
                             kDamageReactionChoosingOriginOffset] == 1.0F,
            "Slim may distinguish the available card backs before randomizing");
    state.ApplyAction(bang::kChooseLawBackAction);
    Require(state.Players()[0].hand.size() == hand_before - 2 &&
                state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 0,
            "Slim randomly discards one opposing hand card after the played hit");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kDalonRanger),
        static_cast<int>(bang::CharacterName::kWyattEar)};
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kIndians, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kStealing,
         bang::CardName::kBeer, bang::CardName::kKnife,
         bang::CardName::kMissed});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kIndians),
        bang::Target::kOwnActive));
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 1,
            "Dalon's reaction waits while Indians resolves its next target");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPlayer() == 0,
            "Indians reaches the opposing RC before deferred reactions");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.CurrentPlayer() == 0,
            "Dalon's deferred draw begins after every Indians target");
    state.ApplyAction(bang::kSkipOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1,
            "skipping Dalon's deferred reaction resumes the Indians player's turn");
    RequireCardConservation(state);
  }
}

void TestPatGarretPublicSecondDraw() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kPatGarret),
        static_cast<int>(bang::CharacterName::kWyattEar)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kDerringer, bang::CardName::kHat,
         bang::CardName::kStrongAle, bang::CardName::kCarbine});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kStealing, bang::CardName::kBeer,
         bang::CardName::kMissed, bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    const auto opponent_view = state.ObservationTensor(1);
    const int known_opponent_offset = bang::obs_layout::kPublicHandCardsOffset +
                                      bang::obs_layout::kPublicHandBlockSize;
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.CurrentPlayer() == 0 &&
                opponent_view[known_opponent_offset +
                              static_cast<int>(bang::CardName::kCarbine)] == 0.125F,
            "Pat Garret publicly shows the second card drawn in his draw phase");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.Players()[0].hand.size() == 7 &&
                bang::GetCardInstance(
                    HandCard(state, 0, bang::CardName::kKnife)).origin ==
                    bang::Side::kOutlaw,
            "a Horseshoe lets Pat optionally draw from the opponent's deck");
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kCarbine)));
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.ObservationTensor(1)
                [known_opponent_offset +
                 static_cast<int>(bang::CardName::kCarbine)] == 0.0F,
            "public hand knowledge is removed when Pat plays the shown card");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kPatGarret),
        static_cast<int>(bang::CharacterName::kWyattEar)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kDerringer, bang::CardName::kHat,
         bang::CardName::kStrongAle, bang::CardName::kSchoolmarm});
    bang::GameState state(config);
    CompleteSetup(state);
    const int known_opponent_offset = bang::obs_layout::kPublicHandCardsOffset +
                                      bang::obs_layout::kPublicHandBlockSize;
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.ObservationTensor(1)
                    [known_opponent_offset +
                     static_cast<int>(bang::CardName::kSchoolmarm)] == 0.125F,
            "Pat's non-Horseshoe second draw remains publicly known without a choice");
    RequireCardConservation(state);
  }
}

void TestSidCurryRevealAndDelayedCounter() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kTexKiller),
        static_cast<int>(bang::CharacterName::kAlanPinkertoon)};
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kSidCurry),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kColt, bang::CardName::kWagon,
         bang::CardName::kDerringer, bang::CardName::kHat,
         bang::CardName::kStrongAle, bang::CardName::kMissed,
         bang::CardName::kWater});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kStealing, bang::CardName::kBeer,
         bang::CardName::kMissed});
    bang::GameState state(config);
    CompleteSetup(state);
    const int law_position = state.Players()[0].active_position;
    const int sid_position = state.Players()[1].active_position;
    const int law_life = state.Players()[0].positions[law_position].life;
    const int sid_life = state.Players()[1].positions[sid_position].life;
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kColt)));
    Require(HasAction(state, bang::UseAbilityAction(
                                 bang::CharacterName::kSidCurry)),
            "Sid Curry may explicitly reveal when targeted by a hit");
    state.ApplyAction(
        bang::UseAbilityAction(bang::CharacterName::kSidCurry));
    const auto pending = state.ObservationTensor(1);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 1 &&
                pending[bang::obs_layout::kPendingAvoidsPlayedOffset] == 1.0F &&
                pending[bang::obs_layout::kPendingCounterCharacterOffset +
                        static_cast<int>(bang::CharacterName::kSidCurry)] == 1.0F,
            "Sid's successful reveal waits for Tex Killer's additional card");
    state.ApplyAction(bang::AvoidHitAction(
        HandCard(state, 1, bang::CardName::kWater)));
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 0,
            "Sid's counter begins only after Tex's hit is completely avoided");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.Players()[0].positions[law_position].life == law_life - 1 &&
                state.Players()[1].positions[sid_position].life == sid_life &&
                state.CurrentPhase() == bang::Phase::kPlay,
            "Sid's delayed counter damages the original shooter and resumes play");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kSidCurry),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kBarrel, bang::CardName::kColt,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kStrongAle,
         bang::CardName::kGunBelt});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kStealing, bang::CardName::kBeer,
         bang::CardName::kKnife, bang::CardName::kWater});
    bang::GameState state(config);
    CompleteSetup(state);
    const int barrel = HandCard(state, 0, bang::CardName::kBarrel);
    state.ApplyAction(
        bang::PlayCardAction(barrel, bang::Target::kOpponentActive));
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kColt)));
    state.ApplyAction(
        bang::UseAbilityAction(bang::CharacterName::kSidCurry));
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                !HasAction(state, bang::UseAbilityAction(
                                      bang::CharacterName::kSidCurry)) &&
                HasAction(state, bang::AvoidHitAction(barrel)),
            "after Sid fails, Barrel remains available but Sid cannot retry");
    state.ApplyAction(bang::AvoidHitAction(barrel));
    Require(state.CurrentPhase() == bang::Phase::kPlay,
            "a successful Barrel can still avoid the hit after Sid fails");
    RequireCardConservation(state);
  }
}

void TestWyattEarMissedBangReactions() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kWyattEar),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kColt, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kStrongAle});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kMissed, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kStealing,
         bang::CardName::kBeer});
    bang::GameState state(config);
    CompleteSetup(state);
    const auto hand_before = state.Players()[0].hand.size();
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kColt)));
    state.ApplyAction(bang::AvoidHitAction(
        HandCard(state, 1, bang::CardName::kMissed)));
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.CurrentPlayer() == 0 &&
                HasAction(state, bang::kSkipOptionalAction) &&
                HasAction(state, bang::kAcceptOptionalAction),
            "Wyatt Ear may draw after the opponent misses his BANG");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 0 &&
                state.Players()[0].hand.size() == hand_before,
            "accepting Wyatt's reaction draws once and resumes play");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kWyattEar),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kGeneralStore, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kStrongAle,
         bang::CardName::kMissed, bang::CardName::kCarbine});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kMissed, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kStealing,
         bang::CardName::kBeer, bang::CardName::kShotgun,
         bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kGeneralStore),
        bang::Target::kOwnActive));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kShotgun));
    while (state.CurrentPhase() == bang::Phase::kChooseGeneralStore) {
      state.ApplyAction(state.LegalActions().front());
    }
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kShotgun)));
    state.ApplyAction(bang::AvoidHitAction(
        HandCard(state, 1, bang::CardName::kMissed)));
    state.ApplyAction(bang::kChooseOwnSwapAction);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.Players()[0]
                        .positions[state.Players()[0].active_position]
                        .character_id ==
                    static_cast<int>(bang::CharacterName::kAnnieOakey),
            "Wyatt's reaction remains pending when Shotgun moves him to RC");
    state.ApplyAction(bang::kSkipOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 0,
            "the captured Wyatt reaction resumes after Shotgun movement");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kWyattEar),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kGeneralStore, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kStrongAle,
         bang::CardName::kMissed, bang::CardName::kCarbine});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kMissed, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kStealing,
         bang::CardName::kBeer, bang::CardName::kRemington,
         bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kGeneralStore),
        bang::Target::kOwnActive));
    state.ApplyAction(SelectActionFor(state, bang::CardName::kRemington));
    while (state.CurrentPhase() == bang::Phase::kChooseGeneralStore) {
      state.ApplyAction(state.LegalActions().front());
    }
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kRemington)));
    state.ApplyAction(bang::AvoidHitAction(
        HandCard(state, 1, bang::CardName::kMissed)));
    const auto hand_before_draws = state.Players()[0].hand.size();
    Require(state.CurrentPhase() == bang::Phase::kChooseRemington,
            "Remington's own optional draw resolves before Wyatt Ear");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.Players()[0].hand.size() == hand_before_draws + 1,
            "Remington draws once before Wyatt's separate choice");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.Players()[0].hand.size() == hand_before_draws + 2,
            "a missed Remington can draw twice through independent choices");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kWyattEar),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kBank, bang::CardName::kHat,
         bang::CardName::kFanning, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kMissed, bang::CardName::kMissed,
         bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kStealing});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kBank),
        bang::Target::kOpponentActive));
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kHat),
        bang::Target::kOpponentActive));
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kFanning)));
    state.ApplyAction(bang::AvoidHitAction(
        HandCard(state, 1, bang::CardName::kMissed)));
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit,
            "Wyatt's first Fanning reaction waits for the remaining hit");
    state.ApplyAction(bang::AvoidHitAction(
        HandCard(state, 1, bang::CardName::kMissed)));
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility,
            "Wyatt's deferred reactions begin after all Fanning hits");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility,
            "each missed Fanning hit creates a separate Wyatt choice");
    state.ApplyAction(bang::kSkipOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 0,
            "Wyatt's Fanning reactions resume the attacking player's turn");
    RequireCardConservation(state);
  }
}

void TestPearlHatActionReplication() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kBillTightman),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kPearlHat),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kStrongAle, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kMissed});
    bang::GameState state(config);
    CompleteSetup(state);
    const int source = HandCard(state, 0, bang::CardName::kStrongAle);
    state.ApplyAction(bang::PlayCardAction(
        source, bang::Target::kOpponentActive));
    EndCurrentTurn(state);
    Require(HasAction(state, bang::UseAbilityAction(
                                 bang::CharacterName::kPearlHat)),
            "Pearl Hat may replicate the Action on top of the discard pile");
    Require(state.ObservationTensor(1)
                    [bang::obs_layout::kTopDiscardNameOffset +
                     static_cast<int>(bang::CardName::kStrongAle)] == 1.0F,
            "the public top discard is visible before committing Pearl's cost");
    state.ApplyAction(
        bang::UseAbilityAction(bang::CharacterName::kPearlHat));
    auto observation = state.ObservationTensor(1);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                observation[bang::obs_layout::kPendingReplicatedCardOffset +
                            static_cast<int>(bang::CardName::kStrongAle)] == 1.0F,
            "Pearl's captured discard-top Action is policy-visible");
    const int cost = state.Players()[1].hand.front();
    state.ApplyAction(bang::DiscardCardAction(cost));
    Require(HasAction(state, bang::PlayCardAction(
                                 source, bang::Target::kOpponentActive)),
            "Pearl chooses among the copied Action's legal targets");
    const int law_active = state.Players()[0].active_position;
    state.ApplyAction(bang::PlayCardAction(
        source, bang::Target::kOpponentActive));
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.Players()[0].positions[law_active].life == 3 &&
                !HasAction(state, bang::UseAbilityAction(
                                      bang::CharacterName::kPearlHat)),
            "Pearl resolves the copied effect without playing its physical card");
    Require(std::find(state.DiscardPile().begin(), state.DiscardPile().end(),
                      source) != state.DiscardPile().end(),
            "the replicated discard card remains in the discard pile");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kBillTightman),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kPearlHat),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kColt, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kStrongAle});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kMissed, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kStealing,
         bang::CardName::kBeer});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kColt)));
    state.ApplyAction(bang::AvoidHitAction(
        HandCard(state, 1, bang::CardName::kMissed)));
    EndCurrentTurn(state);
    Require(!HasAction(state, bang::UseAbilityAction(
                                  bang::CharacterName::kPearlHat)),
            "Pearl cannot replicate Missed or Return Fire cards");
    RequireCardConservation(state);
  }
}

void TestBillTightmanAndBullAndersonEliminations() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kAnnieOakey),
        static_cast<int>(bang::CharacterName::kTheStranger)};
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kBullAnderson),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kHat, bang::CardName::kWater,
         bang::CardName::kStrongAle, bang::CardName::kWagon,
         bang::CardName::kDerringer, bang::CardName::kMissed});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kShotgun, bang::CardName::kKnife,
         bang::CardName::kKnife, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kMissed,
         bang::CardName::kBeer});
    bang::GameState state(config);
    CompleteSetup(state);
    const int hat = HandCard(state, 0, bang::CardName::kHat);
    state.ApplyAction(
        bang::PlayCardAction(hat, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kWater),
        bang::Target::kOwnActive));
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kStrongAle),
        bang::Target::kOwnActive));
    const int eliminated_position = state.Players()[0].active_position;
    EndCurrentTurn(state);
    for (const auto name : {bang::CardName::kShotgun,
                            bang::CardName::kKnife,
                            bang::CardName::kKnife}) {
      state.ApplyAction(
          bang::PlayCardAction(HandCard(state, 1, name)));
      state.ApplyAction(bang::kTakeHitAction);
    }
    const auto pending = state.ObservationTensor(1);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.CurrentPlayer() == 1 &&
                pending[bang::obs_layout::kPendingEliminationOffset] == 1.0F &&
                HasAction(state, bang::SelectCardAction(hat)),
            "Bull Anderson chooses before eliminated Equipment is discarded");
    state.ApplyAction(bang::SelectCardAction(hat));
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                HasAction(state, bang::kChooseLawBackAction),
            "Bull may combine Equipment and a random hand draw");
    state.ApplyAction(bang::kChooseLawBackAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1 &&
                state.Players()[0]
                        .positions[static_cast<std::size_t>(
                            eliminated_position)]
                        .character_id == -1 &&
                std::find(state.Players()[1].hand.begin(),
                          state.Players()[1].hand.end(),
                          hat) != state.Players()[1].hand.end(),
            "Bull takes two cards before normal elimination cleanup and reward");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kBillTightman),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kJackRansome),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kStrongAle, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kMissed});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kShotgun, bang::CardName::kKnife,
         bang::CardName::kKnife, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kMissed,
         bang::CardName::kBeer});
    bang::GameState state(config);
    CompleteSetup(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kStrongAle),
        bang::Target::kOwnRearguard));
    const auto hand_before = state.Players()[0].hand.size();
    const int rear = 1 - state.Players()[0].active_position;
    EndCurrentTurn(state);
    for (const auto name : {bang::CardName::kShotgun,
                            bang::CardName::kKnife,
                            bang::CardName::kKnife}) {
      state.ApplyAction(bang::PlayCardAction(
          HandCard(state, 1, name), bang::Target::kOpponentRearguard));
      state.ApplyAction(bang::kTakeHitAction);
    }
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.CurrentPlayer() == 0 &&
                state.Players()[0].hand.size() == hand_before + 2,
            "Bill's RC elimination first grants the normal two-card reward");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1 &&
                state.Players()[0].hand.size() == hand_before + 4 &&
                state.Players()[0]
                        .positions[static_cast<std::size_t>(rear)]
                        .character_id == -1,
            "Bill may draw two more cards when another character is eliminated");
    RequireCardConservation(state);
  }
}

void TestBuffaloBellDamageRedirection() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kBuffaloBell),
        static_cast<int>(bang::CharacterName::kBillTightman)};
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kJackRansome),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kKnife, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kStealing,
         bang::CardName::kBeer});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    const int buffalo = state.Players()[0].active_position;
    const int rear = 1 - buffalo;
    const int buffalo_life = state.Players()[0].positions[buffalo].life;
    const int rear_life = state.Players()[0].positions[rear].life;
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kKnife),
        bang::Target::kOpponentRearguard));
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                !HasAction(state, bang::UseAbilityAction(
                                      bang::CharacterName::kBuffaloBell)),
            "Buffalo Bell cannot avoid a hit for the other character");
    state.ApplyAction(bang::kTakeHitAction);
    const auto pending = state.ObservationTensor(0);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                pending[bang::obs_layout::kPendingDamageOffset] == 1.0F &&
                HasAction(state, bang::kAcceptOptionalAction),
            "Buffalo may redirect the resulting life loss");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1 &&
                state.Players()[0].positions[buffalo].life ==
                    buffalo_life - 1 &&
                state.Players()[0].positions[rear].life == rear_life,
            "redirected damage is lost by Buffalo instead of the target");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kBuffaloBell),
        static_cast<int>(bang::CharacterName::kBillTightman)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kStrongAle, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kMissed});
    bang::GameState state(config);
    CompleteSetup(state);
    const int buffalo = state.Players()[0].active_position;
    const int rear = 1 - buffalo;
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kStrongAle),
        bang::Target::kOwnRearguard));
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.ObservationTensor(0)
                        [bang::obs_layout::kDamageRemainingOffset] == 2.0F,
            "multi-point life loss exposes each redirectable point");
    state.ApplyAction(bang::kAcceptOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                state.ObservationTensor(0)
                        [bang::obs_layout::kDamageRemainingOffset] == 1.0F,
            "Buffalo may redirect only part of a multi-point loss");
    state.ApplyAction(bang::kSkipOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.Players()[0].positions[buffalo].life == 3 &&
                state.Players()[0].positions[rear].life == 4,
            "remaining damage returns to the originally selected character");
    RequireCardConservation(state);
  }
}

void TestWildBillCardRemovalCounters() {
  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kWildBill),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kStealing, bang::CardName::kWater,
         bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    const int target = state.Players()[1].active_position;
    const int life = state.Players()[1].positions[target].life;
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kStealing),
        bang::Target::kOwnActive));
    state.ApplyAction(bang::kChooseLawBackAction);
    const auto counter = state.ObservationTensor(1);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 1 &&
                counter[bang::obs_layout::kPendingSourceCharacterOffset +
                        static_cast<int>(
                            bang::CharacterName::kWildBill)] == 1.0F,
            "drawing from active Wild Bill's hand creates his automatic BANG");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1 &&
                state.Players()[1].positions[target].life == life - 1,
            "Wild Bill's counter resolves before Stealing returns to play");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kWildBill),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kSchoolmarm, bang::CardName::kHat,
         bang::CardName::kBank, bang::CardName::kGunBelt,
         bang::CardName::kScope, bang::CardName::kStampede});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kStealing, bang::CardName::kWater,
         bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kKnife});
    bang::GameState state(config);
    state.ApplyAction(bang::kChooseActiveRightAction);
    state.ApplyAction(bang::kChooseActiveLeftAction);
    const int hat = HandCard(state, 0, bang::CardName::kHat);
    const int wild_position = 1 - state.Players()[0].active_position;
    state.ApplyAction(
        bang::PlayCardAction(hat, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kBank),
        bang::Target::kOwnRearguard));
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kGunBelt),
        bang::Target::kOwnRearguard));
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kScope),
        bang::Target::kOwnRearguard));
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kStampede),
        bang::Target::kOpponentActive));
    state.ApplyAction(bang::kSwapAction);
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kStealing),
        bang::Target::kOwnActive));
    state.ApplyAction(bang::kChooseLawBackAction);
    state.ApplyAction(bang::kTakeHitAction);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kSchoolmarm),
        bang::Target::kOwnActive));
    state.ApplyAction(bang::SelectCardAction(hat));
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                !CharacterHasEquipment(state, 0, wild_position,
                                       bang::CardName::kHat) &&
                std::find(state.Players()[1].hand.begin(),
                          state.Players()[1].hand.end(),
                          hat) != state.Players()[1].hand.end(),
            "drawing Equipment played with Wild Bill preserves the transfer "
            "while starting his counter");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1,
            "Schoolmarm's suspended choice resumes exactly once");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kWildBill),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kHat, bang::CardName::kWater,
         bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kDerringer, bang::CardName::kStrongAle});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kHat, bang::CardName::kWater,
         bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kKnife});
    bang::GameState state(config);
    state.ApplyAction(bang::kChooseActiveRightAction);
    state.ApplyAction(bang::kChooseActiveLeftAction);
    const int old_hat = HandCard(state, 0, bang::CardName::kHat);
    state.ApplyAction(
        bang::PlayCardAction(old_hat, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    EndCurrentTurn(state);
    const int new_hat = HandCard(state, 1, bang::CardName::kHat);
    state.ApplyAction(bang::PlayCardAction(
        new_hat, bang::Target::kOpponentActive));
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                std::find(state.DiscardPile().begin(),
                          state.DiscardPile().end(),
                          old_hat) != state.DiscardPile().end(),
            "an opponent replacing Equipment with Wild Bill discards the old "
            "card and triggers his BANG");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1,
            "duplicate Equipment placement resumes after Wild Bill's counter");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kWildBill),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kHat, bang::CardName::kMissed,
         bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kDerringer, bang::CardName::kStrongAle});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kCarbine, bang::CardName::kWater,
         bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kKnife});
    bang::GameState state(config);
    state.ApplyAction(bang::kChooseActiveRightAction);
    state.ApplyAction(bang::kChooseActiveLeftAction);
    const int hat = HandCard(state, 0, bang::CardName::kHat);
    state.ApplyAction(
        bang::PlayCardAction(hat, bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kCarbine)));
    state.ApplyAction(bang::AvoidHitAction(
        HandCard(state, 0, bang::CardName::kMissed)));
    state.ApplyAction(bang::DiscardCardAction(hat));
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 1,
            "Carbine discarding Equipment with Wild Bill triggers his BANG");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1,
            "the Carbine continuation resumes after Wild Bill's counter");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kWildBill),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kPeacemaker, bang::CardName::kMissed,
         bang::CardName::kWater, bang::CardName::kBank,
         bang::CardName::kGunBelt, bang::CardName::kScope});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kStealing, bang::CardName::kMissed,
         bang::CardName::kWater, bang::CardName::kWagon,
         bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    for (const auto name : {bang::CardName::kBank,
                            bang::CardName::kGunBelt,
                            bang::CardName::kScope}) {
      state.ApplyAction(bang::PlayCardAction(
          HandCard(state, 0, name), bang::Target::kOwnRearguard));
    }
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kStealing),
        bang::Target::kOwnActive));
    state.ApplyAction(bang::kChooseLawBackAction);
    state.ApplyAction(bang::kTakeHitAction);
    const int wild_position = state.Players()[0].active_position;
    const int wild_life =
        state.Players()[0].positions[wild_position].life;
    const int outlaw_position = state.Players()[1].active_position;
    const int outlaw_life =
        state.Players()[1].positions[outlaw_position].life;
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kPeacemaker)));
    state.ApplyAction(bang::AvoidHitAction(
        HandCard(state, 0, bang::CardName::kMissed)));
    state.ApplyAction(bang::DiscardCardAction(
        HandCard(state, 0, bang::CardName::kWater)));
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 1,
            "Peacemaker's opponent-forced discard triggers Wild Bill");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1 &&
                state.Players()[0].positions[wild_position].life ==
                    wild_life &&
                state.Players()[1].positions[outlaw_position].life ==
                    outlaw_life - 1,
            "Wild Bill's counter returns to the original Peacemaker "
            "continuation exactly once");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kWildBill),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kSlimPoet),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kColt, bang::CardName::kWater,
         bang::CardName::kWagon, bang::CardName::kDerringer,
         bang::CardName::kHat, bang::CardName::kStrongAle});
    bang::GameState state(config);
    CompleteSetup(state);
    const auto hand_before = state.Players()[0].hand.size();
    state.ApplyAction(
        bang::PlayCardAction(HandCard(state, 0, bang::CardName::kColt)));
    state.ApplyAction(bang::kTakeHitAction);
    state.ApplyAction(bang::kAcceptOptionalAction);
    state.ApplyAction(bang::kChooseLawBackAction);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.CurrentPlayer() == 1,
            "Slim Poet discarding from Wild Bill suspends his damage reaction");
    state.ApplyAction(bang::kTakeHitAction);
    const auto nested = state.ObservationTensor(1);
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                nested[bang::obs_layout::kDamageReactionDepthOffset] == 0.5F,
            "a counter that damages Slim safely nests his new reaction");
    state.ApplyAction(bang::kSkipOptionalAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 0 &&
                state.Players()[0].hand.size() == hand_before - 2,
            "nested Slim reactions unwind to the original hit once");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kWildBill),
        static_cast<int>(bang::CharacterName::kAnnieOakey)};
    config.character_rosters[1] = {
        static_cast<int>(bang::CharacterName::kBullAnderson),
        static_cast<int>(bang::CharacterName::kBabeLeroy)};
    config.starting_decks[0] = DeckWithTop(
        bang::Side::kLaw,
        {bang::CardName::kGunBelt, bang::CardName::kStrongAle,
         bang::CardName::kColt, bang::CardName::kDerringer,
         bang::CardName::kStampede, bang::CardName::kWagon});
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kGunBelt, bang::CardName::kScope,
         bang::CardName::kShotgun, bang::CardName::kColt,
         bang::CardName::kRemington, bang::CardName::kSchofield,
         bang::CardName::kBeer});
    bang::GameState state(config);
    state.ApplyAction(bang::kChooseActiveRightAction);
    state.ApplyAction(bang::kChooseActiveLeftAction);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kGunBelt),
        bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kStrongAle),
        bang::Target::kOpponentActive));
    for (const auto name : {bang::CardName::kColt,
                            bang::CardName::kDerringer}) {
      state.ApplyAction(
          bang::PlayCardAction(HandCard(state, 0, name)));
      state.ApplyAction(bang::kTakeHitAction);
    }
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 0, bang::CardName::kStampede),
        bang::Target::kOpponentActive));
    Require(state.Players()[1]
                .positions[static_cast<std::size_t>(
                    state.Players()[1].active_position)]
                .life == 1,
            "Bull is prepared for a lethal Wild Bill counter");
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kGunBelt),
        bang::Target::kOwnRearguard));
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kScope),
        bang::Target::kOwnRearguard));
    state.ApplyAction(bang::kSwapAction);
    const int law_rear = 1 - state.Players()[0].active_position;
    for (const auto name : {bang::CardName::kShotgun,
                            bang::CardName::kColt,
                            bang::CardName::kRemington,
                            bang::CardName::kSchofield}) {
      state.ApplyAction(bang::PlayCardAction(
          HandCard(state, 1, name),
          bang::Target::kOpponentRearguard));
      state.ApplyAction(bang::kTakeHitAction);
    }
    Require(state.CurrentPhase() == bang::Phase::kChooseAbility &&
                HasAction(state, bang::kChooseLawBackAction),
            "Bull may choose Wild Bill's hand before elimination cleanup");
    state.ApplyAction(bang::kChooseLawBackAction);
    Require(state.CurrentPhase() == bang::Phase::kRespondToHit &&
                state.ObservationTensor(1)
                        [bang::obs_layout::kEliminationDepthOffset] == 0.25F,
            "Wild Bill's counter suspends Bull's elimination choice");
    state.ApplyAction(bang::kTakeHitAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1 &&
                state.Players()[0].positions[law_rear].character_id == -1 &&
                state.Players()[1]
                        .positions[static_cast<std::size_t>(
                            1 - state.Players()[1].active_position)]
                        .character_id == -1,
            "a lethal counter resolves nested Bull and original eliminations "
            "without corrupting either continuation");
    RequireCardConservation(state);
  }

  {
    auto config = DeterministicConfig();
    config.character_rosters[0] = {
        static_cast<int>(bang::CharacterName::kAnnieOakey),
        static_cast<int>(bang::CharacterName::kWildBill)};
    config.starting_decks[1] = DeckWithTop(
        bang::Side::kOutlaw,
        {bang::CardName::kStealing, bang::CardName::kWater,
         bang::CardName::kMissed, bang::CardName::kWagon,
         bang::CardName::kKnife});
    bang::GameState state(config);
    CompleteSetup(state);
    EndCurrentTurn(state);
    state.ApplyAction(bang::PlayCardAction(
        HandCard(state, 1, bang::CardName::kStealing),
        bang::Target::kOwnActive));
    state.ApplyAction(bang::kChooseLawBackAction);
    Require(state.CurrentPhase() == bang::Phase::kPlay &&
                state.CurrentPlayer() == 1,
            "Wild Bill does not react while he is the rearguard");
    RequireCardConservation(state);
  }
}

void TestSwapAndDiscardTurnFlow() {
  bang::GameState state(DeterministicConfig());
  CompleteSetup(state);
  auto legal = state.LegalActions();
  Require(std::find(legal.begin(), legal.end(), bang::kSwapAction) != legal.end(),
          "one optional swap is legal");
  state.ApplyAction(bang::kSwapAction);
  legal = state.LegalActions();
  Require(std::find(legal.begin(), legal.end(), bang::kSwapAction) == legal.end(),
          "a second voluntary swap is illegal");

  state.ApplyAction(bang::kEndPlayAction);
  Require(state.CurrentPhase() == bang::Phase::kDiscard, "excess hand enters discard phase");
  while (state.CurrentPhase() == bang::Phase::kDiscard) {
    const auto discard_actions = state.LegalActions();
    Require(!discard_actions.empty(), "mandatory discard has a legal choice");
    state.ApplyAction(discard_actions.front());
  }
  Require(state.TurnPlayer() == 1, "turn passes to Outlaw after discarding");
  Require(state.CurrentPhase() == bang::Phase::kPlay, "Outlaw starts in play phase");
  Require(state.Players()[0].hand.size() == 4, "hand is reduced to active life");
  Require(state.Players()[1].hand.size() == 7, "Outlaw draws two cards");
}

void TestColtHitAndElimination() {
  bang::GameState state(DeterministicConfig());
  CompleteSetup(state);

  int decisions = 0;
  int previous_total = 0;
  for (const auto& player : state.Players()) {
    for (const auto& character : player.positions) previous_total += character.life;
  }
  bool saw_hit = false;
  while (!state.IsTerminal() && decisions < 5000) {
    const auto legal = state.LegalActions();
    Require(!legal.empty(), "non-terminal states always have a legal action");
    int choice = legal.front();
    if (state.CurrentPhase() == bang::Phase::kRespondToHit) {
      choice = bang::kTakeHitAction;
      saw_hit = true;
    } else {
      const auto play = std::find_if(legal.begin(), legal.end(), [](int action) {
        return bang::DecodeAction(action).kind == bang::ActionKind::kPlayCard;
      });
      if (play != legal.end()) choice = *play;
      else if (std::find(legal.begin(), legal.end(), bang::kEndPlayAction) != legal.end())
        choice = bang::kEndPlayAction;
    }
    state.ApplyAction(choice);
    RequireCardConservation(state);
    ++decisions;
  }
  Require(saw_hit, "a Colt creates a response decision");
  Require(state.IsTerminal(), "repeated Colt hits eventually eliminate one roster");
  Require(state.Winner() == 0 || state.Winner() == 1, "terminal game has one winner");
  const auto returns = state.Returns();
  Require(returns[0] == -returns[1] && returns[0] != 0.0F, "returns are zero-sum");
  (void)previous_total;
}

void TestCommonDeckAndConservation() {
  bang::GameState state(DeterministicConfig(4));
  CompleteSetup(state);
  for (int step = 0; step < 1000 &&
                     !(state.Players()[0].personal_deck_exhausted &&
                       state.Players()[1].personal_deck_exhausted);
       ++step) {
    const auto legal = state.LegalActions();
    int choice = legal.front();
    if (std::find(legal.begin(), legal.end(), bang::kEndPlayAction) != legal.end()) {
      choice = bang::kEndPlayAction;
    }
    state.ApplyAction(choice);
  }
  Require(state.CommonDeckCreated(), "first exhausted personal deck creates common deck");
  Require(state.Players()[0].personal_deck_exhausted &&
              state.Players()[1].personal_deck_exhausted,
          "both players eventually transition to the same common deck");

  RequireCardConservation(state);
}

void TestIllegalActionRejected() {
  bang::GameState state(DeterministicConfig());
  bool rejected = false;
  try {
    state.ApplyAction(bang::kEndPlayAction);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  Require(rejected, "actions outside the legal set are rejected");
  rejected = false;
  try {
    state.ApplyAction(bang::kActionCount + 10);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  Require(rejected, "out-of-range action IDs are rejected consistently");

  const int targeted = bang::PlayCardAction(17, bang::Target::kOpponentRearguard);
  const auto decoded = bang::DecodeAction(targeted);
  Require(decoded.kind == bang::ActionKind::kPlayCard && decoded.card_instance == 17 &&
              decoded.target == static_cast<int>(bang::Target::kOpponentRearguard),
          "target-aware play actions round-trip through stable IDs");
  const int selected = bang::SelectCardAction(42);
  const auto decoded_selection = bang::DecodeAction(selected);
  Require(decoded_selection.kind == bang::ActionKind::kSelectCard &&
              decoded_selection.card_instance == 42 &&
              bang::ActionToString(bang::kChooseLawBackAction) ==
                  "choose_back(law)",
          "selection and visible-card-back actions have stable decodings");
  const int transformed =
      bang::PlayAsColtAction(11, bang::Target::kOpponentRearguard);
  const auto decoded_transformed = bang::DecodeAction(transformed);
  Require(decoded_transformed.kind == bang::ActionKind::kPlayAsColt &&
              decoded_transformed.card_instance == 11 &&
              decoded_transformed.target ==
                  static_cast<int>(bang::Target::kOpponentRearguard),
          "character-transformed Colt actions round-trip through stable IDs");
}

}  // namespace

int main() {
  try {
    TestStaticData();
    TestInputLayouts();
    TestWebAppSmoke();
    TestWebForcedResponses();
    TestWebAbilityPrompts();
    TestWebSessionPersistence();
    TestStaticWebBridge();
    TestPolicyArchitectures();
    TestStaticPolicyInference();
    TestSetupAndHiddenSelection();
    TestObservationDoesNotLeakOpponentHand();
    TestTrainingContract();
    TestSwapAndDiscardTurnFlow();
    TestEquipmentPlacementAndScope();
    TestDuplicateEquipmentReplacement();
    TestRearguardEquipmentWakesWhenTargeted();
    TestHatAvoidsAndIsDiscarded();
    TestResourcesWaitAndUse();
    TestAvoidResources();
    TestReinforcementsRandomPlay();
    TestRenegadeBangAndAvoidCards();
    TestRenegadeActionCards();
    TestSpursSidekickAndSharps();
    TestRicochetAndHitResources();
    TestSlap();
    TestRenegadeCharactersResources();
    TestTheMarshalAndPokerAlicia();
    TestJimLonghairAndMysticMaid();
    TestBarrelRevealSuccessAndFailure();
    TestReturnFireCreatesCounterHit();
    TestRemingtonChoicePrecedesReturnFireCounter();
    TestSupportedWeaponEffects();
    TestPeacemakerCarbineAndShotgunChoices();
    TestAuditedRuleInteractions();
    TestTexKillerRequiresAnAvoidAndAdditionalCard();
    TestTexKillerBarrelAndReturnFireTiming();
    TestTexKillerFanningRequiresTwoCardsPerHit();
    TestGatlingOrderAndDeferredElimination();
    TestIndiansResponseAndOrder();
    TestDuelAlternatingResponse();
    TestGeneralStoreAndWagonChoices();
    TestSchoolmarmAndStealingChoices();
    TestBartChoosesReveal();
    TestBeginningTurnAndClockEquipment();
    TestDynamiteExplosionAndTransfer();
    TestRattlesnakeThenStampedeOrder();
    TestBartChoosesDynamiteReveal();
    TestBeerStrongAleAndWater();
    TestGunBeltAndEquipmentElimination();
    TestBangLimitWithoutGunBelt();
    TestPassiveCharacterDrawAndBangLimits();
    TestAnnieAndTocoCardTransformation();
    TestJackRansomeTargetOverrides();
    TestBabeLeroyAndCattleKatieAbilities();
    TestSoundanceKidAbility();
    TestTomThornSwapTriggeredHitsAndContinuations();
    TestAlanPinkertoonNonLastLifeCounters();
    TestJangoAndOpponentDamageReactions();
    TestWyattEarMissedBangReactions();
    TestPearlHatActionReplication();
    TestBillTightmanAndBullAndersonEliminations();
    TestBuffaloBellDamageRedirection();
    TestWildBillCardRemovalCounters();
    TestPatGarretPublicSecondDraw();
    TestSidCurryRevealAndDelayedCounter();
    TestColtHitAndElimination();
    TestCommonDeckAndConservation();
    TestIllegalActionRejected();
    std::cout << "all tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
