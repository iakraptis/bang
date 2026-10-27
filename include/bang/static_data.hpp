#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace bang {

inline constexpr int kSideCount = 2;
// Card names, card copies, and characters cover the base game and the
// Renegades expansion. Base ids come first and never move; expansion ids are
// appended, so a base game uses exactly the first 31 names, copies 0-79, and
// characters 0-23.
inline constexpr int kBaseCardNameCount = 31;
inline constexpr int kCardNameCount = 54;
inline constexpr int kCardsPerSide = 40;            // base deck per side
inline constexpr int kRenegadeCardsPerSide = 20;    // Renegade-Law / -Outlaw
inline constexpr int kBaseCardInstanceCount = kCardsPerSide * 2;
inline constexpr int kCardInstanceCount =
    kBaseCardInstanceCount + kRenegadeCardsPerSide * 2;
inline constexpr int kCharactersPerSide = 12;       // base roster per side
inline constexpr int kRenegadeCharactersPerSide = 6;
inline constexpr int kBaseCharacterCount = kCharactersPerSide * 2;
inline constexpr int kCharacterCount =
    kBaseCharacterCount + kRenegadeCharactersPerSide * 2;

enum class Side : std::uint8_t { kLaw = 0, kOutlaw = 1 };
// Green-bordered Resources (Renegades) are a special kind of Equipment.
enum class CardKind : std::uint8_t { kAction, kEquipment, kBang, kResource };

// Which cards and characters a game deals. Reinforcements shuffles the
// Renegade-Law cards and characters into the Law side and the Renegade-Outlaw
// ones into the Outlaw side.
// Which cards each seat plays with. The engine always seats two players:
// seat 0 (Side::kLaw) plays first, seat 1 (Side::kOutlaw) second; a card's
// Side is its back. In The Third Group one seat plays the Renegades.
enum class GameMode : std::uint8_t {
  kBase,
  kReinforcements,
  kRenegadesVsLaw,     // Law (seat 0, 4 cards) vs Renegades (5 cards)
  kRenegadesVsOutlaw,  // Outlaw (seat 0, 5 cards) vs Renegades (4 cards)
};
enum class Faction : std::uint8_t { kLaw, kOutlaw, kRenegades };
enum class RevealSymbol : std::uint8_t { kBarrel, kSnake, kDynamite, kHorseshoe };

enum class CardName : std::uint8_t {
  kBank,
  kBarrel,
  kBeer,
  kCarbine,
  kClock,
  kColt,
  kDerringer,
  kDuel,
  kDynamite,
  kFanning,
  kGatling,
  kGeneralStore,
  kGunBelt,
  kHat,
  kIndians,
  kKnife,
  kMissed,
  kPeacemaker,
  kRattlesnake,
  kRemington,
  kReturnFire,
  kSchoolmarm,
  kSchofield,
  kScope,
  kShotgun,
  kStampede,
  kStealing,
  kStrongAle,
  kWagon,
  kWater,
  kWinchester,
  // Renegades expansion
  kAgility,
  kAmbush,
  kBuffaloRifle,
  kDoubleBarrel,
  kFeint,
  kGoldPan,
  kHowitzer,
  kIndianChief,
  kLightning,
  kLuckyPenny,
  kPardner,
  kRicochet,
  kSavage,
  kSharps,
  kSidekick,
  kSlap,
  kSpencer,
  kSpurs,
  kSwitch,
  kUnionPacific,
  kWalker,
  kWhiskey,
  kWhitney,
};

static_assert(static_cast<int>(CardName::kWinchester) + 1 == kBaseCardNameCount);
static_assert(static_cast<int>(CardName::kWhitney) + 1 == kCardNameCount);

enum class CharacterName : std::uint8_t {
  kAlanPinkertoon,
  kAnnieOakey,
  kBartMasterson,
  kBillTightman,
  kBuffaloBell,
  kDalonRanger,
  kJango,
  kPatGarret,
  kTexKiller,
  kTheStranger,
  kWildBill,
  kWyattEar,
  kBabeLeroy,
  kBullAnderson,
  kCattleKatie,
  kDaltonBros,
  kJackRansome,
  kPearlHat,
  kSidCurry,
  kSlimPoet,
  kSoundanceKid,
  kTocoRamirez,
  kTomThorn,
  kWestHarding,
  // Renegades expansion: Renegade-Law, then Renegade-Outlaw
  kCharlieBasset,
  kHickAddams,
  kHottieDelou,
  kMysticMaid,
  kPokerAlicia,
  kTheMarshal,
  kBigSteve,
  kDutchHarry,
  kElChindi,
  kJimLonghair,
  kKingFishers,
  kKittyLeroy,
};

static_assert(static_cast<int>(CharacterName::kWestHarding) + 1 ==
              kBaseCharacterCount);
static_assert(static_cast<int>(CharacterName::kKittyLeroy) + 1 ==
              kCharacterCount);

struct CardData {
  CardName name;
  std::string_view key;
  std::string_view display_name;
  std::string_view rules_text;
  CardKind kind;
  bool has_hit;
  bool has_avoid;
  bool reloadable;
  bool always_active;
};

struct CardInstanceData {
  int id;
  Side origin;
  CardName name;
  RevealSymbol reveal;
  bool renegade = false;  // a Renegade-Law / Renegade-Outlaw copy
};

struct CharacterData {
  int id;
  Side side;
  std::string_view key;
  std::string_view display_name;
  std::string_view ability_text;
  int max_life;
  bool always_active;
  bool renegade = false;  // a Renegade-Law / Renegade-Outlaw character
};

Side OtherSide(Side side);
std::string_view SideName(Side side);
const std::array<CardData, kCardNameCount>& Cards();
const CardData& GetCard(CardName name);
const std::array<CardInstanceData, kCardInstanceCount>& CardInstances();
const CardInstanceData& GetCardInstance(int id);
// The faction a seat plays in this mode, and its starting hand.
Faction SeatFaction(Side seat, GameMode mode);
std::string_view FactionName(Faction faction);
int StartingHandSize(Side seat, GameMode mode);
std::string_view GameModeName(GameMode mode);
// Throws std::invalid_argument for an unknown name.
GameMode GameModeFromName(std::string_view name);
// The deck and roster of the faction sitting in `side`'s seat.
std::vector<int> StartingDeck(Side side, GameMode mode = GameMode::kBase);
const std::array<CharacterData, kCharacterCount>& Characters();
const CharacterData& GetCharacter(int id);
std::vector<int> CharacterIds(Side side, GameMode mode = GameMode::kBase);

}  // namespace bang
