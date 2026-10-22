#include "bang/static_data.hpp"

#include <stdexcept>

namespace bang {
namespace {

using CN = CardName;
using CK = CardKind;
using RS = RevealSymbol;

constexpr std::array<CardData, kCardNameCount> kCards = {{
    {CN::kBank, "bank", "Bank",
     "During your Draw Phase, draw 1 extra card. This is mandatory.",
     CK::kEquipment, false, false, false, false},
    {CN::kBarrel, "barrel", "Barrel",
     "When this character is hit, you may reveal. A Barrel symbol avoids 1 hit.",
     CK::kEquipment, false, false, false, false},
    {CN::kBeer, "beer", "Beer",
     "Your active and rearguard each recover 1 life, up to their maximum.",
     CK::kAction, false, false, false, false},
    {CN::kCarbine, "carbine", "Carbine",
     "Deal 1 hit. If the opponent avoids it, you may discard Equipment from either active character.",
     CK::kBang, true, false, false, false},
    {CN::kClock, "clock", "Clock",
     "Whenever your opponent moves their active token, draw 1 card.",
     CK::kEquipment, false, false, true, false},
    {CN::kColt, "colt", "Colt",
     "Deal 1 avoidable hit.",
     CK::kBang, true, false, false, false},
    {CN::kDerringer, "derringer", "Derringer",
     "Deal 1 hit. The target may use any hand card to avoid it. Not a BANG! card.",
     CK::kAction, true, false, false, false},
    {CN::kDuel, "duel", "Duel",
     "Alternate discarding hit cards. The first player to stop makes their active lose 1 life.",
     CK::kAction, false, false, false, false},
    {CN::kDynamite, "dynamite", "Dynamite",
     "Play on a rearguard. At turn start, reveal: Dynamite means lose 3 life; otherwise pass it.",
     CK::kEquipment, false, false, false, true},
    {CN::kFanning, "fanning", "Fanning",
     "Deal 1 hit for each Equipment on the target. Each hit must be avoided separately.",
     CK::kBang, true, false, false, false},
    {CN::kGatling, "gatling", "Gatling",
     "Deal 1 hit to every character except your active. Not a BANG! card.",
     CK::kAction, true, false, false, false},
    {CN::kGeneralStore, "general_store", "General Store",
     "Reveal 4 cards. Players alternate taking one, starting with you.",
     CK::kAction, false, false, false, false},
    {CN::kGunBelt, "gun_belt", "Gun Belt",
     "While on your active character, your BANG! cards do not count toward the turn limit.",
     CK::kEquipment, false, false, false, false},
    {CN::kHat, "hat", "Hat",
     "Discard this Equipment to avoid 1 hit against this character.",
     CK::kEquipment, false, false, false, false},
    {CN::kIndians, "indians", "Indians!",
     "Every character except your active must discard a hit card or lose 1 life.",
     CK::kAction, false, false, false, false},
    {CN::kKnife, "knife", "Knife",
     "Deal 1 avoidable hit. Not a BANG! card.",
     CK::kAction, true, false, false, false},
    {CN::kMissed, "missed", "Missed!",
     "Avoid 1 hit.",
     CK::kAction, false, true, false, false},
    {CN::kPeacemaker, "peacemaker", "Peacemaker",
     "Deal 1 hit. If the opponent avoids it, they discard a hand card of their choice.",
     CK::kBang, true, false, false, false},
    {CN::kRattlesnake, "rattlesnake", "Rattlesnake",
     "At turn start, reveal. A Snake symbol makes this character lose 1 life.",
     CK::kEquipment, false, false, false, true},
    {CN::kRemington, "remington", "Remington",
     "Deal 1 hit. If the opponent avoids it, you may draw 1 card.",
     CK::kBang, true, false, false, false},
    {CN::kReturnFire, "return_fire", "Return Fire",
     "Avoid 1 hit; if fully avoided, the attacker's active character is hit.",
     CK::kAction, false, true, false, false},
    {CN::kSchoolmarm, "schoolmarm", "Schoolmarm",
     "Take any Equipment in play except from the opponent's rearguard into your hand.",
     CK::kAction, false, false, false, false},
    {CN::kSchofield, "schofield", "Schofield",
     "Deal 1 hit. If the opponent avoids it, your active character recovers 1 life.",
     CK::kBang, true, false, false, false},
    {CN::kScope, "scope", "Scope",
     "Your hit cards may target the opponent's rearguard.",
     CK::kEquipment, false, false, true, false},
    {CN::kShotgun, "shotgun", "Shotgun",
     "Deal 1 hit. If the opponent avoids it, swap either your characters or the opponent's.",
     CK::kBang, true, false, false, false},
    {CN::kStampede, "stampede", "Stampede",
     "At turn start, this character swaps active/rearguard position.",
     CK::kEquipment, false, false, true, true},
    {CN::kStealing, "stealing", "Stealing",
     "Steal a random hand card, or discard eligible Equipment in play.",
     CK::kAction, false, false, false, false},
    {CN::kStrongAle, "strong_ale", "Strong Ale",
     "Set an eligible character to exactly 3 life.",
     CK::kAction, false, false, false, false},
    {CN::kWagon, "wagon", "Wagon",
     "Draw 3 cards, keep 2, and discard the other.",
     CK::kAction, false, false, false, false},
    {CN::kWater, "water", "Water",
     "Swap either player's active and rearguard characters.",
     CK::kAction, false, false, true, false},
    {CN::kWinchester, "winchester", "Winchester",
     "Deal 1 hit. This card may target the opponent's rearguard.",
     CK::kBang, true, false, false, false},
}};

struct CardVariant {
  CardName name;
  RevealSymbol reveal;
};

// Variants follow the printed card-sheet indices. Some cards with the same
// name have different reveal symbols, so instances intentionally retain the
// variant rather than collapsing to a name-only deck.
constexpr std::array<CardVariant, 28> kLawVariants = {{
    {CN::kBank, RS::kBarrel},          {CN::kBarrel, RS::kSnake},
    {CN::kHat, RS::kHorseshoe},        {CN::kGunBelt, RS::kDynamite},
    {CN::kStampede, RS::kBarrel},      {CN::kScope, RS::kHorseshoe},
    {CN::kWater, RS::kSnake},          {CN::kWater, RS::kBarrel},
    {CN::kStrongAle, RS::kBarrel},     {CN::kWagon, RS::kBarrel},
    {CN::kDerringer, RS::kSnake},      {CN::kDerringer, RS::kHorseshoe},
    {CN::kDuel, RS::kDynamite},        {CN::kGeneralStore, RS::kSnake},
    {CN::kGatling, RS::kBarrel},       {CN::kIndians, RS::kHorseshoe},
    {CN::kSchoolmarm, RS::kBarrel},    {CN::kSchoolmarm, RS::kHorseshoe},
    {CN::kMissed, RS::kBarrel},        {CN::kMissed, RS::kSnake},
    {CN::kReturnFire, RS::kHorseshoe}, {CN::kCarbine, RS::kHorseshoe},
    {CN::kColt, RS::kDynamite},        {CN::kPeacemaker, RS::kHorseshoe},
    {CN::kFanning, RS::kBarrel},       {CN::kFanning, RS::kHorseshoe},
    {CN::kShotgun, RS::kDynamite},     {CN::kWinchester, RS::kHorseshoe},
}};

constexpr std::array<int, kCardsPerSide> kLawDeckVariants = {
    5, 16, 7, 24, 23, 15, 27, 27, 21, 11, 3, 14, 6, 0, 19, 19, 17, 12, 2, 1,
    19, 13, 23, 19, 6, 26, 21, 26, 11, 8, 25, 10, 4, 22, 20, 9, 18, 6, 11, 9};

constexpr std::array<CardVariant, 26> kOutlawVariants = {{
    {CN::kBarrel, RS::kSnake},       {CN::kHat, RS::kHorseshoe},
    {CN::kGunBelt, RS::kSnake},      {CN::kDynamite, RS::kBarrel},
    {CN::kScope, RS::kHorseshoe},    {CN::kClock, RS::kHorseshoe},
    {CN::kRattlesnake, RS::kSnake},  {CN::kWater, RS::kBarrel},
    {CN::kBeer, RS::kBarrel},        {CN::kWagon, RS::kBarrel},
    {CN::kDuel, RS::kHorseshoe},     {CN::kGeneralStore, RS::kHorseshoe},
    {CN::kStealing, RS::kBarrel},    {CN::kStealing, RS::kHorseshoe},
    {CN::kIndians, RS::kHorseshoe},  {CN::kMissed, RS::kSnake},
    {CN::kMissed, RS::kBarrel},      {CN::kMissed, RS::kHorseshoe},
    {CN::kKnife, RS::kSnake},        {CN::kKnife, RS::kHorseshoe},
    {CN::kCarbine, RS::kBarrel},     {CN::kColt, RS::kDynamite},
    {CN::kRemington, RS::kSnake},    {CN::kRemington, RS::kBarrel},
    {CN::kSchofield, RS::kBarrel},   {CN::kShotgun, RS::kHorseshoe},
}};

constexpr std::array<int, kCardsPerSide> kOutlawDeckVariants = {
    7, 23, 17, 0, 3, 22, 10, 16, 8, 15, 5, 25, 19, 15, 5, 25, 2, 19, 12, 15,
    24, 24, 15, 4, 11, 15, 21, 21, 21, 21, 6, 20, 21, 13, 13, 13, 1, 14, 18, 9};

constexpr std::array<CardInstanceData, kCardInstanceCount> BuildCardInstances() {
  std::array<CardInstanceData, kCardInstanceCount> result{};
  for (int i = 0; i < kCardsPerSide; ++i) {
    const auto law = kLawVariants[static_cast<std::size_t>(kLawDeckVariants[i])];
    result[i] = {i, Side::kLaw, law.name, law.reveal};
    const auto outlaw = kOutlawVariants[static_cast<std::size_t>(kOutlawDeckVariants[i])];
    result[kCardsPerSide + i] = {
        kCardsPerSide + i, Side::kOutlaw, outlaw.name, outlaw.reveal};
  }
  return result;
}

constexpr auto kCardInstances = BuildCardInstances();

constexpr std::array<CharacterData, kCharacterCount> kCharacters = {{
    {0, Side::kLaw, "alan_pinkertoon", "Alan Pinkertoon",
     "Whenever he loses non-final life, the opponent's active character is hit.",
     4, false},
    {1, Side::kLaw, "annie_oakey", "Annie Oakey",
     "You may use hit cards to avoid, and play Missed! cards as Colts.",
     4, false},
    {2, Side::kLaw, "bart_masterson", "Bart Masterson",
     "Whenever anyone reveals, reveal 2 cards and choose which result applies.",
     4, true},
    {3, Side::kLaw, "bill_tightman", "Bill Tightman",
     "Whenever another character is eliminated, you may draw 2 cards.",
     5, false},
    {4, Side::kLaw, "buffalo_bell", "Buffalo Bell",
     "He may lose non-final life in place of your other character.",
     4, true},
    {5, Side::kLaw, "dalon_ranger", "Dalon Ranger",
     "When the opponent makes him lose non-final life, you may draw 1 card.",
     5, false},
    {6, Side::kLaw, "jango", "Jango",
     "When the opponent moves their active token, you may draw 1 card.",
     3, false},
    {7, Side::kLaw, "pat_garret", "Pat Garret",
     "Show your second Draw Phase card. On Horseshoe, draw from the opponent's deck.",
     5, false},
    {8, Side::kLaw, "tex_killer", "Tex Killer",
     "To avoid his BANG!, the opponent needs an avoid plus 1 additional card.",
     4, false},
    {9, Side::kLaw, "the_stranger", "The Stranger",
     "You may play 1 additional BANG! card during your turn.",
     4, true},
    {10, Side::kLaw, "wild_bill", "Wild Bill",
     "When the opponent removes one of your cards, their active character is hit.",
     5, false},
    {11, Side::kLaw, "wyatt_ear", "Wyatt Ear",
     "Whenever the opponent avoids one of your hits, you may draw 1 card.",
     4, false},
    {12, Side::kOutlaw, "babe_leroy", "Babe Leroy",
     "Once per turn while active and empty-handed, draw 2 cards.",
     4, false},
    {13, Side::kOutlaw, "bull_anderson", "Bull Anderson",
     "When another character is eliminated, take up to 2 of their Equipment or random hand cards.",
     5, false},
    {14, Side::kOutlaw, "cattle_katie", "Cattle Katie",
     "Once per turn, discard her non-always-active Equipment to draw 2 cards.",
     4, false},
    {15, Side::kOutlaw, "dalton_bros", "Dalton Bros",
     "Draw 3 cards instead of 2 during your Draw Phase.",
     3, false},
    {16, Side::kOutlaw, "jack_ransome", "Jack Ransome",
     "You may play cards on any character, ignoring normal target restrictions.",
     4, false},
    {17, Side::kOutlaw, "pearl_hat", "Pearl Hat",
     "Once per turn, discard a hand card to copy the top discard's eligible Action effect.",
     3, false},
    {18, Side::kOutlaw, "sid_curry", "Sid Curry",
     "When targeted by a hit, reveal from the opponent's deck: Barrel avoids and counters.",
     4, false},
    {19, Side::kOutlaw, "slim_poet", "Slim Poet",
     "When the opponent makes him lose non-final life, you may discard a random opponent hand card.",
     4, false},
    {20, Side::kOutlaw, "soundance_kid", "Soundance Kid",
     "Once per turn, discard a hand card; draw from both decks, keep 1, discard 1.",
     4, true},
    {21, Side::kOutlaw, "toco_ramirez", "Toco Ramirez",
     "You may play any hand card as a Colt; it counts as a BANG! card.",
     4, false},
    {22, Side::kOutlaw, "tom_thorn", "Tom Thorn",
     "Whenever he becomes active, the opponent's rearguard is hit.",
     4, false},
    {23, Side::kOutlaw, "west_harding", "West Harding",
     "You may play any number of BANG! cards during your turn.",
     4, false},
}};

}  // namespace

Side OtherSide(Side side) {
  return side == Side::kLaw ? Side::kOutlaw : Side::kLaw;
}

std::string_view SideName(Side side) {
  return side == Side::kLaw ? "law" : "outlaw";
}

const std::array<CardData, kCardNameCount>& Cards() { return kCards; }

const CardData& GetCard(CardName name) {
  const auto index = static_cast<std::size_t>(name);
  if (index >= kCards.size()) {
    throw std::out_of_range("invalid card name");
  }
  return kCards[index];
}

const std::array<CardInstanceData, kCardInstanceCount>& CardInstances() {
  return kCardInstances;
}

const CardInstanceData& GetCardInstance(int id) {
  if (id < 0 || id >= kCardInstanceCount) {
    throw std::out_of_range("invalid card instance");
  }
  return kCardInstances[static_cast<std::size_t>(id)];
}

std::vector<int> StartingDeck(Side side) {
  std::vector<int> result;
  result.reserve(kCardsPerSide);
  const int first = side == Side::kLaw ? 0 : kCardsPerSide;
  for (int id = first; id < first + kCardsPerSide; ++id) {
    result.push_back(id);
  }
  return result;
}

const std::array<CharacterData, kCharacterCount>& Characters() { return kCharacters; }

const CharacterData& GetCharacter(int id) {
  if (id < 0 || id >= kCharacterCount) {
    throw std::out_of_range("invalid character id");
  }
  return kCharacters[static_cast<std::size_t>(id)];
}

std::vector<int> CharacterIds(Side side) {
  std::vector<int> result;
  result.reserve(kCharactersPerSide);
  const int first = side == Side::kLaw ? 0 : kCharactersPerSide;
  for (int id = first; id < first + kCharactersPerSide; ++id) {
    result.push_back(id);
  }
  return result;
}

}  // namespace bang
