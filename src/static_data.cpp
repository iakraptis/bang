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
    // Renegades expansion (docs/renegades_cards.md). Flags: hit, avoid,
    // reload, always active.
    {CN::kAgility, "agility", "Agility",
     "Avoid 1 hit. Draw 1 card.",
     CK::kAction, false, true, false, false},
    {CN::kAmbush, "ambush", "Ambush",
     "Discard 1 card in play from your active character and/or 1 from any active character.",
     CK::kAction, false, false, false, false},
    {CN::kBuffaloRifle, "buffalo_rifle", "Buffalo Rifle",
     "Deal 1 hit; it may target the opponent's rearguard. Draw 1 card.",
     CK::kResource, true, false, false, false},
    {CN::kDoubleBarrel, "double_barrel", "Double Barrel",
     "Deal 1 hit. If it is avoided, the opponent's other character is targeted too.",
     CK::kResource, true, false, false, false},
    {CN::kFeint, "feint", "Feint",
     "Avoid 1 hit. If it avoids a BANG! card, take that card.",
     CK::kAction, false, true, false, false},
    {CN::kGoldPan, "gold_pan", "Gold Pan",
     "Draw the top card of the discard pile.",
     CK::kResource, false, false, false, false},
    {CN::kHowitzer, "howitzer", "Howitzer",
     "Deal 1 hit. Unless avoided, the opponent's active and rearguard each lose 1 life.",
     CK::kResource, true, false, false, false},
    {CN::kIndianChief, "indian_chief", "Indian Chief",
     "For each of their characters, the opponent discards a hit card or loses 1 life.",
     CK::kResource, false, false, false, false},
    {CN::kLightning, "lightning", "Lightning",
     "Deal 1 hit. You may play 1 additional BANG! card this turn.",
     CK::kBang, true, false, false, false},
    {CN::kLuckyPenny, "lucky_penny", "Lucky Penny",
     "Avoid 1 hit. Draw 1 card.",
     CK::kResource, false, true, false, false},
    {CN::kPardner, "pardner", "Pardner",
     "Use as a hit or as an avoid.",
     CK::kResource, true, true, false, false},
    {CN::kRicochet, "ricochet", "Ricochet",
     "Players alternately discard avoid cards, opponent first; the first who fails loses 1 life on their active.",
     CK::kAction, false, false, false, false},
    {CN::kSavage, "savage", "Savage",
     "Deal 1 hit. Draw 1 card.",
     CK::kBang, true, false, false, false},
    {CN::kSharps, "sharps", "Sharps",
     "Deal 1 hit. The target's ability and Equipment have no effect.",
     CK::kBang, true, false, false, false},
    {CN::kSidekick, "sidekick", "Sidekick",
     "Whenever this character becomes active, the opponent's active is hit; any hand card avoids it.",
     CK::kEquipment, false, false, true, false},
    {CN::kSlap, "slap", "Slap",
     "Discard 1 card in play from any active character and/or 1 card from any player's hand.",
     CK::kResource, false, false, false, false},
    {CN::kSpencer, "spencer", "Spencer",
     "Deal 1 hit; a target with 4 or more life loses 2 life.",
     CK::kBang, true, false, false, false},
    {CN::kSpurs, "spurs", "Spurs",
     "While your hand is empty, this character cannot be targeted by hits.",
     CK::kEquipment, false, false, false, false},
    {CN::kSwitch, "switch", "Switch",
     "Swap up to two active characters with their rearguards, in the order you choose.",
     CK::kAction, false, false, true, false},
    {CN::kUnionPacific, "union_pacific", "Union Pacific",
     "Draw 2 cards in total from your deck and/or the opponent's.",
     CK::kAction, false, false, false, false},
    {CN::kWalker, "walker", "Walker",
     "Deal 1 hit. If it is avoided, take this card back into your hand.",
     CK::kBang, true, false, false, false},
    {CN::kWhiskey, "whiskey", "Whiskey",
     "Reveal: on a Horseshoe your active regains 2 life, otherwise 1.",
     CK::kAction, false, false, false, false},
    {CN::kWhitney, "whitney", "Whitney",
     "Deal 1 hit that can only be avoided with a hit card.",
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

// Renegades: every copy as printed (docs/renegades_cards.md).
constexpr std::array<CardVariant, kRenegadeCardsPerSide> kRenegadeLawCards = {{
    {CN::kAmbush, RS::kHorseshoe},      {CN::kAgility, RS::kSnake},
    {CN::kFeint, RS::kSnake},           {CN::kMissed, RS::kSnake},
    {CN::kMissed, RS::kSnake},          {CN::kMissed, RS::kDynamite},
    {CN::kRicochet, RS::kHorseshoe},    {CN::kSwitch, RS::kBarrel},
    {CN::kSwitch, RS::kBarrel},         {CN::kSpurs, RS::kBarrel},
    {CN::kCarbine, RS::kHorseshoe},     {CN::kWalker, RS::kBarrel},
    {CN::kWalker, RS::kBarrel},         {CN::kWhitney, RS::kDynamite},
    {CN::kBuffaloRifle, RS::kHorseshoe}, {CN::kBuffaloRifle, RS::kHorseshoe},
    {CN::kHowitzer, RS::kDynamite},     {CN::kGoldPan, RS::kSnake},
    {CN::kPardner, RS::kHorseshoe},     {CN::kPardner, RS::kHorseshoe},
}};

constexpr std::array<CardVariant, kRenegadeCardsPerSide> kRenegadeOutlawCards = {{
    {CN::kAmbush, RS::kSnake},          {CN::kAgility, RS::kSnake},
    {CN::kFeint, RS::kSnake},           {CN::kKnife, RS::kHorseshoe},
    {CN::kUnionPacific, RS::kHorseshoe}, {CN::kWhiskey, RS::kBarrel},
    {CN::kSidekick, RS::kSnake},        {CN::kLightning, RS::kDynamite},
    {CN::kLightning, RS::kDynamite},    {CN::kSavage, RS::kSnake},
    {CN::kSharps, RS::kHorseshoe},      {CN::kSharps, RS::kHorseshoe},
    {CN::kSpencer, RS::kHorseshoe},     {CN::kSpencer, RS::kHorseshoe},
    {CN::kIndianChief, RS::kBarrel},    {CN::kDoubleBarrel, RS::kHorseshoe},
    {CN::kDoubleBarrel, RS::kHorseshoe}, {CN::kSlap, RS::kBarrel},
    {CN::kSlap, RS::kBarrel},           {CN::kLuckyPenny, RS::kBarrel},
}};

// Copies 0-39 Law, 40-79 Outlaw (base); 80-99 Renegade-Law, 100-119
// Renegade-Outlaw.
inline constexpr int kRenegadeLawFirstCard = kBaseCardInstanceCount;
inline constexpr int kRenegadeOutlawFirstCard =
    kRenegadeLawFirstCard + kRenegadeCardsPerSide;

constexpr std::array<CardInstanceData, kCardInstanceCount> BuildCardInstances() {
  std::array<CardInstanceData, kCardInstanceCount> result{};
  for (int i = 0; i < kCardsPerSide; ++i) {
    const auto law = kLawVariants[static_cast<std::size_t>(kLawDeckVariants[i])];
    result[i] = {i, Side::kLaw, law.name, law.reveal};
    const auto outlaw = kOutlawVariants[static_cast<std::size_t>(kOutlawDeckVariants[i])];
    result[kCardsPerSide + i] = {
        kCardsPerSide + i, Side::kOutlaw, outlaw.name, outlaw.reveal};
  }
  for (int i = 0; i < kRenegadeCardsPerSide; ++i) {
    const auto law = kRenegadeLawCards[static_cast<std::size_t>(i)];
    result[kRenegadeLawFirstCard + i] = {
        kRenegadeLawFirstCard + i, Side::kLaw, law.name, law.reveal, true};
    const auto outlaw = kRenegadeOutlawCards[static_cast<std::size_t>(i)];
    result[kRenegadeOutlawFirstCard + i] = {
        kRenegadeOutlawFirstCard + i, Side::kOutlaw, outlaw.name, outlaw.reveal, true};
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
    // Renegades expansion (docs/renegades_cards.md): Renegade-Law ...
    {24, Side::kLaw, "charlie_basset", "Charlie Basset",
     "Once per turn, after you use a Resource, draw 1 card.",
     3, true, true},
    {25, Side::kLaw, "hick_addams", "Hick Addams",
     "When another player makes you discard a hand card or a card from him, draw 1 card.",
     4, false, true},
    {26, Side::kLaw, "hottie_delou", "Hottie Delou",
     "Once per turn, you may use one of her Resources the turn it was put into play.",
     4, false, true},
    {27, Side::kLaw, "mystic_maid", "Mystic Maid",
     "Discard one of her Equipment cards (not always active) to avoid or as a Colt, then draw 1 card.",
     3, false, true},
    {28, Side::kLaw, "poker_alicia", "Poker Alicia",
     "Instead of drawing your 2 cards, you may take 1 Equipment from any active character.",
     4, false, true},
    {29, Side::kLaw, "the_marshal", "The Marshal",
     "When he enters play, draw 3 cards in total from the decks of your choice.",
     5, true, true},
    // ... and Renegade-Outlaw.
    {30, Side::kOutlaw, "big_steve", "Big Steve",
     "At the end of your turn, you may hold up to 6 cards in your hand.",
     5, true, true},
    {31, Side::kOutlaw, "dutch_harry", "Dutch Harry",
     "When you draw, you may look at the top card of the deck and keep or discard it.",
     4, true, true},
    {32, Side::kOutlaw, "el_chindi", "El Chindi",
     "While you hold any cards, he is immune to Action cards played by other players.",
     3, false, true},
    {33, Side::kOutlaw, "jim_longhair", "Jim Longhair",
     "Whenever your characters swap, you may swap the opponent's characters too.",
     4, true, true},
    {34, Side::kOutlaw, "king_fishers", "King Fishers",
     "Whenever any character receives its first Resource of the turn, draw 1 card.",
     4, false, true},
    {35, Side::kOutlaw, "kitty_leroy", "Kitty Leroy",
     "Once per turn, when you use one of her Resources, you may discard a hand card to repeat it.",
     3, false, true},
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

std::vector<int> StartingDeck(Side side, GameMode mode) {
  std::vector<int> result;
  result.reserve(kCardsPerSide + kRenegadeCardsPerSide);
  const int first = side == Side::kLaw ? 0 : kCardsPerSide;
  for (int id = first; id < first + kCardsPerSide; ++id) {
    result.push_back(id);
  }
  if (mode == GameMode::kReinforcements) {
    const int renegades =
        side == Side::kLaw ? kRenegadeLawFirstCard : kRenegadeOutlawFirstCard;
    for (int id = renegades; id < renegades + kRenegadeCardsPerSide; ++id) {
      result.push_back(id);
    }
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

std::vector<int> CharacterIds(Side side, GameMode mode) {
  std::vector<int> result;
  result.reserve(kCharactersPerSide + kRenegadeCharactersPerSide);
  const int first = side == Side::kLaw ? 0 : kCharactersPerSide;
  for (int id = first; id < first + kCharactersPerSide; ++id) {
    result.push_back(id);
  }
  if (mode == GameMode::kReinforcements) {
    const int renegades = kBaseCharacterCount +
                          (side == Side::kLaw ? 0 : kRenegadeCharactersPerSide);
    for (int id = renegades; id < renegades + kRenegadeCharactersPerSide; ++id) {
      result.push_back(id);
    }
  }
  return result;
}

}  // namespace bang
