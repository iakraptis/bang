#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "bang/game.hpp"
#include "bang/heuristic.hpp"
#include "bang/json.hpp"
#include "bang/static_policy.hpp"
#include "bang/web.hpp"
#ifdef BANG_ENABLE_TORCH
#include "bang/training.hpp"
#endif

namespace {

int ChooseSimulationAction(const bang::GameState& state, bang::FastRng& rng) {
  const auto legal = state.LegalActions();
  if (legal.empty()) throw std::logic_error("non-terminal state has no legal actions");
  if (state.CurrentPhase() == bang::Phase::kRespondToHit) {
    // Randomly decide whether to spend an avoid card when one is available.
    return legal[rng.USize(legal.size())];
  }
  const auto is_play = [](int action) {
    const auto kind = bang::DecodeAction(action).kind;
    return kind == bang::ActionKind::kPlayCard ||
           kind == bang::ActionKind::kPlayAsColt ||
           kind == bang::ActionKind::kUseAbility;
  };
  const auto plays = std::count_if(legal.begin(), legal.end(), is_play);
  if (plays > 0) {
    std::vector<int> candidates;
    for (const int action : legal) {
      if (is_play(action)) {
        candidates.push_back(action);
      }
    }
    return candidates[rng.USize(candidates.size())];
  }
  if (std::find(legal.begin(), legal.end(), bang::kEndPlayAction) != legal.end()) {
    return bang::kEndPlayAction;
  }
  return legal[rng.USize(legal.size())];
}

// A player for `benchmark`: an exported browser policy (scored by the same
// C++ the web game uses), the scripted heuristic, or uniformly random moves.
struct BenchmarkAgent {
  std::string name;
  std::optional<bang::StaticPolicyNetwork> policy;
  bool heuristic = false;
};

BenchmarkAgent LoadBenchmarkAgent(const std::string& spec) {
  if (spec == "heuristic") return {spec, std::nullopt, true};
  if (spec == "random") return {spec, std::nullopt, false};
  return {spec, bang::LoadStaticPolicyFile(spec), false};
}

int ChooseBenchmarkAction(const BenchmarkAgent& agent, const bang::GameState& state,
                          bang::FastRng& rng, bool greedy) {
  const auto legal = state.LegalActions();
  if (legal.empty()) {
    throw std::logic_error("no legal actions in phase " +
                           std::to_string(static_cast<int>(state.CurrentPhase())) +
                           " for player " + std::to_string(state.CurrentPlayer()));
  }
  if (legal.size() == 1) return legal.front();
  if (agent.heuristic) return bang::ChooseHeuristicAction(state, rng);
  if (!agent.policy) return legal[rng.USize(legal.size())];
  std::vector<bang::ActionFeature> features;
  features.reserve(legal.size());
  for (const int action : legal) features.push_back(bang::EncodeAction(action));
  const std::vector<float> logits = bang::StaticPolicyLogits(
      *agent.policy, state.TrainingObservationTensor(state.CurrentPlayer()), features);
  if (greedy) {
    return legal[static_cast<std::size_t>(
        std::max_element(logits.begin(), logits.end()) - logits.begin())];
  }
  // Sampled, like the web game's default (stochastic) opponent.
  const float maximum = *std::max_element(logits.begin(), logits.end());
  std::vector<double> weights(logits.size());
  double total = 0.0;
  for (std::size_t index = 0; index < logits.size(); ++index) {
    weights[index] = std::exp(static_cast<double>(logits[index] - maximum));
    total += weights[index];
  }
  double pick = static_cast<double>(rng.NextU64() >> 11) * 0x1.0p-53 * total;
  for (std::size_t index = 0; index < weights.size(); ++index) {
    pick -= weights[index];
    if (pick <= 0.0) return legal[index];
  }
  return legal.back();
}

/* ---------------- benchmark statistics ----------------
   Measured from the outside: each move is compared with the state before it,
   so the engine needs no instrumentation. Everything is per seat. */

struct SeatTally {
  double games = 0, wins = 0;
  double drawn = 0;      // from a deck, the common deck or a looked-at choice
  double taken = 0;      // from the opponent's hand or any card in play
  double recovered = 0;  // from the discard pile (Gold Pan, Schoolmarm…)
  double damage_dealt = 0, damage_taken = 0, eliminations_suffered = 0;
  double cards_played = 0, resources_used = 0, avoid_responses = 0;
};

struct UsageTally {
  double games = 0, wins = 0, uses = 0;
};

// Where every card is: 1 a draw pile or choice, 2/3 a hand, 4 in play, 5 discard.
std::array<std::int8_t, bang::kCardInstanceCount> LocateCards(const bang::GameState& state) {
  std::array<std::int8_t, bang::kCardInstanceCount> where{};
  const auto mark = [&](const std::vector<int>& cards, std::int8_t code) {
    for (const int card : cards) where[static_cast<std::size_t>(card)] = code;
  };
  mark(state.CommonDeck(), 1);
  mark(state.ChoiceCards(), 1);
  mark(state.CardsSetAside(), 1);
  mark(state.DiscardPile(), 5);
  for (int seat = 0; seat < bang::kSideCount; ++seat) {
    const auto& player = state.Players()[static_cast<std::size_t>(seat)];
    mark(player.deck, 1);
    mark(player.hand, static_cast<std::int8_t>(2 + seat));
    for (const auto& position : player.positions) mark(position.equipment, 4);
  }
  return where;
}

// "law:sharps", "outlaw:sharps" or "sharps" (both seats) → seats and key.
std::pair<std::vector<int>, std::string> ParseSeatKey(const std::string& text) {
  const auto colon = text.find(':');
  if (colon == std::string::npos) return {{0, 1}, text};
  const std::string seat = text.substr(0, colon);
  if (seat != "law" && seat != "outlaw") {
    throw std::invalid_argument("seat must be law (first) or outlaw (second): " + text);
  }
  return {{seat == "law" ? 0 : 1}, text.substr(colon + 1)};
}

bang::CardName CardByKey(const std::string& key) {
  for (const auto& card : bang::Cards()) {
    if (card.key == key) return card.name;
  }
  throw std::invalid_argument("unknown card key: " + key);
}

int CharacterByKey(const std::string& key) {
  for (const auto& character : bang::Characters()) {
    if (character.key == key) return character.id;
  }
  throw std::invalid_argument("unknown character key: " + key);
}

// Agent A against agent B in one format, on paired seeds: each deal is
// played twice with the seats swapped, so deal luck cancels out.
int RunBenchmark(const std::vector<std::string>& arguments) {
  std::string a_spec = "policy/static-policy.json";
  std::string b_spec = "heuristic";
  bang::GameMode mode = bang::GameMode::kBase;
  int pairs = 500;
  std::uint64_t seed = 900001;
  int characters = 4;
  bool greedy = false;
  std::string stats_path;
  std::array<std::vector<bang::CardName>, bang::kSideCount> excluded_cards{};
  std::array<std::vector<int>, bang::kSideCount> excluded_characters{};
  int threads = static_cast<int>(std::max(1U, std::thread::hardware_concurrency()));
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    const std::string& option = arguments[index];
    const auto value = [&]() -> const std::string& {
      if (index + 1 >= arguments.size()) {
        throw std::invalid_argument("missing value for " + option);
      }
      return arguments[++index];
    };
    if (option == "--a") {
      a_spec = value();
    } else if (option == "--b") {
      b_spec = value();
    } else if (option == "--mode") {
      mode = bang::GameModeFromName(value());
    } else if (option == "--pairs") {
      pairs = std::stoi(value());
    } else if (option == "--seed") {
      seed = std::stoull(value());
    } else if (option == "--characters") {
      characters = std::stoi(value());
    } else if (option == "--threads") {
      threads = std::max(1, std::stoi(value()));
    } else if (option == "--greedy") {
      greedy = true;
    } else if (option == "--stats") {
      stats_path = value();
    } else if (option == "--exclude-card") {
      const auto [seats, key] = ParseSeatKey(value());
      for (const int seat : seats) excluded_cards[static_cast<std::size_t>(seat)].push_back(CardByKey(key));
    } else if (option == "--exclude-character") {
      const auto [seats, key] = ParseSeatKey(value());
      for (const int seat : seats) {
        excluded_characters[static_cast<std::size_t>(seat)].push_back(CharacterByKey(key));
      }
    } else {
      throw std::invalid_argument("unknown benchmark option: " + option);
    }
  }
  if (pairs <= 0) throw std::invalid_argument("pairs must be positive");
  const BenchmarkAgent a = LoadBenchmarkAgent(a_spec);
  const BenchmarkAgent b = LoadBenchmarkAgent(b_spec);

