#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bang/game.hpp"
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
            const std::string mode = value();
            if (mode == "base") {
              config.mode = bang::GameMode::kBase;
            } else if (mode == "reinforcements") {
              config.mode = bang::GameMode::kReinforcements;
            } else {
              throw std::invalid_argument("--mode must be base or reinforcements");
            }
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
