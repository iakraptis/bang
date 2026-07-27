#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace bang {

inline constexpr int kSideCount = 2;
inline constexpr int kCardNameCount = 31;
inline constexpr int kCardInstanceCount = 80;
inline constexpr int kCardsPerSide = 40;
inline constexpr int kCharacterCount = 24;
inline constexpr int kCharactersPerSide = 12;

enum class Side : std::uint8_t { kLaw = 0, kOutlaw = 1 };
enum class CardKind : std::uint8_t { kAction, kEquipment, kBang };
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
};

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
};

static_assert(static_cast<int>(CharacterName::kWestHarding) + 1 ==
              kCharacterCount);

static_assert(static_cast<int>(CharacterName::kWestHarding) + 1 ==
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
};

struct CharacterData {
  int id;
  Side side;
  std::string_view key;
  std::string_view display_name;
  std::string_view ability_text;
  int max_life;
  bool always_active;
};

Side OtherSide(Side side);
std::string_view SideName(Side side);
const std::array<CardData, kCardNameCount>& Cards();
const CardData& GetCard(CardName name);
const std::array<CardInstanceData, kCardInstanceCount>& CardInstances();
const CardInstanceData& GetCardInstance(int id);
std::vector<int> StartingDeck(Side side);
const std::array<CharacterData, kCharacterCount>& Characters();
const CharacterData& GetCharacter(int id);
std::vector<int> CharacterIds(Side side);

}  // namespace bang