  // wins[seat A sat in][winning seat]; truncated games count for neither.
  std::array<std::array<long long, 2>, 2> wins{};
  long long truncated = 0;
  long long decisions = 0;
  long long failed = 0;
  std::array<SeatTally, 2> seat_stats{};
  std::array<std::map<std::string, UsageTally>, 2> card_usage{};
  std::array<std::map<std::string, UsageTally>, 2> character_usage{};
  double total_turns = 0;
  std::mutex mutex;
  std::atomic<int> next{0};
  const auto start = std::chrono::steady_clock::now();
  const auto worker = [&] {
    for (int pair = next++; pair < pairs; pair = next++) {
      for (int a_seat = 0; a_seat < 2; ++a_seat) {
        bang::GameConfig config;
        config.seed = seed + static_cast<std::uint64_t>(pair);
        config.mode = mode;
        config.characters_per_side = characters;
        config.excluded_cards = excluded_cards;
        config.excluded_characters = excluded_characters;
        bang::GameState state(config);
        bang::FastRng rng(config.seed * 0x9e3779b97f4a7c15ULL + static_cast<std::uint64_t>(a_seat));
        long long steps = 0;
        std::vector<std::string> recent;  // the last moves, for error reports
        std::array<SeatTally, 2> game{};
        std::array<std::map<std::string, double>, 2> played{};
        std::array<std::set<int>, 2> fielded{};
        const auto field = [&] {
          for (int seat = 0; seat < 2; ++seat) {
            for (const auto& position : state.Players()[static_cast<std::size_t>(seat)].positions) {
              if (position.character_id >= 0 && position.life > 0) {
                fielded[static_cast<std::size_t>(seat)].insert(position.character_id);
              }
            }
          }
        };
        try {
          field();
          while (!state.IsTerminal() && steps < 20000) {
            const int actor_seat = state.CurrentPlayer();
            const BenchmarkAgent& actor = actor_seat == a_seat ? a : b;
            const int action = ChooseBenchmarkAction(actor, state, rng, greedy);
            recent.push_back("p" + std::to_string(actor_seat) + " " +
                             bang::ActionToString(action));
            if (recent.size() > 8) recent.erase(recent.begin());

            // What the move does with a card, before it moves.
            const auto key = bang::DecodeAction(action);
            auto& mine = game[static_cast<std::size_t>(actor_seat)];
            const bool card_move = key.kind == bang::ActionKind::kPlayCard ||
                                   key.kind == bang::ActionKind::kPlayAsColt ||
                                   key.kind == bang::ActionKind::kUseResource;
            if (card_move && state.CurrentPhase() == bang::Phase::kPlay) {
              const auto& card = bang::GetCard(bang::GetCardInstance(key.card_instance).name);
              ++mine.cards_played;
              played[static_cast<std::size_t>(actor_seat)][std::string(card.key)] += 1;
              if (key.kind == bang::ActionKind::kUseResource) ++mine.resources_used;
            }
            if (key.kind == bang::ActionKind::kAvoidHit ||
                (key.kind == bang::ActionKind::kSelectCard &&
                 state.CurrentPhase() == bang::Phase::kRespondToHit)) {
              ++mine.avoid_responses;
              const auto& card = bang::GetCard(bang::GetCardInstance(key.card_instance).name);
              played[static_cast<std::size_t>(actor_seat)][std::string(card.key)] += 1;
              if (card.kind == bang::CardKind::kResource) ++mine.resources_used;
            }

            const auto before = LocateCards(state);
            std::array<std::array<std::pair<int, int>, 2>, 2> lives{};
            for (int seat = 0; seat < 2; ++seat) {
              for (int position = 0; position < 2; ++position) {
                const auto& slot = state.Players()[static_cast<std::size_t>(seat)]
                                       .positions[static_cast<std::size_t>(position)];
                lives[static_cast<std::size_t>(seat)][static_cast<std::size_t>(position)] = {
                    slot.character_id, slot.life};
              }
            }
            state.ApplyAction(action);
            ++steps;

            const auto after = LocateCards(state);
            for (std::size_t card = 0; card < after.size(); ++card) {
              for (int seat = 0; seat < 2; ++seat) {
                const auto hand = static_cast<std::int8_t>(2 + seat);
                if (after[card] != hand || before[card] == hand) continue;
                auto& tally = game[static_cast<std::size_t>(seat)];
                if (before[card] == 1) {
                  ++tally.drawn;
                } else if (before[card] == 5) {
                  ++tally.recovered;
                } else {
                  ++tally.taken;
                }
              }
            }
            for (int seat = 0; seat < 2; ++seat) {
              for (int position = 0; position < 2; ++position) {
                const auto [id, life] =
                    lives[static_cast<std::size_t>(seat)][static_cast<std::size_t>(position)];
                const auto& slot = state.Players()[static_cast<std::size_t>(seat)]
                                       .positions[static_cast<std::size_t>(position)];
                double lost = 0;
                bool eliminated = false;
                if (id >= 0 && slot.character_id != id) {
                  lost = life;  // replaced from the reserve: the old one died
                  eliminated = life > 0;
                } else if (slot.life < life) {
                  lost = life - slot.life;
                  eliminated = slot.life <= 0;
                }
                game[static_cast<std::size_t>(seat)].damage_taken += lost;
                game[static_cast<std::size_t>(1 - seat)].damage_dealt += lost;
                if (eliminated) ++game[static_cast<std::size_t>(seat)].eliminations_suffered;
              }
            }
            field();
          }
        } catch (const std::exception& error) {
          // An engine error is a bug: report the game and keep benchmarking.
          std::lock_guard lock(mutex);
          ++failed;
          std::cerr << "game seed=" << config.seed << " a_seat=" << a_seat
                    << " step=" << steps << " failed: " << error.what() << "\n  after:";
          for (const auto& move : recent) std::cerr << ' ' << move << ';';
          std::cerr << '\n';
          continue;
        }
        std::lock_guard lock(mutex);
        decisions += steps;
        if (!state.IsTerminal()) {
          ++truncated;
          continue;
        }
        const int winner = state.Winner();
        ++wins[static_cast<std::size_t>(a_seat)][static_cast<std::size_t>(winner)];
        total_turns += state.TurnNumber();
        for (int seat = 0; seat < 2; ++seat) {
          const auto index = static_cast<std::size_t>(seat);
          const double won = winner == seat ? 1.0 : 0.0;
          auto& total = seat_stats[index];
          const auto& one = game[index];
          total.games += 1;
          total.wins += won;
          total.drawn += one.drawn;
          total.taken += one.taken;
          total.recovered += one.recovered;
          total.damage_dealt += one.damage_dealt;
          total.damage_taken += one.damage_taken;
          total.eliminations_suffered += one.eliminations_suffered;
          total.cards_played += one.cards_played;
          total.resources_used += one.resources_used;
          total.avoid_responses += one.avoid_responses;
          for (const auto& [card, uses] : played[index]) {
            auto& usage = card_usage[index][card];
            usage.games += 1;
            usage.wins += won;
            usage.uses += uses;
          }
          for (const int character : fielded[index]) {
            auto& usage = character_usage[index][std::string(bang::GetCharacter(character).key)];
            usage.games += 1;
            usage.wins += won;
          }
        }
      }
    }
  };
  std::vector<std::thread> pool;
  for (int index = 0; index < threads; ++index) pool.emplace_back(worker);
  for (auto& thread : pool) thread.join();
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

  const auto rate = [](long long won, long long played) {
    return played > 0 ? static_cast<double>(won) / static_cast<double>(played) : 0.0;
  };
  const long long a_first_games = wins[0][0] + wins[0][1];
  const long long a_second_games = wins[1][0] + wins[1][1];
  const long long a_wins = wins[0][0] + wins[1][1];
  const long long games = a_first_games + a_second_games;
  const double a_rate = rate(a_wins, games);
  // Standard error of A's win rate over the decided games.
  const double error =
      games > 0 ? std::sqrt(a_rate * (1.0 - a_rate) / static_cast<double>(games)) : 0.0;
  const bang::JsonValue result = bang::JsonValue::Object{
      {"mode", std::string(bang::GameModeName(mode))},
      {"a", a.name},
      {"b", b.name},
      {"games", static_cast<double>(games)},
      {"aWins", static_cast<double>(a_wins)},
      {"aWinRate", a_rate},
      {"aWinRateStdErr", error},
      {"aWinRateFirstSeat", rate(wins[0][0], a_first_games)},
      {"aWinRateSecondSeat", rate(wins[1][1], a_second_games)},
      {"firstSeatWinRate", rate(wins[0][0] + wins[1][0], games)},
      {"truncated", static_cast<double>(truncated)},
      {"failed", static_cast<double>(failed)},
      {"decisionsPerGame", games + truncated > 0 ? static_cast<double>(decisions) /
                                                       static_cast<double>(games + truncated)
                                                 : 0.0},
      {"seconds", seconds},
  };
  std::cout << result.Dump() << '\n';

  if (!stats_path.empty()) {
    bang::JsonValue::Array seats;
    for (int seat = 0; seat < 2; ++seat) {
      const auto index = static_cast<std::size_t>(seat);
      const auto& total = seat_stats[index];
      const double n = std::max(1.0, total.games);
      const auto usage_json = [&](const std::map<std::string, UsageTally>& usage) {
        bang::JsonValue::Object result;
        for (const auto& [name, tally] : usage) {
          result[name] = bang::JsonValue::Object{
              {"games", tally.games}, {"wins", tally.wins}, {"uses", tally.uses}};
        }
        return result;
      };
      seats.emplace_back(bang::JsonValue::Object{
          {"faction", std::string(bang::FactionName(bang::SeatFaction(static_cast<bang::Side>(seat), mode)))},
          {"games", total.games},
          {"winRate", total.wins / n},
          {"drawnPerGame", total.drawn / n},
          {"takenPerGame", total.taken / n},
          {"recoveredPerGame", total.recovered / n},
          {"damageDealtPerGame", total.damage_dealt / n},
          {"damageTakenPerGame", total.damage_taken / n},
          {"eliminationsSufferedPerGame", total.eliminations_suffered / n},
          {"cardsPlayedPerGame", total.cards_played / n},
          {"resourcesUsedPerGame", total.resources_used / n},
          {"avoidResponsesPerGame", total.avoid_responses / n},
          {"cards", usage_json(card_usage[index])},
          {"characters", usage_json(character_usage[index])},
      });
    }
    const double decided = std::max(1.0, seat_stats[0].games);
    const bang::JsonValue stats = bang::JsonValue::Object{
        {"summary", result},
        {"turnsPerGame", total_turns / decided},
        {"seats", std::move(seats)},
    };
    std::ofstream file(stats_path);
    if (!file) throw std::runtime_error("cannot write " + stats_path);
    file << stats.Dump() << '\n';
  }
  return failed > 0 ? 1 : 0;
}

int RunSimulation(int games, std::uint64_t seed, int characters_per_side) {
  bang::FastRng policy_rng(seed ^ 0xd1b54a32d192ed03ULL);
  std::array<int, 2> wins{};
  std::uint64_t decisions = 0;
  const auto start = std::chrono::steady_clock::now();
  for (int game = 0; game < games; ++game) {
    try {
      bang::GameConfig config;
      config.seed = seed + static_cast<std::uint64_t>(game);
      config.characters_per_side = characters_per_side;
      bang::GameState state(config);
      while (!state.IsTerminal()) {
        if (decisions > static_cast<std::uint64_t>(games) * 20000ULL) {
          throw std::runtime_error("simulation exceeded decision safety limit");
        }
        state.ApplyAction(ChooseSimulationAction(state, policy_rng));
        ++decisions;
      }
      ++wins[static_cast<std::size_t>(state.Winner())];
    } catch (const std::exception& error) {
      throw std::runtime_error("game " + std::to_string(game) +
                               " failed: " + error.what());
    }
  }
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
  std::cout << "games=" << games << " law_wins=" << wins[0]
            << " outlaw_wins=" << wins[1] << " decisions=" << decisions
            << " decisions_per_second=" << static_cast<std::uint64_t>(decisions / elapsed.count())
            << '\n';
  return 0;
}

#ifdef BANG_ENABLE_TORCH
std::optional<std::string> Option(
    const std::vector<std::string>& arguments,
    std::string_view name) {
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    if (arguments[index] == name) {
      if (index + 1 >= arguments.size()) {
        throw std::invalid_argument(
            "missing value for " + std::string(name));
      }
      return arguments[index + 1];
    }
  }
  return std::nullopt;
}

int IntOption(
    const std::vector<std::string>& arguments,
    std::string_view name,
    int fallback) {
  const auto value = Option(arguments, name);
  return value.has_value() ? std::stoi(*value) : fallback;
}

double DoubleOption(
    const std::vector<std::string>& arguments,
    std::string_view name,
    double fallback) {
  const auto value = Option(arguments, name);
  return value.has_value() ? std::stod(*value) : fallback;
}

std::vector<bang::TrainingOpponentSpec> OpponentOptions(
    const std::vector<std::string>& arguments) {
  std::vector<bang::TrainingOpponentSpec> result;
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    if (arguments[index] != "--opponent") continue;
    if (index + 1 >= arguments.size()) {
      throw std::invalid_argument("missing value for --opponent");
    }
    const std::string& text = arguments[index + 1];
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
      const std::size_t separator = text.find(':', start);
      parts.push_back(text.substr(start, separator - start));
      if (separator == std::string::npos) break;
      start = separator + 1;
    }
    if (parts.size() < 3) {
      throw std::invalid_argument(
          "--opponent must be kind:id:probability or "
          "checkpoint:id:path:probability");
    }
    bang::TrainingOpponentSpec spec{
        .id = parts[1],
        .checkpoint = {},
    };
    if (parts[0] == "random") {
      spec.kind = bang::TrainingOpponentKind::kRandom;
      spec.probability = std::stod(parts[2]);
    } else if (parts[0] == "heuristic") {
      spec.kind = bang::TrainingOpponentKind::kHeuristic;
      spec.probability = std::stod(parts[2]);
    } else if (parts[0] == "checkpoint" && parts.size() >= 4) {
      // The path may itself contain ':' (a Windows drive letter), so it spans
      // everything between the id and the final probability field.
      const std::size_t path_start = parts[0].size() + parts[1].size() + 2;
      const std::size_t path_end = text.rfind(':');
      spec.kind = bang::TrainingOpponentKind::kCheckpoint;
      spec.checkpoint = text.substr(path_start, path_end - path_start);
      spec.probability = std::stod(parts.back());
    } else {
      throw std::invalid_argument("unsupported --opponent specification");
    }
    result.push_back(std::move(spec));
  }
  return result;
}

void ValidateTrainingArguments(
    const std::vector<std::string>& arguments) {
  const std::array<std::string_view, 17> options{
      "--iterations",
      "--model",
      "--batch-games",
      "--update-epochs",
      "--minibatches",
      "--seed",
      "--torch-threads",
      "--learning-rate",
      "--entropy-coef",
      "--target-kl",
      "--checkpoint-dir",
      "--metrics",
      "--resume",
      "--stop-file",
      "--device",
      "--mmd-alpha",
      "--magnet-interval",
  };
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    const std::string_view argument = arguments[index];
    const bool known =
        argument == "--opponent" ||
        std::find(options.begin(), options.end(), argument) != options.end();
    if (!known) {
      throw std::invalid_argument(
          "unknown training option: " + std::string(argument));
    }
    if (++index >= arguments.size()) {
      throw std::invalid_argument(
          "missing value for " + std::string(argument));
    }
  }
}
#endif

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) {
      std::cerr
          << "usage:\n"
          << "  bang_duel simulate [games] [seed] [characters_per_side]\n"
          << "  bang_duel web [port] [host] [--session-dir DIR]\n"
          << "  bang_duel web-smoke\n"
          << "  bang_duel static-policy-smoke STATIC_POLICY_JSON\n"
          << "  bang_duel benchmark [--a POLICY.json|heuristic|random] [--b ...]"
             " [--mode MODE] [--pairs N] [--seed S] [--greedy]\n"
#ifdef BANG_ENABLE_TORCH
          << "  bang_duel train [--model v2|v3] [--iterations N]"
             " [--batch-games N] ...\n"
          << "  bang_duel evaluate CANDIDATE [OPPONENT|-] [pairs] [seed] [threads]\n"
          << "  bang_duel self-evaluate CHECKPOINT [games] [seed] [threads]\n"
          << "  bang_duel infer CHECKPOINT [games] [seed] [threads]\n"
          << "  bang_duel checkpoint-canary CHECKPOINT\n"
#endif
          ;
      return 2;
    }
    const std::string command = argv[1];
#ifdef BANG_ENABLE_TORCH
    if (command == "train") {
      const std::vector<std::string> arguments(argv + 2, argv + argc);
      ValidateTrainingArguments(arguments);
      bang::TrainingConfig config;
      config.iterations =
          IntOption(arguments, "--iterations", config.iterations);
      if (const auto value = Option(arguments, "--model")) {
        config.model_type = bang::ResolveModelType(*value);
      }
      config.batch_games =
          IntOption(arguments, "--batch-games", config.batch_games);
      config.update_epochs =
          IntOption(arguments, "--update-epochs", config.update_epochs);
      config.minibatches =
          IntOption(arguments, "--minibatches", config.minibatches);
      config.seed = IntOption(arguments, "--seed", config.seed);
      config.torch_threads =
          IntOption(arguments, "--torch-threads", config.torch_threads);
      config.learning_rate =
          DoubleOption(arguments, "--learning-rate", config.learning_rate);
      config.entropy_coef =
          DoubleOption(arguments, "--entropy-coef", config.entropy_coef);
      config.target_kl =
          DoubleOption(arguments, "--target-kl", config.target_kl);
      if (const auto value = Option(arguments, "--checkpoint-dir")) {
        config.checkpoint_dir = *value;
      }
      if (const auto value = Option(arguments, "--metrics")) {
        config.metrics_path = *value;
      } else {
        config.metrics_path = config.checkpoint_dir / "metrics.jsonl";
      }
      if (const auto value = Option(arguments, "--resume")) {
        config.resume_from = *value;
      }
      if (const auto value = Option(arguments, "--stop-file")) {
        config.stop_file = *value;
      }
      if (const auto value = Option(arguments, "--device")) {
        config.device = *value;
      }
      config.mmd_alpha =
          DoubleOption(arguments, "--mmd-alpha", config.mmd_alpha);
      config.magnet_interval =
          IntOption(arguments, "--magnet-interval", config.magnet_interval);
      config.opponents = OpponentOptions(arguments);
      return bang::RunTraining(config);
    }
    if (command == "evaluate") {
      if (argc < 3) {
        throw std::invalid_argument(
            "evaluate requires a candidate checkpoint");
      }
      const std::filesystem::path opponent =
          argc >= 4 && std::string(argv[3]) != "-"
              ? std::filesystem::path(argv[3])
              : std::filesystem::path{};
      const auto result = bang::RunPairedEvaluation(
          argv[2],
          opponent,
          argc >= 5 ? std::atoi(argv[4]) : 64,
          argc >= 6 ? std::atoi(argv[5]) : 900001,
          argc >= 7 ? std::atoi(argv[6]) : 6);
      std::cout << result.ToJson().Dump() << '\n';
      return 0;
    }
    if (command == "self-evaluate") {
      if (argc < 3) {
        throw std::invalid_argument(
            "self-evaluate requires a checkpoint");
      }
      std::cout << bang::RunSelfPlayEvaluation(
                       argv[2],
                       argc >= 4 ? std::atoi(argv[3]) : 2048,
                       argc >= 5 ? std::atoi(argv[4]) : 950001,
                       argc >= 6 ? std::atoi(argv[5]) : 6)
                       .Dump()
                << '\n';
      return 0;
    }
    if (command == "infer") {
      if (argc < 3) {
        throw std::invalid_argument(
            "infer requires a checkpoint");
      }
      std::cout << bang::RunCheckpointInference(
                       argv[2],
                       argc >= 4 ? std::atoi(argv[3]) : 16,
                       argc >= 5 ? std::atoi(argv[4]) : 700001,
                       argc >= 6 ? std::atoi(argv[5]) : 6)
                       .Dump()
                << '\n';
      return 0;
    }
    if (command == "checkpoint-canary") {
      if (argc < 3) {
        throw std::invalid_argument(
            "checkpoint-canary requires a checkpoint");
      }
      std::cout << bang::RunCheckpointCanary(argv[2]).Dump() << '\n';
      return 0;
    }
#endif
    if (command == "web") {
      bang::WebConfig config;
      if (argc >= 3 && std::string_view(argv[2]).find("--") != 0) {
        config.port = std::atoi(argv[2]);
        if (argc >= 4) config.host = argv[3];
      } else {
        for (int index = 2; index < argc; ++index) {
          const std::string option = argv[index];
          const auto value = [&]() -> std::string {
            if (index + 1 >= argc) {
              throw std::invalid_argument("missing value for " + option);
            }
            return argv[++index];
          };
          if (option == "--port") {
            config.port = std::stoi(value());
          } else if (option == "--host") {
            config.host = value();
          } else if (option == "--run-dir") {
            config.run_dir = value();
          } else if (option == "--checkpoint") {
            config.checkpoint = value();
          } else if (option == "--static-policy") {
            // An exported browser policy, scored by the same C++ the WASM
            // build runs, so a candidate can be played before it ships.
            config.static_policy = value();
          } else if (option == "--session-dir") {
            config.session_dir = value();
          } else if (option == "--torch-threads") {
            config.torch_threads = std::stoi(value());
          } else if (option == "--require-trained") {
            config.require_trained_policy = true;
          } else if (option == "--mode") {
            config.mode = bang::GameModeFromName(value());
          } else if (option == "--greedy") {
            config.stochastic_policy = false;
          } else {
            throw std::invalid_argument("unknown web option: " + option);
          }
        }
      }
      return bang::RunWebServer(config);
    }
    if (command == "web-smoke") {
      bang::WebConfig config;
      if (argc >= 3) {
        config.checkpoint = argv[2];
        config.require_trained_policy = true;
      }
      std::cout << bang::RunWebSmokeTest(config).Dump() << '\n';
      return 0;
    }
    if (command == "static-policy-parity") {
      if (argc < 4) {
        throw std::invalid_argument(
            "static-policy-parity requires a checkpoint and a static policy");
      }
      std::cout << bang::RunStaticPolicyParity(
                       argv[2],
                       argv[3],
                       argc >= 5 ? std::atoi(argv[4]) : 2000,
                       argc >= 6 ? std::atoi(argv[5]) : 31337)
                       .Dump()
                << '\n';
      return 0;
    }
    if (command == "policy-logits") {
      // Scores a static policy assigns to every legal move along fixed random
      // base-game playouts. Moves are named rather than numbered so a fixture
      // stays comparable when the action space is renumbered.
      if (argc < 3) {
        throw std::invalid_argument(
            "policy-logits requires a static policy JSON file");
      }
      const auto policy = bang::LoadStaticPolicyFile(argv[2]);
      const int decisions = argc >= 4 ? std::atoi(argv[3]) : 300;
      const std::uint64_t seed =
          argc >= 5 ? std::strtoull(argv[4], nullptr, 10) : 4242;
      bang::FastRng rng(seed ^ 0x6a09e667f3bcc909ULL);
      int game = 0;
      bang::GameState state(
          bang::GameConfig{.seed = seed, .characters_per_side = 4});
      bang::JsonValue::Array rows;
      while (static_cast<int>(rows.size()) < decisions) {
        if (state.IsTerminal()) {
          state = bang::GameState(bang::GameConfig{
              .seed = seed + static_cast<std::uint64_t>(++game),
              .characters_per_side = 4});
        }
        const std::vector<int> legal = state.LegalActions();
        if (legal.size() > 1) {
          std::vector<bang::ActionFeature> features;
          bang::JsonValue::Array moves;
          for (const int action : legal) {
            features.push_back(bang::EncodeAction(action));
            moves.emplace_back(bang::ActionToString(action));
          }
          const auto logits = bang::StaticPolicyLogits(
              policy, state.TrainingObservationTensor(state.CurrentPlayer()),
              features);
          bang::JsonValue::Array values;
          for (const float value : logits) {
            values.emplace_back(static_cast<double>(value));
          }
          rows.emplace_back(bang::JsonValue::Object{
              {"game", game},
              {"moves", std::move(moves)},
              {"logits", std::move(values)},
          });
        }
        state.ApplyAction(legal[rng.USize(legal.size())]);
      }
      std::cout << bang::JsonValue(bang::JsonValue::Object{
                       {"seed", static_cast<double>(seed)},
                       {"decisions", std::move(rows)},
                   }).Dump()
                << '\n';
      return 0;
    }
    if (command == "static-policy-smoke") {
      if (argc != 3) {
        throw std::invalid_argument(
            "static-policy-smoke requires a static policy JSON file");
      }
      bang::WebConfig config;
      config.static_policy = argv[2];
      config.require_trained_policy = true;
      config.stochastic_policy = false;
      std::cout << bang::RunWebSmokeTest(config).Dump() << '\n';
      return 0;
    }
    if (command == "benchmark") {
      return RunBenchmark(std::vector<std::string>(argv + 2, argv + argc));
    }
    if (command != "simulate") {
      throw std::invalid_argument("unknown command: " + command);
    }
    const int games = argc >= 3 ? std::atoi(argv[2]) : 100;
    const std::uint64_t seed = argc >= 4 ? std::strtoull(argv[3], nullptr, 10) : 1;
    const int characters_per_side = argc >= 5 ? std::atoi(argv[4]) : 4;
    if (games <= 0) throw std::invalid_argument("games must be positive");
    if (characters_per_side < 2 ||
        characters_per_side > bang::kCharactersPerSide) {
      throw std::invalid_argument(
          "characters_per_side must be between 2 and 12");
    }
    return RunSimulation(games, seed, characters_per_side);
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
