#include "bang/training.hpp"

#ifdef BANG_ENABLE_TORCH

#include <algorithm>
#include <optional>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "bang/checkpoint.hpp"
#include "bang/game.hpp"
#include "bang/heuristic.hpp"
#include "bang/policy_network.hpp"
#include "bang/static_policy.hpp"

namespace bang {
namespace {

std::atomic<bool> stop_requested{false};

void StopHandler(int) {
  stop_requested.store(true);
}

struct Transition {
  std::array<float, kTrainingObservationSize> observation{};
  std::array<float, kPrivilegedObservationSize> privileged{};
  std::vector<ActionFeature> action_features;
  int selected_row = -1;
  float old_log_probability = 0.0F;
  float value = 0.0F;
  float reward = 0.0F;
  float return_value = 0.0F;
  float advantage = 0.0F;
  int player = -1;
};

struct RuntimeOpponent {
  TrainingOpponentSpec spec;
  BangActorCritic network{nullptr};
};

struct Episode {
  Episode(GameState initial, int learner_player, int selected_opponent,
          std::uint64_t policy_seed)
      : state(std::move(initial)),
        learner(learner_player),
        opponent(selected_opponent),
        policy_rng(policy_seed) {}

  GameState state;
  int learner = 0;
  int opponent = 0;
  FastRng policy_rng;
  std::vector<std::size_t> transition_indexes;
  int steps = 0;
  bool finished = false;
};

struct PendingDecision {
  std::size_t episode = 0;
  std::size_t transition = 0;
  std::vector<int> actions;
};

struct UpdateStats {
  double policy_loss = 0.0;
  double value_loss = 0.0;
  double auxiliary_loss = 0.0;
  double entropy = 0.0;
  double kl = 0.0;
  double magnet_kl = 0.0;
  double clip_fraction = 0.0;
  double gradient_norm = 0.0;
  int minibatch_updates = 0;
  int epochs = 0;
  bool early_stopped = false;
};

struct StyleStats {
  std::int64_t total_decisions = 0;
  std::int64_t decisions = 0;
  std::int64_t turns = 0;
  std::int64_t cards_played = 0;
  std::int64_t equipment_played = 0;
  std::int64_t attack_opportunities = 0;
  std::int64_t attacks = 0;
  std::int64_t defense_opportunities = 0;
  std::int64_t defenses = 0;
  std::int64_t swap_opportunities = 0;
  std::int64_t swaps = 0;
  std::int64_t ability_opportunities = 0;
  std::int64_t abilities = 0;
  std::int64_t opponent_targets = 0;
  std::int64_t rear_targets = 0;
  double normalized_entropy = 0.0;
  double top_probability = 0.0;
  double selected_probability = 0.0;
  double probability_margin = 0.0;
  std::array<std::int64_t, kCardNameCount> card_opportunities{};
  std::array<std::int64_t, kCardNameCount> card_selections{};
  std::array<std::int64_t, kCardNameCount> card_usage{};
};

bool HasCard(const ActionKey& action) {
  return action.kind == ActionKind::kPlayCard ||
      action.kind == ActionKind::kReloadCard ||
      action.kind == ActionKind::kDiscardCard ||
      action.kind == ActionKind::kAvoidHit ||
      action.kind == ActionKind::kSelectCard ||
      action.kind == ActionKind::kPlayAsColt;
}

bool IsAttack(const ActionKey& action) {
  if (action.kind == ActionKind::kPlayAsColt) return true;
  return action.kind == ActionKind::kPlayCard &&
      GetCard(GetCardInstance(action.card_instance).name).has_hit;
}

double Rate(std::int64_t numerator, std::int64_t denominator) {
  return denominator == 0
      ? 0.0
      : static_cast<double>(numerator) / static_cast<double>(denominator);
}

void RecordTurnIfEnded(int learner, int player, int action, StyleStats& stats) {
  if (player == learner && action == kEndPlayAction) ++stats.turns;
}

void RecordStyleChoice(
    const std::vector<int>& legal,
    int selected_action,
    const std::vector<double>& probabilities,
    bool sample_opportunities,
    StyleStats& stats) {
  ++stats.total_decisions;
  const ActionKey selected = DecodeAction(selected_action);
  if (selected.kind == ActionKind::kPlayCard ||
      selected.kind == ActionKind::kPlayAsColt) {
    ++stats.cards_played;
  }
  if (selected.kind == ActionKind::kPlayCard &&
      GetCard(GetCardInstance(selected.card_instance).name).kind ==
          CardKind::kEquipment) {
    ++stats.equipment_played;
  }
  if (selected.target == static_cast<int>(Target::kOpponentActive) ||
      selected.target == static_cast<int>(Target::kOpponentRearguard)) {
    ++stats.opponent_targets;
    stats.rear_targets +=
        selected.target == static_cast<int>(Target::kOpponentRearguard)
            ? 1
            : 0;
  }
  if (HasCard(selected)) {
    const auto name = GetCardInstance(selected.card_instance).name;
    ++stats.card_usage[static_cast<std::size_t>(name)];
  }
  if (!sample_opportunities) return;

  ++stats.decisions;
  bool attack_available = false;
  bool take_hit_available = false;
  bool swap_available = false;
  bool ability_available = false;
  std::array<bool, kCardNameCount> cards_available{};
  for (const int action_id : legal) {
    const ActionKey action = DecodeAction(action_id);
    attack_available = attack_available || IsAttack(action);
    take_hit_available =
        take_hit_available || action.kind == ActionKind::kTakeHit;
    swap_available = swap_available || action.kind == ActionKind::kSwap;
    ability_available =
        ability_available || action.kind == ActionKind::kUseAbility;
    if (HasCard(action)) {
      const auto name = GetCardInstance(action.card_instance).name;
      cards_available[static_cast<std::size_t>(name)] = true;
    }
  }
  const bool defense_available = take_hit_available && legal.size() > 1;
  stats.attack_opportunities += attack_available ? 1 : 0;
  stats.defense_opportunities += defense_available ? 1 : 0;
  stats.swap_opportunities += swap_available ? 1 : 0;
  stats.ability_opportunities += ability_available ? 1 : 0;
  for (std::size_t index = 0; index < cards_available.size(); ++index) {
    stats.card_opportunities[index] += cards_available[index] ? 1 : 0;
  }

  stats.attacks += IsAttack(selected) ? 1 : 0;
  stats.defenses +=
      defense_available && selected.kind != ActionKind::kTakeHit ? 1 : 0;
  stats.swaps += selected.kind == ActionKind::kSwap ? 1 : 0;
  stats.abilities += selected.kind == ActionKind::kUseAbility ? 1 : 0;
  if (HasCard(selected)) {
    const auto name = GetCardInstance(selected.card_instance).name;
    ++stats.card_selections[static_cast<std::size_t>(name)];
  }

  double entropy = 0.0;
  double largest = 0.0;
  double second_largest = 0.0;
  for (const double probability : probabilities) {
    if (probability >= largest) {
      second_largest = largest;
      largest = probability;
    } else if (probability > second_largest) {
      second_largest = probability;
    }
    if (probability > 0.0) entropy -= probability * std::log(probability);
  }
  const double maximum_entropy = std::log(
      static_cast<double>(probabilities.size()));
  stats.normalized_entropy +=
      maximum_entropy > 0.0 ? entropy / maximum_entropy : 0.0;
  stats.top_probability += largest;
  stats.probability_margin += largest - second_largest;
  const auto selected_row = std::find(
      legal.begin(), legal.end(), selected_action);
  if (selected_row != legal.end()) {
    stats.selected_probability += probabilities[
        static_cast<std::size_t>(selected_row - legal.begin())];
  }
}

double Uniform(FastRng& rng) {
  return static_cast<double>(rng.NextU64() >> 11U) /
      static_cast<double>(1ULL << 53U);
}

std::size_t WeightedIndex(
    const std::vector<TrainingOpponentSpec>& opponents,
    FastRng& rng) {
  double total = 0.0;
  for (const auto& opponent : opponents) {
    total += std::max(0.0, opponent.probability);
  }
  if (!(total > 0.0)) {
    throw std::invalid_argument("opponent probabilities must have positive mass");
  }
  double choice = Uniform(rng) * total;
  for (std::size_t index = 0; index < opponents.size(); ++index) {
    choice -= std::max(0.0, opponents[index].probability);
    if (choice <= 0.0) return index;
  }
  return opponents.size() - 1;
}

std::vector<RuntimeOpponent> LoadOpponents(
    const std::vector<TrainingOpponentSpec>& specs) {
  std::vector<RuntimeOpponent> result;
  result.reserve(specs.size());
  for (const auto& spec : specs) {
    RuntimeOpponent runtime{.spec = spec};
    if (spec.kind == TrainingOpponentKind::kCheckpoint) {
      runtime.network = LoadPolicyFromCheckpoint(spec.checkpoint);
      runtime.network->eval();
    }
    result.push_back(std::move(runtime));
  }
  return result;
}

struct FlatBatch {
  torch::Tensor observations;
  torch::Tensor privileged;
  torch::Tensor features;
  torch::Tensor decision_ids;
  std::vector<int> counts;
};

FlatBatch MakeDecisionBatch(
    const std::vector<Episode>& episodes,
    const std::vector<PendingDecision>& pending,
    const std::vector<Transition>* transitions = nullptr) {
  std::vector<float> observations;
  std::vector<float> privileged;
  std::vector<float> features;
  std::vector<std::int64_t> decision_ids;
  std::vector<int> counts;
  observations.reserve(pending.size() * kTrainingObservationSize);
  privileged.reserve(pending.size() * kPrivilegedObservationSize);
  for (std::size_t decision = 0; decision < pending.size(); ++decision) {
    const auto& item = pending[decision];
    if (transitions == nullptr) {
      const auto observation =
          episodes[item.episode].state.TrainingObservationTensor(
              episodes[item.episode].state.CurrentPlayer());
      const auto private_state =
          episodes[item.episode].state.PrivilegedObservationTensor(
              episodes[item.episode].state.CurrentPlayer());
      observations.insert(
          observations.end(), observation.begin(), observation.end());
      privileged.insert(
          privileged.end(), private_state.begin(), private_state.end());
      for (const int action : item.actions) {
        const auto feature = EncodeAction(action);
        features.insert(
            features.end(), feature.values.begin(), feature.values.end());
        decision_ids.push_back(static_cast<std::int64_t>(decision));
      }
    } else {
      const auto& transition = (*transitions)[item.transition];
      observations.insert(
          observations.end(),
          transition.observation.begin(),
          transition.observation.end());
      privileged.insert(
          privileged.end(),
          transition.privileged.begin(),
          transition.privileged.end());
      for (const auto& feature : transition.action_features) {
        features.insert(
            features.end(), feature.values.begin(), feature.values.end());
        decision_ids.push_back(static_cast<std::int64_t>(decision));
      }
    }
    counts.push_back(static_cast<int>(item.actions.size()));
    if (transitions != nullptr) {
      counts.back() = static_cast<int>(
          (*transitions)[item.transition].action_features.size());
    }
  }
  const auto float_options =
      torch::TensorOptions().dtype(torch::kFloat32);
  const auto long_options =
      torch::TensorOptions().dtype(torch::kInt64);
  return {
      .observations = torch::from_blob(
          observations.data(),
          {static_cast<long>(pending.size()), kTrainingObservationSize},
          float_options).clone(),
      .privileged = torch::from_blob(
          privileged.data(),
          {static_cast<long>(pending.size()), kPrivilegedObservationSize},
          float_options).clone(),
      .features = torch::from_blob(
          features.data(),
          {static_cast<long>(decision_ids.size()), kActionFeatureSize},
          float_options).clone(),
      .decision_ids = torch::from_blob(
          decision_ids.data(),
          {static_cast<long>(decision_ids.size())},
          long_options).clone(),
      .counts = std::move(counts),
  };
}

std::vector<int> SelectPolicyActions(
    BangActorCritic& network,
    const std::vector<Episode>& episodes,
    const std::vector<PendingDecision>& pending,
    std::vector<FastRng*>& rngs,
    bool greedy) {
  if (pending.empty()) return {};
  const auto batch = MakeDecisionBatch(episodes, pending);
  torch::NoGradGuard no_grad;
  const auto output = network->Forward(
      batch.observations, batch.features, batch.decision_ids);
  const auto logits = output.logits.to(torch::kCPU).contiguous();
  const float* values = logits.data_ptr<float>();
  std::vector<int> result;
  result.reserve(pending.size());
  long offset = 0;
  for (std::size_t decision = 0; decision < pending.size(); ++decision) {
    const int count = batch.counts[decision];
    int selected = 0;
    if (greedy) {
      for (int row = 1; row < count; ++row) {
        if (values[offset + row] > values[offset + selected]) selected = row;
      }
    } else {
      float maximum = values[offset];
      for (int row = 1; row < count; ++row) {
        maximum = std::max(maximum, values[offset + row]);
      }
      double total = 0.0;
      std::vector<double> weights(static_cast<std::size_t>(count));
      for (int row = 0; row < count; ++row) {
        weights[static_cast<std::size_t>(row)] =
            std::exp(values[offset + row] - maximum);
        total += weights[static_cast<std::size_t>(row)];
      }
      double choice = Uniform(*rngs[decision]) * total;
      for (int row = 0; row < count; ++row) {
        choice -= weights[static_cast<std::size_t>(row)];
        if (choice <= 0.0) {
          selected = row;
          break;
        }
      }
    }
    result.push_back(
        pending[decision].actions[static_cast<std::size_t>(selected)]);
    offset += count;
  }
  return result;
}

void ApplyLearnerBatch(
    BangActorCritic& network,
    std::vector<Episode>& episodes,
    std::vector<Transition>& transitions,
    const std::vector<PendingDecision>& pending,
    std::int64_t& environment_steps,
    StyleStats& style) {
  if (pending.empty()) return;
  const auto batch = MakeDecisionBatch(episodes, pending, &transitions);
  torch::NoGradGuard no_grad;
  const auto output = network->Forward(
      batch.observations,
      batch.features,
      batch.decision_ids,
      batch.privileged);
  const auto logits = output.logits.to(torch::kCPU).contiguous();
  const auto values = output.value.to(torch::kCPU).contiguous();
  const float* logit_values = logits.data_ptr<float>();
  const float* value_values = values.data_ptr<float>();
  long offset = 0;
  for (std::size_t decision = 0; decision < pending.size(); ++decision) {
    const auto& item = pending[decision];
    auto& episode = episodes[item.episode];
    auto& transition = transitions[item.transition];
    const int count = batch.counts[decision];
    float maximum = logit_values[offset];
    for (int row = 1; row < count; ++row) {
      maximum = std::max(maximum, logit_values[offset + row]);
    }
    std::vector<double> weights(static_cast<std::size_t>(count));
    double total = 0.0;
    for (int row = 0; row < count; ++row) {
      weights[static_cast<std::size_t>(row)] =
          std::exp(logit_values[offset + row] - maximum);
      total += weights[static_cast<std::size_t>(row)];
    }
    double choice = Uniform(episode.policy_rng) * total;
    int selected = count - 1;
    for (int row = 0; row < count; ++row) {
      choice -= weights[static_cast<std::size_t>(row)];
      if (choice <= 0.0) {
        selected = row;
        break;
      }
    }
    transition.selected_row = selected;
    transition.old_log_probability = static_cast<float>(
        std::log(weights[static_cast<std::size_t>(selected)] / total + 1e-12));
    transition.value = value_values[decision];
    for (double& probability : weights) probability /= total;
    const int selected_action =
        item.actions[static_cast<std::size_t>(selected)];
    RecordStyleChoice(
        item.actions,
        selected_action,
        weights,
        item.transition % 4 == 0,
        style);
    RecordTurnIfEnded(
        episode.learner,
        episode.state.CurrentPlayer(),
        selected_action,
        style);
    episode.state.ApplyAction(selected_action);
    ++episode.steps;
    ++environment_steps;
    offset += count;
  }
}

void ComputeAdvantages(
    std::vector<Transition>& transitions,
    const std::vector<std::size_t>& indexes,
    float lambda) {
  float advantage = 0.0F;
  for (std::size_t reverse = indexes.size(); reverse > 0; --reverse) {
    const std::size_t local = reverse - 1;
    auto& transition = transitions[indexes[local]];
    const float next_value =
        local + 1 < indexes.size()
            ? transitions[indexes[local + 1]].value
            : 0.0F;
    const float delta =
        transition.reward + next_value - transition.value;
    advantage = delta + lambda * advantage;
    transition.advantage = advantage;
    transition.return_value = transition.value + advantage;
  }
}

void NormalizeAdvantages(std::vector<Transition>& transitions) {
  if (transitions.empty()) return;
  double mean = 0.0;
  for (const auto& transition : transitions) mean += transition.advantage;
  mean /= static_cast<double>(transitions.size());
  double variance = 0.0;
  for (const auto& transition : transitions) {
    const double centered = transition.advantage - mean;
    variance += centered * centered;
  }
  const double standard_deviation =
      std::sqrt(variance / transitions.size() + 1e-8);
  for (auto& transition : transitions) {
    transition.advantage = static_cast<float>(
        (transition.advantage - mean) / standard_deviation);
  }
}

struct RolloutResult {
  struct OpponentStats {
    std::string id;
    TrainingOpponentKind kind = TrainingOpponentKind::kHeuristic;
    int games = 0;
    int wins = 0;
    int law_games = 0;
    int law_wins = 0;
    int outlaw_games = 0;
    int outlaw_wins = 0;
  };

  std::vector<Transition> transitions;
  std::vector<OpponentStats> opponents;
  StyleStats style;
  std::int64_t environment_steps = 0;
  int law_wins = 0;
  int outlaw_wins = 0;
  int learner_wins = 0;
  int learner_law_games = 0;
  int learner_law_wins = 0;
  int learner_outlaw_games = 0;
  int learner_outlaw_wins = 0;
  int truncated = 0;
};

RolloutResult CollectRollouts(
    BangActorCritic& network,
    const TrainingConfig& config,
    int iteration) {
  std::vector<TrainingOpponentSpec> opponents = config.opponents;
  if (opponents.empty()) {
    opponents = {
        {"heuristic", TrainingOpponentKind::kHeuristic, {}, 0.7},
        {"random", TrainingOpponentKind::kRandom, {}, 0.3},
    };
  }
  auto runtime_opponents = LoadOpponents(opponents);
  FastRng selection_rng(
      static_cast<std::uint64_t>(config.seed) ^
      (static_cast<std::uint64_t>(iteration) << 32U));
  std::vector<Episode> episodes;
  episodes.reserve(static_cast<std::size_t>(config.batch_games));
  for (int game = 0; game < config.batch_games; ++game) {
    const std::uint64_t game_seed =
        static_cast<std::uint64_t>(config.seed) +
        static_cast<std::uint64_t>(iteration) * 1'000'003ULL +
        static_cast<std::uint64_t>(game);
    episodes.emplace_back(
        GameState(GameConfig{
            .seed = game_seed,
            .characters_per_side = config.characters_per_side}),
        (iteration + game) % kSideCount,
        static_cast<int>(WeightedIndex(opponents, selection_rng)),
        game_seed ^ 0x9e3779b97f4a7c15ULL);
  }

  RolloutResult result;
  result.opponents.reserve(opponents.size());
  for (const auto& opponent : opponents) {
    result.opponents.push_back({
        .id = opponent.id,
        .kind = opponent.kind,
    });
  }
  result.transitions.reserve(
      static_cast<std::size_t>(config.batch_games) * 96);
  int unfinished = config.batch_games;
  while (unfinished > 0) {
    std::vector<PendingDecision> learner_pending;
    std::vector<std::vector<PendingDecision>> opponent_pending(
        runtime_opponents.size());
    for (std::size_t index = 0; index < episodes.size(); ++index) {
      auto& episode = episodes[index];
      if (episode.finished) continue;
      if (episode.state.IsTerminal() ||
          episode.steps >= config.max_game_steps) {
        if (episode.state.IsTerminal()) {
          const int winner = episode.state.Winner();
          ++(winner == 0 ? result.law_wins : result.outlaw_wins);
          if (winner == episode.learner) ++result.learner_wins;
          auto& opponent =
              result.opponents[static_cast<std::size_t>(episode.opponent)];
          ++opponent.games;
          opponent.wins += winner == episode.learner ? 1 : 0;
          if (episode.learner == static_cast<int>(Side::kLaw)) {
            ++result.learner_law_games;
            ++opponent.law_games;
            result.learner_law_wins += winner == episode.learner ? 1 : 0;
            opponent.law_wins += winner == episode.learner ? 1 : 0;
          } else {
            ++result.learner_outlaw_games;
            ++opponent.outlaw_games;
            result.learner_outlaw_wins += winner == episode.learner ? 1 : 0;
            opponent.outlaw_wins += winner == episode.learner ? 1 : 0;
          }
          if (!episode.transition_indexes.empty()) {
            result.transitions[episode.transition_indexes.back()].reward =
                episode.state.Returns()[
                    static_cast<std::size_t>(episode.learner)];
          }
        } else {
          ++result.truncated;
        }
        ComputeAdvantages(
            result.transitions,
            episode.transition_indexes,
            config.gae_lambda);
        episode.finished = true;
        --unfinished;
        continue;
      }
      const auto legal = episode.state.LegalActions();
      if (legal.empty()) {
        throw std::runtime_error("non-terminal rollout has no legal action");
      }
      const int player = episode.state.CurrentPlayer();
      if (legal.size() == 1) {
        RecordTurnIfEnded(
            episode.learner, player, legal.front(), result.style);
        episode.state.ApplyAction(legal.front());
        ++episode.steps;
        ++result.environment_steps;
        continue;
      }
      if (player == episode.learner) {
        Transition transition{
            .observation =
                episode.state.TrainingObservationTensor(player),
            .privileged =
                episode.state.PrivilegedObservationTensor(player),
            .action_features = {},
            .player = player,
        };
        transition.action_features.reserve(legal.size());
        for (const int action : legal) {
          transition.action_features.push_back(EncodeAction(action));
        }
        const std::size_t transition_index = result.transitions.size();
        result.transitions.push_back(std::move(transition));
        episode.transition_indexes.push_back(transition_index);
        learner_pending.push_back({
            .episode = index,
            .transition = transition_index,
            .actions = legal,
        });
        continue;
      }
      auto& opponent =
          runtime_opponents[static_cast<std::size_t>(episode.opponent)];
      if (opponent.spec.kind == TrainingOpponentKind::kRandom) {
        episode.state.ApplyAction(
            legal[episode.policy_rng.USize(legal.size())]);
        ++episode.steps;
        ++result.environment_steps;
      } else if (
          opponent.spec.kind == TrainingOpponentKind::kHeuristic) {
        episode.state.ApplyAction(
            ChooseHeuristicAction(episode.state, episode.policy_rng));
        ++episode.steps;
        ++result.environment_steps;
      } else {
        opponent_pending[static_cast<std::size_t>(episode.opponent)]
            .push_back({.episode = index, .actions = legal});
      }
    }

    for (std::size_t opponent_index = 0;
         opponent_index < opponent_pending.size();
         ++opponent_index) {
      auto& pending = opponent_pending[opponent_index];
      if (pending.empty()) continue;
      std::vector<FastRng*> rngs;
      rngs.reserve(pending.size());
      for (const auto& decision : pending) {
        rngs.push_back(&episodes[decision.episode].policy_rng);
      }
      auto actions = SelectPolicyActions(
          runtime_opponents[opponent_index].network,
          episodes,
          pending,
          rngs,
          false);
      for (std::size_t decision = 0; decision < pending.size(); ++decision) {
        auto& episode = episodes[pending[decision].episode];
        episode.state.ApplyAction(actions[decision]);
        ++episode.steps;
        ++result.environment_steps;
      }
    }
    ApplyLearnerBatch(
        network,
        episodes,
        result.transitions,
        learner_pending,
        result.environment_steps,
        result.style);
  }
  NormalizeAdvantages(result.transitions);
  return result;
}

struct Minibatch {
  torch::Tensor observations;
  torch::Tensor privileged;
  torch::Tensor features;
  torch::Tensor decision_ids;
  torch::Tensor selected_rows;
  torch::Tensor old_log_probabilities;
  torch::Tensor returns;
  torch::Tensor advantages;
};

Minibatch MakeMinibatch(
    const std::vector<Transition>& transitions,
    const std::vector<int>& indexes) {
  std::vector<float> observations;
  std::vector<float> privileged;
  std::vector<float> features;
  std::vector<std::int64_t> decision_ids;
  std::vector<std::int64_t> selected_rows;
  std::vector<float> old_log_probabilities;
  std::vector<float> returns;
  std::vector<float> advantages;
  long row_offset = 0;
  for (std::size_t decision = 0; decision < indexes.size(); ++decision) {
    const auto& transition =
        transitions[static_cast<std::size_t>(indexes[decision])];
    observations.insert(
        observations.end(),
        transition.observation.begin(),
        transition.observation.end());
    privileged.insert(
        privileged.end(),
        transition.privileged.begin(),
        transition.privileged.end());
    for (const auto& feature : transition.action_features) {
      features.insert(
          features.end(), feature.values.begin(), feature.values.end());
      decision_ids.push_back(static_cast<std::int64_t>(decision));
    }
    selected_rows.push_back(row_offset + transition.selected_row);
    row_offset += static_cast<long>(transition.action_features.size());
    old_log_probabilities.push_back(transition.old_log_probability);
    returns.push_back(transition.return_value);
    advantages.push_back(transition.advantage);
  }
  const auto floats = torch::TensorOptions().dtype(torch::kFloat32);
  const auto longs = torch::TensorOptions().dtype(torch::kInt64);
  return {
      .observations = torch::from_blob(
          observations.data(),
          {static_cast<long>(indexes.size()), kTrainingObservationSize},
          floats).clone(),
      .privileged = torch::from_blob(
          privileged.data(),
          {static_cast<long>(indexes.size()), kPrivilegedObservationSize},
          floats).clone(),
      .features = torch::from_blob(
          features.data(),
          {static_cast<long>(decision_ids.size()), kActionFeatureSize},
          floats).clone(),
      .decision_ids = torch::from_blob(
          decision_ids.data(),
          {static_cast<long>(decision_ids.size())},
          longs).clone(),
      .selected_rows = torch::from_blob(
          selected_rows.data(),
          {static_cast<long>(selected_rows.size())},
          longs).clone(),
      .old_log_probabilities = torch::from_blob(
          old_log_probabilities.data(),
          {static_cast<long>(old_log_probabilities.size())},
          floats).clone(),
      .returns = torch::from_blob(
          returns.data(),
          {static_cast<long>(returns.size())},
          floats).clone(),
      .advantages = torch::from_blob(
          advantages.data(),
          {static_cast<long>(advantages.size())},
          floats).clone(),
  };
}

Minibatch ToDevice(Minibatch batch, const torch::Device& device) {
  if (device.is_cpu()) return batch;
  for (auto* tensor : {
           &batch.observations, &batch.privileged, &batch.features,
           &batch.decision_ids, &batch.selected_rows,
           &batch.old_log_probabilities, &batch.returns, &batch.advantages}) {
    *tensor = tensor->to(device);
  }
  return batch;
}

struct SegmentedSoftmax {
  torch::Tensor log_probabilities;
  torch::Tensor probabilities;
};

SegmentedSoftmax SegmentedLogSoftmax(
    const torch::Tensor& logits,
    const torch::Tensor& decision_ids,
    long decision_count) {
  // Legal action sets have different sizes. A segmented softmax avoids both
  // padding to the largest action set and thousands of tiny autograd graphs.
  const auto maxima = torch::full(
      {decision_count},
      -std::numeric_limits<float>::infinity(),
      logits.options());
  maxima.scatter_reduce_(
      0, decision_ids, logits.detach(), "amax", true);
  const auto shifted =
      logits - maxima.index_select(0, decision_ids);
  const auto weights = torch::exp(shifted);
  const auto totals = torch::zeros(
      {decision_count}, logits.options()).index_add(
          0, decision_ids, weights);
  const auto row_totals = totals.index_select(0, decision_ids);
  return {
      .log_probabilities = shifted - torch::log(row_totals),
      .probabilities = weights / row_totals,
  };
}

// Log-probability of each legal row under uniform play over its decision.
torch::Tensor UniformLogProbabilities(
    const torch::Tensor& decision_ids,
    long decision_count,
    const torch::TensorOptions& options) {
  const auto counts = torch::zeros({decision_count}, options).index_add(
      0, decision_ids, torch::ones({decision_ids.size(0)}, options));
  return -torch::log(counts).index_select(0, decision_ids);
}

struct PolicyTerms {
  torch::Tensor selected_log_probabilities;
  torch::Tensor entropies;
  torch::Tensor magnet_kl;  // Per decision; undefined without a magnet.
};

PolicyTerms ComputePolicyTerms(
    const torch::Tensor& logits,
    const torch::Tensor& decision_ids,
    const torch::Tensor& selected_rows,
    long decision_count,
    const torch::Tensor& magnet_log_probabilities) {
  const auto [log_probabilities, probabilities] =
      SegmentedLogSoftmax(logits, decision_ids, decision_count);
  const auto segment_sum = [&](const torch::Tensor& values) {
    return torch::zeros({decision_count}, logits.options())
        .index_add(0, decision_ids, values);
  };
  PolicyTerms terms{
      .selected_log_probabilities =
          log_probabilities.index_select(0, selected_rows),
      .entropies = segment_sum(-(probabilities * log_probabilities)),
      .magnet_kl = {},
  };
  if (magnet_log_probabilities.defined()) {
    terms.magnet_kl = segment_sum(
        probabilities * (log_probabilities - magnet_log_probabilities));
  }
  return terms;
}

// `magnet` is null while the MMD magnet is still uniform play.
UpdateStats UpdateNetwork(
    BangActorCritic& network,
    BangActorCritic* magnet,
    torch::optim::Adam& optimizer,
    const std::vector<Transition>& transitions,
    const TrainingConfig& config,
    int iteration) {
  std::vector<int> indexes(transitions.size());
  std::iota(indexes.begin(), indexes.end(), 0);
  std::mt19937 random(
      static_cast<std::uint32_t>(config.seed + iteration * 7919));
  const int minibatch_size = std::max(
      1,
      static_cast<int>(
          (indexes.size() + config.minibatches - 1) /
          config.minibatches));
  const torch::Device device = network->parameters().front().device();
  UpdateStats stats;
  network->train();
  for (int epoch = 0; epoch < config.update_epochs; ++epoch) {
    std::shuffle(indexes.begin(), indexes.end(), random);
    double epoch_kl = 0.0;
    int epoch_updates = 0;
    for (int start = 0; start < static_cast<int>(indexes.size());
         start += minibatch_size) {
      const int end = std::min(
          start + minibatch_size,
          static_cast<int>(indexes.size()));
      const std::vector<int> selected_indexes(
          indexes.begin() + start, indexes.begin() + end);
      const auto batch =
          ToDevice(MakeMinibatch(transitions, selected_indexes), device);
      const auto output = network->Forward(
          batch.observations,
          batch.features,
          batch.decision_ids,
          batch.privileged);
      const long decision_count =
          static_cast<long>(batch.observations.size(0));
      torch::Tensor magnet_log_probabilities;
      if (config.mmd_alpha > 0.0) {
        torch::NoGradGuard no_grad;
        magnet_log_probabilities =
            magnet == nullptr
                ? UniformLogProbabilities(
                      batch.decision_ids, decision_count,
                      output.logits.options())
                : SegmentedLogSoftmax(
                      (*magnet)->Forward(
                          batch.observations,
                          batch.features,
                          batch.decision_ids,
                          batch.privileged).logits,
                      batch.decision_ids,
                      decision_count).log_probabilities;
      }
      const auto terms = ComputePolicyTerms(
          output.logits,
          batch.decision_ids,
          batch.selected_rows,
          decision_count,
          magnet_log_probabilities);
      const auto& new_log_probabilities = terms.selected_log_probabilities;
      const auto& entropies = terms.entropies;
      const auto log_ratio =
          new_log_probabilities - batch.old_log_probabilities;
      const auto ratio = torch::exp(log_ratio);
      const auto unclipped = ratio * batch.advantages;
      const auto clipped =
          torch::clamp(
              ratio,
              1.0 - config.clip_range,
              1.0 + config.clip_range) *
          batch.advantages;
      const auto policy_loss = -torch::minimum(unclipped, clipped).mean();
      const auto value_loss =
          torch::mse_loss(output.value, batch.returns);
      const auto auxiliary_loss =
          torch::mse_loss(output.auxiliary_value, batch.returns);
      const auto entropy = entropies.mean();
      auto loss =
          policy_loss +
          config.value_loss_coef * value_loss +
          config.auxiliary_value_loss_coef * auxiliary_loss -
          config.entropy_coef * entropy;
      if (terms.magnet_kl.defined()) {
        const auto magnet_kl = terms.magnet_kl.mean();
        loss = loss + config.mmd_alpha * magnet_kl;
        stats.magnet_kl += magnet_kl.item<double>();
      }
      optimizer.zero_grad();
      loss.backward();
      stats.gradient_norm +=
          torch::nn::utils::clip_grad_norm_(network->parameters(), 0.5);
      optimizer.step();

      const double approximate_kl =
          ((ratio.detach() - 1.0) - log_ratio.detach()).mean().item<double>();
      stats.policy_loss += policy_loss.item<double>();
      stats.value_loss += value_loss.item<double>();
      stats.auxiliary_loss += auxiliary_loss.item<double>();
      stats.entropy += entropy.item<double>();
      stats.kl += approximate_kl;
      stats.clip_fraction +=
          (torch::abs(ratio.detach() - 1.0) > config.clip_range)
              .to(torch::kFloat32)
              .mean()
              .item<double>();
      epoch_kl += approximate_kl;
      ++epoch_updates;
      ++stats.minibatch_updates;
    }
    ++stats.epochs;
    if (epoch_updates > 0 &&
        config.target_kl > 0.0 &&
        epoch_kl / epoch_updates > config.target_kl) {
      stats.early_stopped = true;
      break;
    }
  }
  if (stats.minibatch_updates > 0) {
    const double divisor = stats.minibatch_updates;
    stats.policy_loss /= divisor;
    stats.value_loss /= divisor;
    stats.auxiliary_loss /= divisor;
    stats.entropy /= divisor;
    stats.kl /= divisor;
    stats.magnet_kl /= divisor;
    stats.clip_fraction /= divisor;
    stats.gradient_norm /= divisor;
  }
  network->eval();
  return stats;
}

std::string Percent(double value) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(1) << value * 100.0 << '%';
  return output.str();
}

std::string OpportunityRate(
    std::int64_t selected,
    std::int64_t opportunities) {
  return opportunities == 0 ? "n/a" : Percent(Rate(selected, opportunities));
}

std::string OpponentKindName(TrainingOpponentKind kind) {
  switch (kind) {
    case TrainingOpponentKind::kRandom: return "random";
    case TrainingOpponentKind::kHeuristic: return "heuristic";
    case TrainingOpponentKind::kCheckpoint: return "checkpoint";
  }
  throw std::logic_error("unknown training opponent kind");
}

double CriticExplainedVariance(const std::vector<Transition>& transitions) {
  if (transitions.size() < 2) return 0.0;
  double return_mean = 0.0;
  double residual_mean = 0.0;
  for (const auto& transition : transitions) {
    return_mean += transition.return_value;
    residual_mean += transition.return_value - transition.value;
  }
  return_mean /= static_cast<double>(transitions.size());
  residual_mean /= static_cast<double>(transitions.size());
  double return_variance = 0.0;
  double residual_variance = 0.0;
  for (const auto& transition : transitions) {
    const double centered_return = transition.return_value - return_mean;
    const double centered_residual =
        transition.return_value - transition.value - residual_mean;
    return_variance += centered_return * centered_return;
    residual_variance += centered_residual * centered_residual;
  }
  if (return_variance <= 1e-12) return 0.0;
  return 1.0 - residual_variance / return_variance;
}

JsonValue OpponentMetricsJson(
    const std::vector<RolloutResult::OpponentStats>& opponents) {
  JsonValue::Array result;
  result.reserve(opponents.size());
  for (const auto& opponent : opponents) {
    result.emplace_back(JsonValue::Object{
        {"id", opponent.id},
        {"kind", OpponentKindName(opponent.kind)},
        {"games", opponent.games},
        {"wins", opponent.wins},
        {"winRate", Rate(opponent.wins, opponent.games)},
        {"lawGames", opponent.law_games},
        {"lawWins", opponent.law_wins},
        {"lawWinRate", Rate(opponent.law_wins, opponent.law_games)},
        {"outlawGames", opponent.outlaw_games},
        {"outlawWins", opponent.outlaw_wins},
        {"outlawWinRate", Rate(opponent.outlaw_wins, opponent.outlaw_games)},
    });
  }
  return result;
}

JsonValue StyleMetricsJson(const StyleStats& style) {
  JsonValue::Array cards;
  for (std::size_t index = 0; index < style.card_selections.size(); ++index) {
    if (style.card_opportunities[index] == 0 &&
        style.card_selections[index] == 0 &&
        style.card_usage[index] == 0) {
      continue;
    }
    const auto name = static_cast<CardName>(index);
    cards.emplace_back(JsonValue::Object{
        {"card", std::string(GetCard(name).key)},
        {"used", static_cast<double>(style.card_usage[index])},
        {"selected", static_cast<double>(style.card_selections[index])},
        {"opportunities", static_cast<double>(style.card_opportunities[index])},
        {"selectionRate", Rate(
             style.card_selections[index],
             style.card_opportunities[index])},
    });
  }
  return JsonValue::Object{
      {"totalDecisions", static_cast<double>(style.total_decisions)},
      {"decisions", static_cast<double>(style.decisions)},
      {"sampleRate", Rate(style.decisions, style.total_decisions)},
      {"turns", static_cast<double>(style.turns)},
      {"cardsPlayed", static_cast<double>(style.cards_played)},
      {"cardsPerTurn", Rate(style.cards_played, style.turns)},
      {"equipmentPlayed", static_cast<double>(style.equipment_played)},
      {"equipmentShare", Rate(style.equipment_played, style.cards_played)},
      {"attackOpportunities", static_cast<double>(style.attack_opportunities)},
      {"attacks", static_cast<double>(style.attacks)},
      {"attackRate", Rate(style.attacks, style.attack_opportunities)},
      {"defenseOpportunities", static_cast<double>(style.defense_opportunities)},
      {"defenses", static_cast<double>(style.defenses)},
      {"defenseRate", Rate(style.defenses, style.defense_opportunities)},
      {"swapOpportunities", static_cast<double>(style.swap_opportunities)},
      {"swaps", static_cast<double>(style.swaps)},
      {"swapRate", Rate(style.swaps, style.swap_opportunities)},
      {"abilityOpportunities", static_cast<double>(style.ability_opportunities)},
      {"abilities", static_cast<double>(style.abilities)},
      {"abilityRate", Rate(style.abilities, style.ability_opportunities)},
      {"opponentTargets", static_cast<double>(style.opponent_targets)},
      {"rearTargets", static_cast<double>(style.rear_targets)},
      {"rearTargetRate", Rate(style.rear_targets, style.opponent_targets)},
      {"normalizedEntropy", style.decisions == 0
           ? 0.0
           : style.normalized_entropy /
               static_cast<double>(style.decisions)},
      {"topActionProbability", style.decisions == 0
           ? 0.0
           : style.top_probability / static_cast<double>(style.decisions)},
      {"selectedActionProbability", style.decisions == 0
           ? 0.0
           : style.selected_probability / static_cast<double>(style.decisions)},
      {"topActionMargin", style.decisions == 0
           ? 0.0
           : style.probability_margin / static_cast<double>(style.decisions)},
      {"cards", std::move(cards)},
  };
}

std::string TopCards(const StyleStats& style) {
  std::vector<std::size_t> indexes(kCardNameCount);
  std::iota(indexes.begin(), indexes.end(), 0);
  std::sort(
      indexes.begin(),
      indexes.end(),
      [&style](std::size_t left, std::size_t right) {
        if (style.card_usage[left] != style.card_usage[right]) {
          return style.card_usage[left] > style.card_usage[right];
        }
        return left < right;
      });
  std::ostringstream output;
  int printed = 0;
  for (const std::size_t index : indexes) {
    if (style.card_usage[index] == 0 || printed == 3) break;
    if (printed > 0) output << ',';
    output << GetCard(static_cast<CardName>(index)).display_name;
    ++printed;
  }
  return printed == 0 ? "none" : output.str();
}

void MergeOpponentStats(
    std::vector<RolloutResult::OpponentStats>& totals,
    const std::vector<RolloutResult::OpponentStats>& update) {
  for (const auto& source : update) {
    auto found = std::find_if(
        totals.begin(),
        totals.end(),
        [&source](const auto& candidate) {
          return candidate.id == source.id;
        });
    if (found == totals.end()) {
      totals.push_back(source);
      continue;
    }
    found->games += source.games;
    found->wins += source.wins;
    found->law_games += source.law_games;
    found->law_wins += source.law_wins;
    found->outlaw_games += source.outlaw_games;
    found->outlaw_wins += source.outlaw_wins;
  }
}

struct GroupedStrength {
  int current_games = 0;
  int current_wins = 0;
  int history_games = 0;
  int history_wins = 0;
  int heuristic_games = 0;
  int heuristic_wins = 0;
  int random_games = 0;
  int random_wins = 0;
};

GroupedStrength GroupStrength(
    const std::vector<RolloutResult::OpponentStats>& opponents) {
  GroupedStrength result;
  for (const auto& opponent : opponents) {
    if (opponent.kind == TrainingOpponentKind::kRandom) {
      result.random_games += opponent.games;
      result.random_wins += opponent.wins;
    } else if (opponent.kind == TrainingOpponentKind::kHeuristic) {
      result.heuristic_games += opponent.games;
      result.heuristic_wins += opponent.wins;
    } else if (opponent.id == "current") {
      result.current_games += opponent.games;
      result.current_wins += opponent.wins;
    } else {
      result.history_games += opponent.games;
      result.history_wins += opponent.wins;
    }
  }
  return result;
}

std::deque<double> ReadRecentWinRates(
    const std::filesystem::path& path,
    std::size_t limit) {
  std::deque<double> result;
  std::ifstream input(path);
  std::string line;
  while (std::getline(input, line)) {
    const auto parsed = ParseJson(line);
    if (!parsed) continue;
    const auto* object =
        std::get_if<JsonValue::Object>(&parsed->storage());
    if (object == nullptr) continue;
    const auto found = object->find("learnerWinRate");
    if (found == object->end()) continue;
    const auto* value = std::get_if<double>(&found->second.storage());
    if (value == nullptr) continue;
    result.push_back(*value);
    while (result.size() > limit) result.pop_front();
  }
  return result;
}

double Mean(
    const std::deque<double>& values,
    std::size_t begin,
    std::size_t end) {
  if (begin >= end || end > values.size()) return 0.0;
  return std::accumulate(
             values.begin() + static_cast<std::ptrdiff_t>(begin),
             values.begin() + static_cast<std::ptrdiff_t>(end),
             0.0) /
      static_cast<double>(end - begin);
}

void AppendMetric(
    const std::filesystem::path& path,
    const JsonValue& metric) {
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }
  std::ofstream output(path, std::ios::app);
  if (!output) {
    throw std::runtime_error("failed to append training metrics");
  }
  output << metric.Dump() << '\n';
}

CheckpointMetadata Metadata(
    BangActorCritic& network,
    const TrainingConfig& config,
    int iteration,
    std::int64_t environment_steps,
    std::int64_t learner_decisions,
    double elapsed_seconds) {
  return {
      .model_type = config.model_type,
      .ruleset = "base_four_character",
      .observation_abi = kObservationAbi,
      .action_feature_abi = kActionFeatureAbi,
      .observation_size = kTrainingObservationSize,
      .action_feature_size = kActionFeatureSize,
      .privileged_size = kPrivilegedObservationSize,
      .iteration = iteration,
      .environment_steps = environment_steps,
      .learner_decisions = learner_decisions,
      .parameter_count = CountParameters(*network),
      .seed = config.seed,
      .learning_rate = config.learning_rate,
      .elapsed_seconds = elapsed_seconds,
  };
}

std::filesystem::path TrainingCommitPath(
    const std::filesystem::path& checkpoint) {
  return checkpoint.string() + ".commit.json";
}

std::filesystem::path MagnetPath(const std::filesystem::path& checkpoint) {
  return checkpoint.string() + ".magnet.pt";
}

// Returns the iteration the bundle's MMD magnet was taken at; 0 is uniform.
int ValidateTrainingCommit(
    const std::filesystem::path& checkpoint,
    int expected_iteration) {
  const auto parsed = ParseJson(ReadTextFile(TrainingCommitPath(checkpoint)));
  if (!parsed.has_value()) {
    throw std::runtime_error("training checkpoint commit is invalid JSON");
  }
  const auto* object =
      std::get_if<JsonValue::Object>(&parsed->storage());
  if (object == nullptr) {
    throw std::runtime_error("training checkpoint commit must be an object");
  }
  const auto found = object->find("iteration");
  const auto* iteration =
      found == object->end()
          ? nullptr
          : std::get_if<double>(&found->second.storage());
  if (iteration == nullptr ||
      static_cast<int>(*iteration) != expected_iteration) {
    throw std::runtime_error(
        "training checkpoint bundle was not fully committed");
  }
  const auto magnet = object->find("magnetIteration");
  const auto* magnet_iteration =
      magnet == object->end()
          ? nullptr
          : std::get_if<double>(&magnet->second.storage());
  return magnet_iteration == nullptr ? 0 : static_cast<int>(*magnet_iteration);
}

// `changed_magnet` is written only when the magnet moved this iteration; the
// file otherwise still holds the snapshot named by `magnet_iteration`.
void CommitTrainingCheckpoint(
    const std::filesystem::path& checkpoint,
    BangActorCritic& network,
    torch::optim::Adam& optimizer,
    const CheckpointMetadata& metadata,
    BangActorCritic* changed_magnet,
    int magnet_iteration) {
  const auto commit = TrainingCommitPath(checkpoint);
  std::error_code ignored;
  std::filesystem::remove(commit, ignored);
  SavePolicyCheckpoint(checkpoint, network, metadata);
  SaveOptimizerCheckpoint(
      checkpoint.string() + ".optimizer.pt", optimizer);
  const auto magnet_path = MagnetPath(checkpoint);
  if (magnet_iteration == 0) {
    std::filesystem::remove(magnet_path, ignored);
  } else if (changed_magnet != nullptr) {
    torch::serialize::OutputArchive archive;
    (*changed_magnet)->save(archive);
    const auto temporary = magnet_path.string() + ".tmp";
    archive.save_to(temporary);
    std::filesystem::rename(temporary, magnet_path);
  }
  WriteTextFile(
      commit,
      JsonValue(JsonValue::Object{
          {"schemaVersion", 1},
          {"iteration", metadata.iteration},
          {"magnetIteration", magnet_iteration},
      }).Dump() + "\n");
}

struct EvalMatch {
  GameState state;
  int candidate_seat = 0;
  FastRng rng;
  int steps = 0;
  bool finished = false;

  EvalMatch(std::uint64_t seed, int seat)
      : state(GameConfig{.seed = seed, .characters_per_side = 4}),
        candidate_seat(seat),
        rng(seed ^ 0xd1b54a32d192ed03ULL) {}
};

// Greedy choice of an exported browser policy, matching how checkpoint
// opponents are played during evaluation.
int SelectStaticPolicyAction(
    const StaticPolicyNetwork& policy,
    const GameState& state,
    const std::vector<int>& legal) {
  std::vector<ActionFeature> features;
  features.reserve(legal.size());
  for (const int action : legal) features.push_back(EncodeAction(action));
  const std::vector<float> logits = StaticPolicyLogits(
      policy, state.TrainingObservationTensor(state.CurrentPlayer()), features);
  const auto best = std::max_element(logits.begin(), logits.end());
  return legal[static_cast<std::size_t>(best - logits.begin())];
}

// The opponent is a checkpoint, an exported static policy, or (both null)
// the tactical heuristic.
EvaluationResult Evaluate(
    BangActorCritic& candidate,
    BangActorCritic* opponent,
    const StaticPolicyNetwork* static_opponent,
    int pairs,
    int seed,
    int max_game_steps) {
  std::vector<EvalMatch> matches;
  matches.reserve(static_cast<std::size_t>(pairs * 2));
  for (int pair = 0; pair < pairs; ++pair) {
    for (int seat = 0; seat < kSideCount; ++seat) {
      matches.emplace_back(
          static_cast<std::uint64_t>(seed + pair), seat);
    }
  }
  int unfinished = static_cast<int>(matches.size());
  while (unfinished > 0) {
    std::array<std::vector<std::size_t>, 2> policy_matches;
    for (std::size_t index = 0; index < matches.size(); ++index) {
      auto& match = matches[index];
      if (match.finished) continue;
      if (match.state.IsTerminal() ||
          match.steps >= max_game_steps) {
        match.finished = true;
        --unfinished;
        continue;
      }
      const auto legal = match.state.LegalActions();
      if (legal.size() == 1) {
        match.state.ApplyAction(legal.front());
        ++match.steps;
        continue;
      }
      const bool candidate_turn =
          match.state.CurrentPlayer() == match.candidate_seat;
      if (!candidate_turn && opponent == nullptr) {
        match.state.ApplyAction(
            static_opponent != nullptr
                ? SelectStaticPolicyAction(*static_opponent, match.state, legal)
                : ChooseHeuristicAction(match.state, match.rng));
        ++match.steps;
        continue;
      }
      policy_matches[candidate_turn ? 0 : 1].push_back(index);
    }
    for (int policy = 0; policy < 2; ++policy) {
      if (policy_matches[policy].empty()) continue;
      std::vector<Episode> episodes;
      std::vector<PendingDecision> pending;
      std::vector<FastRng*> rngs;
      episodes.reserve(policy_matches[policy].size());
      pending.reserve(policy_matches[policy].size());
      rngs.reserve(policy_matches[policy].size());
      for (const std::size_t match_index : policy_matches[policy]) {
        auto& match = matches[match_index];
        episodes.emplace_back(
            match.state, 0, 0, match.rng.NextU64());
        pending.push_back({
            .episode = episodes.size() - 1,
            .actions = match.state.LegalActions(),
        });
        rngs.push_back(&episodes.back().policy_rng);
      }
      BangActorCritic& selected_policy =
          policy == 0 ? candidate : *opponent;
      const auto actions = SelectPolicyActions(
          selected_policy, episodes, pending, rngs, true);
      for (std::size_t decision = 0; decision < actions.size(); ++decision) {
        auto& match = matches[policy_matches[policy][decision]];
        match.state.ApplyAction(actions[decision]);
        ++match.steps;
      }
    }
  }
  EvaluationResult result{
      .pairs = pairs,
      .games = pairs * 2,
  };
  for (const auto& match : matches) {
    result.total_steps += match.steps;
    if (!match.state.IsTerminal()) {
      ++result.truncated;
      continue;
    }
    if (match.state.Winner() == match.candidate_seat) {
      ++result.candidate_wins;
      if (match.candidate_seat == 0) {
        ++result.candidate_law_wins;
      } else {
        ++result.candidate_outlaw_wins;
      }
    }
  }
  return result;
}

struct SelfPlayCharacterStats {
  int games = 0;
  int wins = 0;
  int starting_active_games = 0;
  int starting_active_wins = 0;
  int entered_games = 0;
  int initial_position_games = 0;
  int initial_position_wins = 0;
  int reserve_games = 0;
  int reserve_wins = 0;
  int reserve_entered_games = 0;
  int reserve_entered_wins = 0;
  int eliminated = 0;
  std::int64_t total_turns = 0;
};

struct SelfPlayBucket {
  int games = 0;
  int wins = 0;
};

struct SelfPlayStats {
  int requested_games = 0;
  int terminal_games = 0;
  int truncated = 0;
  std::array<int, kSideCount> wins{};
  std::int64_t total_steps = 0;
  std::int64_t total_turns = 0;
  std::int64_t winning_life = 0;
  std::int64_t losing_life = 0;
  std::int64_t winning_characters = 0;
  std::int64_t losing_characters = 0;
  std::vector<int> game_steps;
  std::vector<int> game_turns;
  std::array<SelfPlayCharacterStats, kCharacterCount> characters{};
  std::array<SelfPlayBucket, kCharacterCount * kCharacterCount> pairs{};
  std::array<SelfPlayBucket, kCharacterCount * kCharacterCount> matchups{};
  std::array<SelfPlayBucket, kCharacterCount * kCharacterCount>
      active_matchups{};
};

struct SelfPlayMatch {
  GameState state;
  FastRng rng;
  int steps = 0;
  bool finished = false;
  std::array<std::vector<int>, kSideCount> rosters;
  std::array<int, kSideCount> starting_active{-1, -1};
  std::array<std::array<bool, kCharacterCount>, kSideCount> entered{};
  std::array<std::array<bool, kCharacterCount>, kSideCount>
      initial_position{};

  explicit SelfPlayMatch(std::uint64_t seed)
      : state(GameConfig{.seed = seed, .characters_per_side = 4}),
        rng(seed ^ 0xa0761d6478bd642fULL) {
    const auto& players = state.Players();
    for (int player = 0; player < kSideCount; ++player) {
      const auto& source = players[static_cast<std::size_t>(player)];
      for (const auto& character : source.positions) {
        rosters[static_cast<std::size_t>(player)].push_back(
            character.character_id);
        entered[static_cast<std::size_t>(player)]
               [static_cast<std::size_t>(character.character_id)] = true;
        initial_position[static_cast<std::size_t>(player)]
                        [static_cast<std::size_t>(character.character_id)] =
            true;
      }
      rosters[static_cast<std::size_t>(player)].insert(
          rosters[static_cast<std::size_t>(player)].end(),
          source.reserve.begin(),
          source.reserve.end());
    }
  }

  void Observe() {
    const auto& players = state.Players();
    for (int player = 0; player < kSideCount; ++player) {
      const auto& source = players[static_cast<std::size_t>(player)];
      if (state.SetupComplete() &&
          starting_active[static_cast<std::size_t>(player)] < 0) {
        starting_active[static_cast<std::size_t>(player)] =
            source.positions[static_cast<std::size_t>(source.active_position)]
                .character_id;
      }
      for (const auto& character : source.positions) {
        if (character.character_id >= 0) {
          entered[static_cast<std::size_t>(player)]
                 [static_cast<std::size_t>(character.character_id)] = true;
        }
      }
    }
  }
};

std::size_t CharacterPairIndex(int first, int second) {
  return static_cast<std::size_t>(first * kCharacterCount + second);
}

std::pair<double, double> WilsonInterval(int wins, int games) {
  if (games <= 0) return {0.0, 0.0};
  constexpr double z = 1.959963984540054;
  const double rate = Rate(wins, games);
  const double denominator = 1.0 + z * z / games;
  const double center =
      (rate + z * z / (2.0 * games)) / denominator;
  const double radius =
      z * std::sqrt(
              rate * (1.0 - rate) / games +
              z * z / (4.0 * games * games)) /
      denominator;
  return {
      std::max(0.0, center - radius),
      std::min(1.0, center + radius),
  };
}

int Percentile(std::vector<int> values, double percentile) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const std::size_t index = static_cast<std::size_t>(std::ceil(
      percentile * static_cast<double>(values.size()))) - 1;
  return values[std::min(index, values.size() - 1)];
}

double Mean(const std::vector<int>& values) {
  if (values.empty()) return 0.0;
  return static_cast<double>(
             std::accumulate(
                 values.begin(), values.end(), std::int64_t{0})) /
      static_cast<double>(values.size());
}

int RemainingLife(const PlayerState& player) {
  int result = 0;
  for (const auto& character : player.positions) {
    result += std::max(0, character.life);
  }
  return result;
}

int RemainingCharacters(const PlayerState& player) {
  int result = static_cast<int>(player.reserve.size());
  for (const auto& character : player.positions) {
    result += character.life > 0 ? 1 : 0;
  }
  return result;
}

void RecordSelfPlayMatch(
    SelfPlayMatch& match,
    SelfPlayStats& stats) {
  match.Observe();
  stats.total_steps += match.steps;
  stats.total_turns += match.state.TurnNumber();
  stats.game_steps.push_back(match.steps);
  stats.game_turns.push_back(match.state.TurnNumber());
  if (!match.state.IsTerminal()) {
    ++stats.truncated;
    return;
  }
  ++stats.terminal_games;
  const int winner = match.state.Winner();
  ++stats.wins[static_cast<std::size_t>(winner)];
  const auto& final_players = match.state.Players();
  stats.winning_life +=
      RemainingLife(final_players[static_cast<std::size_t>(winner)]);
  stats.losing_life +=
      RemainingLife(final_players[static_cast<std::size_t>(1 - winner)]);
  stats.winning_characters +=
      RemainingCharacters(final_players[static_cast<std::size_t>(winner)]);
  stats.losing_characters +=
      RemainingCharacters(final_players[static_cast<std::size_t>(1 - winner)]);

  for (int player = 0; player < kSideCount; ++player) {
    std::array<bool, kCharacterCount> present{};
    const auto& final_player =
        final_players[static_cast<std::size_t>(player)];
    for (const auto& character : final_player.positions) {
      if (character.character_id >= 0 && character.life > 0) {
        present[static_cast<std::size_t>(character.character_id)] = true;
      }
    }
    for (const int character : final_player.reserve) {
      present[static_cast<std::size_t>(character)] = true;
    }
    const auto& roster = match.rosters[static_cast<std::size_t>(player)];
    for (const int character : roster) {
      auto& bucket =
          stats.characters[static_cast<std::size_t>(character)];
      ++bucket.games;
      bucket.wins += winner == player ? 1 : 0;
      bucket.total_turns += match.state.TurnNumber();
      bucket.entered_games +=
          match.entered[static_cast<std::size_t>(player)]
                       [static_cast<std::size_t>(character)]
              ? 1
              : 0;
      const bool started_in_play =
          match.initial_position[static_cast<std::size_t>(player)]
                                [static_cast<std::size_t>(character)];
      bucket.initial_position_games += started_in_play ? 1 : 0;
      bucket.initial_position_wins +=
          started_in_play && winner == player ? 1 : 0;
      bucket.reserve_games += started_in_play ? 0 : 1;
      bucket.reserve_wins +=
          !started_in_play && winner == player ? 1 : 0;
      bucket.reserve_entered_games +=
          !started_in_play &&
                  match.entered[static_cast<std::size_t>(player)]
                               [static_cast<std::size_t>(character)]
              ? 1
              : 0;
      bucket.reserve_entered_wins +=
          !started_in_play &&
                  match.entered[static_cast<std::size_t>(player)]
                               [static_cast<std::size_t>(character)] &&
                  winner == player
              ? 1
              : 0;
      bucket.eliminated +=
          present[static_cast<std::size_t>(character)] ? 0 : 1;
      if (match.starting_active[static_cast<std::size_t>(player)] ==
          character) {
        ++bucket.starting_active_games;
        bucket.starting_active_wins += winner == player ? 1 : 0;
      }
    }
    for (std::size_t first = 0; first < roster.size(); ++first) {
      for (std::size_t second = first + 1; second < roster.size(); ++second) {
        const int low = std::min(roster[first], roster[second]);
        const int high = std::max(roster[first], roster[second]);
        auto& pair = stats.pairs[CharacterPairIndex(low, high)];
        ++pair.games;
        pair.wins += winner == player ? 1 : 0;
      }
    }
  }

  for (const int law : match.rosters[0]) {
    for (const int outlaw : match.rosters[1]) {
      auto& matchup =
          stats.matchups[CharacterPairIndex(law, outlaw)];
      ++matchup.games;
      matchup.wins += winner == static_cast<int>(Side::kLaw) ? 1 : 0;
    }
  }
  if (match.starting_active[0] >= 0 &&
      match.starting_active[1] >= 0) {
    auto& matchup = stats.active_matchups[CharacterPairIndex(
        match.starting_active[0], match.starting_active[1])];
    ++matchup.games;
    matchup.wins += winner == static_cast<int>(Side::kLaw) ? 1 : 0;
  }
}

SelfPlayStats EvaluateSelfPlay(
    BangActorCritic& network,
    int games,
    int seed,
    int max_game_steps) {
  SelfPlayStats stats;
  stats.requested_games = games;
  constexpr int kEvaluationBatchGames = 256;
  for (int first_game = 0; first_game < games;
       first_game += kEvaluationBatchGames) {
    const int batch_games =
        std::min(kEvaluationBatchGames, games - first_game);
    std::vector<SelfPlayMatch> matches;
    matches.reserve(static_cast<std::size_t>(batch_games));
    for (int game = 0; game < batch_games; ++game) {
      matches.emplace_back(
          static_cast<std::uint64_t>(seed + first_game + game));
    }
    int unfinished = batch_games;
    while (unfinished > 0) {
      std::vector<Episode> episodes;
      std::vector<PendingDecision> pending;
      std::vector<FastRng*> rngs;
      episodes.reserve(matches.size());
      pending.reserve(matches.size());
      rngs.reserve(matches.size());
      for (std::size_t index = 0; index < matches.size(); ++index) {
        auto& match = matches[index];
        if (match.finished) continue;
        match.Observe();
        if (match.state.IsTerminal() ||
            match.steps >= max_game_steps) {
          match.finished = true;
          --unfinished;
          continue;
        }
        auto legal = match.state.LegalActions();
        if (legal.empty()) {
          throw std::runtime_error(
              "non-terminal self-evaluation game has no legal action");
        }
        if (legal.size() == 1) {
          match.state.ApplyAction(legal.front());
          ++match.steps;
          continue;
        }
        episodes.emplace_back(
            match.state, 0, 0, match.rng.NextU64());
        pending.push_back({
            .episode = episodes.size() - 1,
            .transition = index,
            .actions = std::move(legal),
        });
        rngs.push_back(&episodes.back().policy_rng);
      }
      if (pending.empty()) continue;
      const auto actions =
          SelectPolicyActions(network, episodes, pending, rngs, true);
      for (std::size_t decision = 0; decision < actions.size(); ++decision) {
        auto& match =
            matches[pending[decision].transition];
        match.state.ApplyAction(actions[decision]);
        ++match.steps;
      }
    }
    for (auto& match : matches) {
      RecordSelfPlayMatch(match, stats);
    }
  }
  return stats;
}

JsonValue ConfidenceJson(int wins, int games) {
  const auto [low, high] = WilsonInterval(wins, games);
  return JsonValue::Array{low, high};
}

JsonValue SelfPlayStatsJson(
    const SelfPlayStats& stats,
    int seed) {
  JsonValue::Array characters;
  characters.reserve(kBaseCharacterCount);
  // Self-play evaluates base games, so only the base roster can be dealt.
  for (int character = 0; character < kBaseCharacterCount; ++character) {
    const auto& bucket =
        stats.characters[static_cast<std::size_t>(character)];
    const auto& data = GetCharacter(character);
    const int side = static_cast<int>(data.side);
    const double side_rate = Rate(
        stats.wins[static_cast<std::size_t>(side)],
        stats.terminal_games);
    const double win_rate = Rate(bucket.wins, bucket.games);
    characters.emplace_back(JsonValue::Object{
        {"id", character},
        {"key", std::string(data.key)},
        {"name", std::string(data.display_name)},
        {"side", std::string(SideName(data.side))},
        {"games", bucket.games},
        {"wins", bucket.wins},
        {"winRate", win_rate},
        {"confidence95", ConfidenceJson(bucket.wins, bucket.games)},
        {"sideWinRate", side_rate},
        {"uplift", win_rate - side_rate},
        {"startingActiveGames", bucket.starting_active_games},
        {"startingActiveWins", bucket.starting_active_wins},
        {"startingActiveWinRate", Rate(
             bucket.starting_active_wins,
             bucket.starting_active_games)},
        {"startingActiveUplift", bucket.starting_active_games == 0
             ? 0.0
             : Rate(
                   bucket.starting_active_wins,
                   bucket.starting_active_games) - win_rate},
        {"enteredPlayGames", bucket.entered_games},
        {"enteredPlayRate", Rate(bucket.entered_games, bucket.games)},
        {"initialPositionGames", bucket.initial_position_games},
        {"initialPositionWins", bucket.initial_position_wins},
        {"initialPositionWinRate", Rate(
             bucket.initial_position_wins,
             bucket.initial_position_games)},
        {"reserveGames", bucket.reserve_games},
        {"reserveWins", bucket.reserve_wins},
        {"reserveWinRate", Rate(
             bucket.reserve_wins, bucket.reserve_games)},
        {"reserveEnteredGames", bucket.reserve_entered_games},
        {"reserveEnteredWins", bucket.reserve_entered_wins},
        {"reserveEntryRate", Rate(
             bucket.reserve_entered_games, bucket.reserve_games)},
        {"reserveEnteredWinRate", Rate(
             bucket.reserve_entered_wins,
             bucket.reserve_entered_games)},
        {"eliminated", bucket.eliminated},
        {"eliminationRate", Rate(bucket.eliminated, bucket.games)},
        {"meanGameTurns", Rate(bucket.total_turns, bucket.games)},
    });
  }

  JsonValue::Array pairs;
  for (int first = 0; first < kCharacterCount; ++first) {
    for (int second = first + 1; second < kCharacterCount; ++second) {
      const auto& first_data = GetCharacter(first);
      const auto& second_data = GetCharacter(second);
      if (first_data.side != second_data.side) continue;
      const auto& bucket =
          stats.pairs[CharacterPairIndex(first, second)];
      if (bucket.games == 0) continue;
      const int side = static_cast<int>(first_data.side);
      const double side_rate = Rate(
          stats.wins[static_cast<std::size_t>(side)],
          stats.terminal_games);
      pairs.emplace_back(JsonValue::Object{
          {"first", std::string(first_data.key)},
          {"firstName", std::string(first_data.display_name)},
          {"second", std::string(second_data.key)},
          {"secondName", std::string(second_data.display_name)},
          {"side", std::string(SideName(first_data.side))},
          {"games", bucket.games},
          {"wins", bucket.wins},
          {"winRate", Rate(bucket.wins, bucket.games)},
          {"confidence95", ConfidenceJson(bucket.wins, bucket.games)},
          {"uplift", Rate(bucket.wins, bucket.games) - side_rate},
      });
    }
  }

  JsonValue::Array matchups;
  for (const int law : CharacterIds(Side::kLaw)) {
    for (const int outlaw : CharacterIds(Side::kOutlaw)) {
      const auto& bucket =
          stats.matchups[CharacterPairIndex(law, outlaw)];
      if (bucket.games == 0) continue;
      matchups.emplace_back(JsonValue::Object{
          {"law", std::string(GetCharacter(law).key)},
          {"lawName", std::string(GetCharacter(law).display_name)},
          {"outlaw", std::string(GetCharacter(outlaw).key)},
          {"outlawName", std::string(GetCharacter(outlaw).display_name)},
          {"games", bucket.games},
          {"lawWins", bucket.wins},
          {"lawWinRate", Rate(bucket.wins, bucket.games)},
          {"confidence95", ConfidenceJson(bucket.wins, bucket.games)},
      });
    }
  }

  JsonValue::Array active_matchups;
  for (const int law : CharacterIds(Side::kLaw)) {
    for (const int outlaw : CharacterIds(Side::kOutlaw)) {
      const auto& bucket =
          stats.active_matchups[CharacterPairIndex(law, outlaw)];
      if (bucket.games == 0) continue;
      active_matchups.emplace_back(JsonValue::Object{
          {"law", std::string(GetCharacter(law).key)},
          {"lawName", std::string(GetCharacter(law).display_name)},
          {"outlaw", std::string(GetCharacter(outlaw).key)},
          {"outlawName", std::string(GetCharacter(outlaw).display_name)},
          {"games", bucket.games},
          {"lawWins", bucket.wins},
          {"lawWinRate", Rate(bucket.wins, bucket.games)},
      });
    }
  }

  return JsonValue::Object{
      {"schemaVersion", 1},
      {"method", "greedy_checkpoint_self_play_v1"},
      {"games", stats.requested_games},
      {"terminalGames", stats.terminal_games},
      {"truncatedGames", stats.truncated},
      {"seed", seed},
      {"charactersPerSide", 4},
      {"lawWins", stats.wins[0]},
      {"lawWinRate", Rate(stats.wins[0], stats.terminal_games)},
      {"outlawWins", stats.wins[1]},
      {"outlawWinRate", Rate(stats.wins[1], stats.terminal_games)},
      {"steps", static_cast<double>(stats.total_steps)},
      {"meanSteps", Mean(stats.game_steps)},
      {"medianSteps", Percentile(stats.game_steps, 0.5)},
      {"p90Steps", Percentile(stats.game_steps, 0.9)},
      {"meanTurns", Mean(stats.game_turns)},
      {"medianTurns", Percentile(stats.game_turns, 0.5)},
      {"p90Turns", Percentile(stats.game_turns, 0.9)},
      {"endState", JsonValue::Object{
           {"meanWinningLife", Rate(
                stats.winning_life, stats.terminal_games)},
           {"meanLosingLife", Rate(
                stats.losing_life, stats.terminal_games)},
           {"meanWinningCharacters", Rate(
                stats.winning_characters, stats.terminal_games)},
           {"meanLosingCharacters", Rate(
                stats.losing_characters, stats.terminal_games)},
       }},
      {"characters", std::move(characters)},
      {"characterPairs", std::move(pairs)},
      {"matchups", std::move(matchups)},
      {"startingActiveMatchups", std::move(active_matchups)},
  };
}

}  // namespace

double EvaluationResult::WinRate() const {
  return games == 0 ? 0.0
                    : static_cast<double>(candidate_wins) / games;
}

JsonValue EvaluationResult::ToJson() const {
  return JsonValue::Object{
      {"pairs", pairs},
      {"games", games},
      {"candidateWins", candidate_wins},
      {"candidateWinRate", WinRate()},
      {"candidateLawWins", candidate_law_wins},
      {"candidateLawWinRate",
       pairs == 0 ? 0.0
                  : static_cast<double>(candidate_law_wins) / pairs},
      {"candidateOutlawWins", candidate_outlaw_wins},
      {"candidateOutlawWinRate",
       pairs == 0 ? 0.0
                  : static_cast<double>(candidate_outlaw_wins) / pairs},
      {"steps", total_steps},
      {"truncated", truncated},
  };
}

torch::Device ResolveTrainingDevice(const std::string& name) {
  const torch::Device device(name);
  if (!device.is_cpu() && !device.is_cuda()) {
    throw std::invalid_argument("--device must be cpu, cuda, or cuda:N");
  }
  if (device.is_cuda() && !torch::cuda::is_available()) {
    throw std::runtime_error(
        "--device " + name + " requested, but this LibTorch build sees no "
        "CUDA device");
  }
  return device;
}

// Parameters keep their identity (the optimizer holds them), so values are
// copied in place rather than the modules being reassigned.
void CopyModuleState(BangActorCritic& source, BangActorCritic& destination) {
  torch::NoGradGuard no_grad;
  const auto source_parameters = source->named_parameters(true);
  for (auto& item : destination->named_parameters(true)) {
    item.value().copy_(source_parameters[item.key()]);
  }
  const auto source_buffers = source->named_buffers(true);
  for (auto& item : destination->named_buffers(true)) {
    item.value().copy_(source_buffers[item.key()]);
  }
}

int RunTraining(const TrainingConfig& config) {
  if (config.iterations <= 0 || config.batch_games <= 0 ||
      config.update_epochs <= 0 || config.minibatches <= 0 ||
      !std::isfinite(config.learning_rate) ||
      config.learning_rate <= 0.0 ||
      !std::isfinite(config.entropy_coef) ||
      config.entropy_coef < 0.0 ||
      !std::isfinite(config.target_kl) ||
      config.target_kl < 0.0 ||
      config.characters_per_side != 4 ||
      config.max_game_steps <= 0) {
    throw std::invalid_argument("invalid BANG training configuration");
  }
  for (const auto& opponent : config.opponents) {
    if (!std::isfinite(opponent.probability) ||
        opponent.probability < 0.0) {
      throw std::invalid_argument(
          "opponent probabilities must be finite and non-negative");
    }
    if (opponent.kind == TrainingOpponentKind::kCheckpoint &&
        opponent.checkpoint.empty()) {
      throw std::invalid_argument(
          "checkpoint opponent requires a path");
    }
  }
  torch::set_num_threads(std::max(1, config.torch_threads));
  torch::manual_seed(config.seed);
  stop_requested.store(false);
  std::signal(SIGINT, StopHandler);
  std::signal(SIGTERM, StopHandler);
  std::filesystem::create_directories(config.checkpoint_dir);
  if (config.mmd_alpha < 0.0 || config.magnet_interval < 0) {
    throw std::invalid_argument(
        "--mmd-alpha and --magnet-interval must be non-negative");
  }
  const torch::Device device = ResolveTrainingDevice(config.device);
  // `network` stays on the CPU for batched rollout inference and checkpoints.
  // On an accelerator, `learner` is a device-resident twin that owns the
  // optimizer and is copied back after every update; on the CPU they are one
  // module.
  BangActorCritic network(ArchitectureForModelType(config.model_type));
  network->eval();
  BangActorCritic learner = network;
  if (!device.is_cpu()) {
    learner = BangActorCritic(ArchitectureForModelType(config.model_type));
    learner->to(device);
    learner->eval();
    CopyModuleState(network, learner);
  }
  torch::optim::Adam optimizer(
      learner->parameters(),
      torch::optim::AdamOptions(config.learning_rate));
  // MMD magnet: null (uniform play) until its first move, then a frozen
  // snapshot of the learner on the learner's device.
  const bool mmd = config.mmd_alpha > 0.0;
  BangActorCritic magnet{nullptr};
  int magnet_iteration = 0;
  int first_iteration = 1;
  std::int64_t environment_steps = 0;
  std::int64_t learner_decisions = 0;
  double elapsed_before = 0.0;
  if (!config.resume_from.empty()) {
    const auto metadata = LoadCheckpointMetadata(config.resume_from);
    magnet_iteration =
        ValidateTrainingCommit(config.resume_from, metadata.iteration);
    if (metadata.model_type != config.model_type) {
      throw std::runtime_error(
          "resume checkpoint holds a " + metadata.model_type +
          " policy but training was configured for " + config.model_type);
    }
    if (metadata.layout != kLayoutDims) {
      throw std::runtime_error(
          "resume checkpoint was trained on an older input layout; it can be "
          "evaluated and played, but training must start a new run");
    }
    LoadPolicyCheckpoint(config.resume_from, network);
    if (!device.is_cpu()) CopyModuleState(network, learner);
    if (!LoadOptimizerCheckpointIfPresent(
            config.resume_from.string() + ".optimizer.pt",
            optimizer,
            device)) {
      throw std::runtime_error(
          "resume checkpoint is missing optimizer state");
    }
    if (mmd && magnet_iteration > 0) {
      magnet = BangActorCritic(ArchitectureForModelType(config.model_type));
      magnet->to(device);
      magnet->eval();
      torch::serialize::InputArchive archive;
      archive.load_from(MagnetPath(config.resume_from).string(), device);
      magnet->load(archive);
    } else if (!mmd) {
      magnet_iteration = 0;
    }
    first_iteration = metadata.iteration + 1;
    environment_steps = metadata.environment_steps;
    learner_decisions = metadata.learner_decisions;
    elapsed_before = metadata.elapsed_seconds;
  }
  if (first_iteration > config.iterations) {
    std::cout << "training already reached iteration "
              << first_iteration - 1 << '\n';
    return 0;
  }
#ifdef _WIN32
  const bool color = ::_isatty(::_fileno(stdout)) != 0;
#else
  const bool color = ::isatty(STDOUT_FILENO) != 0;
#endif
  const char* cyan = color ? "\033[1;36m" : "";
  const char* green = color ? "\033[1;32m" : "";
  const char* yellow = color ? "\033[1;33m" : "";
  const char* reset = color ? "\033[0m" : "";
  std::cout << cyan << "BANG! League PPO" << reset
            << "  ·  " << config.model_type << " "
            << CountParameters(*network)
            << " params  ·  " << config.batch_games
            << " games/update  ·  " << config.torch_threads
            << " CPU threads  ·  learner " << device.str()
            << '\n' << std::flush;
  std::deque<double> recent_win_rates =
      ReadRecentWinRates(config.metrics_path, 20);
  std::vector<RolloutResult::OpponentStats> chunk_opponents;
  const auto training_start = std::chrono::steady_clock::now();
  for (int iteration = first_iteration;
       iteration <= config.iterations;
       ++iteration) {
    const auto iteration_start = std::chrono::steady_clock::now();
    const auto rollout_start = iteration_start;
    auto rollout = CollectRollouts(network, config, iteration);
    const auto rollout_end = std::chrono::steady_clock::now();
    auto update = UpdateNetwork(
        learner, magnet ? &magnet : nullptr, optimizer,
        rollout.transitions, config, iteration);
    if (!device.is_cpu()) CopyModuleState(learner, network);
    bool magnet_moved = false;
    if (mmd && config.magnet_interval > 0 &&
        iteration % config.magnet_interval == 0) {
      if (!magnet) {
        magnet = BangActorCritic(ArchitectureForModelType(config.model_type));
        magnet->to(device);
        magnet->eval();
      }
      CopyModuleState(learner, magnet);
      magnet_iteration = iteration;
      magnet_moved = true;
    }
    const auto iteration_end = std::chrono::steady_clock::now();
    const double rollout_seconds =
        std::chrono::duration<double>(rollout_end - rollout_start).count();
    const double update_seconds =
        std::chrono::duration<double>(iteration_end - rollout_end).count();
    const double iteration_seconds =
        std::chrono::duration<double>(iteration_end - iteration_start).count();
    environment_steps += rollout.environment_steps;
    learner_decisions +=
        static_cast<std::int64_t>(rollout.transitions.size());
    const double elapsed =
        elapsed_before +
        std::chrono::duration<double>(
            iteration_end - training_start).count();
    const auto metadata = Metadata(
        network,
        config,
        iteration,
        environment_steps,
        learner_decisions,
        elapsed);
    const auto latest = config.checkpoint_dir / "latest.pt";
    CommitTrainingCheckpoint(
        latest, network, optimizer, metadata,
        magnet_moved ? &magnet : nullptr, magnet_iteration);
    const double learner_win_rate =
        static_cast<double>(rollout.learner_wins) / config.batch_games;
    const double law_win_rate =
        static_cast<double>(rollout.law_wins) / config.batch_games;
    const double steps_per_second =
        rollout.environment_steps / std::max(rollout_seconds, 1e-9);
    const double end_to_end_steps_per_second =
        rollout.environment_steps / std::max(iteration_seconds, 1e-9);
    const double training_decisions_per_second =
        rollout.transitions.size() * update.epochs /
        std::max(update_seconds, 1e-9);
    recent_win_rates.push_back(learner_win_rate);
    while (recent_win_rates.size() > 20) recent_win_rates.pop_front();
    const std::size_t form_window =
        std::min<std::size_t>(10, recent_win_rates.size());
    const std::size_t form_begin = recent_win_rates.size() - form_window;
    const double form_win_rate =
        Mean(recent_win_rates, form_begin, recent_win_rates.size());
    const std::size_t previous_window =
        std::min<std::size_t>(10, form_begin);
    const double form_delta = previous_window == 0
        ? 0.0
        : form_win_rate - Mean(
              recent_win_rates,
              form_begin - previous_window,
              form_begin);
    MergeOpponentStats(chunk_opponents, rollout.opponents);
    const double critic_explained_variance =
        CriticExplainedVariance(rollout.transitions);
    const double normalized_entropy = rollout.style.decisions == 0
        ? 0.0
        : rollout.style.normalized_entropy /
            static_cast<double>(rollout.style.decisions);
    const double top_action_probability = rollout.style.decisions == 0
        ? 0.0
        : rollout.style.top_probability /
            static_cast<double>(rollout.style.decisions);
    const JsonValue metric = JsonValue::Object{
        {"schemaVersion", 1},
        {"iteration", iteration},
        {"games", config.batch_games},
        {"completedGames", config.batch_games - rollout.truncated},
        {"environmentSteps", static_cast<double>(rollout.environment_steps)},
        {"cumulativeEnvironmentSteps", static_cast<double>(environment_steps)},
        {"learnerDecisions", static_cast<double>(rollout.transitions.size())},
        {"learnerWinRate", learner_win_rate},
        {"formWindow", static_cast<double>(form_window)},
        {"formWinRate", form_win_rate},
        {"formDelta", form_delta},
        {"learnerLawGames", rollout.learner_law_games},
        {"learnerLawWinRate", Rate(
             rollout.learner_law_wins, rollout.learner_law_games)},
        {"learnerOutlawGames", rollout.learner_outlaw_games},
        {"learnerOutlawWinRate", Rate(
             rollout.learner_outlaw_wins, rollout.learner_outlaw_games)},
        {"lawWinRate", law_win_rate},
        {"outlawWinRate", 1.0 - law_win_rate},
        {"opponentPerformance", OpponentMetricsJson(rollout.opponents)},
        {"chunkOpponentPerformance", OpponentMetricsJson(chunk_opponents)},
        {"style", StyleMetricsJson(rollout.style)},
        {"policyLoss", update.policy_loss},
        {"valueLoss", update.value_loss},
        {"auxiliaryValueLoss", update.auxiliary_loss},
        {"criticExplainedVariance", critic_explained_variance},
        {"entropy", update.entropy},
        {"normalizedEntropy", normalized_entropy},
        {"topActionProbability", top_action_probability},
        {"gradientNorm", update.gradient_norm},
        {"approximateKl", update.kl},
        {"magnetKl", update.magnet_kl},
        {"magnetIteration", magnet_iteration},
        {"clipFraction", update.clip_fraction},
        {"updateEpochs", update.epochs},
        {"klEarlyStop", update.early_stopped},
        {"rolloutSeconds", rollout_seconds},
        {"updateSeconds", update_seconds},
        {"iterationSeconds", iteration_seconds},
        {"rolloutStepsPerSecond", steps_per_second},
        {"endToEndStepsPerSecond", end_to_end_steps_per_second},
        {"trainingDecisionsPerSecond", training_decisions_per_second},
        {"truncatedGames", rollout.truncated},
    };
    AppendMetric(config.metrics_path, metric);
    std::cout
        << green << "PPO " << std::setw(5) << iteration << reset
        << " │ games " << std::setw(4) << config.batch_games
        << " │ steps " << std::setw(7) << rollout.environment_steps
        << " │ learner " << std::setw(6) << Percent(learner_win_rate)
        << " │ form-" << form_window << ' ' << Percent(form_win_rate)
        << (previous_window == 0 ? "" : form_delta >= 0.0 ? " +" : " ")
        << (previous_window == 0
                ? ""
                : Percent(form_delta))
        << " │ seats L "
        << Percent(Rate(
             rollout.learner_law_wins, rollout.learner_law_games))
        << " · O "
        << Percent(Rate(
             rollout.learner_outlaw_wins, rollout.learner_outlaw_games))
        << " │ ent " << std::fixed << std::setprecision(3) << update.entropy
        << " │ KL " << std::setprecision(4) << update.kl
        << (update.early_stopped ? "*" : " ")
        << " │ clip " << Percent(update.clip_fraction)
        << " │ " << std::setprecision(0) << std::fixed
        << steps_per_second << " env/s · "
        << training_decisions_per_second << " train/s"
        << " │ " << std::setprecision(1)
        << rollout_seconds << "s roll + " << update_seconds << "s learn"
        << '\n' << std::flush;
    const bool detailed_report =
        iteration == first_iteration ||
        iteration % 5 == 0 ||
        iteration == config.iterations;
    if (detailed_report) {
      const auto grouped = GroupStrength(chunk_opponents);
      std::cout
          << "  STRENGTH │ chunk vs current "
          << OpportunityRate(grouped.current_wins, grouped.current_games)
          << " (" << grouped.current_games << "g)"
          << " · history "
          << OpportunityRate(grouped.history_wins, grouped.history_games)
          << " (" << grouped.history_games << "g)"
          << " · heuristic "
          << OpportunityRate(grouped.heuristic_wins, grouped.heuristic_games)
          << " (" << grouped.heuristic_games << "g)"
          << " · random "
          << OpportunityRate(grouped.random_wins, grouped.random_games)
          << " (" << grouped.random_games << "g)\n"
          << "  STYLE    │ attack "
          << OpportunityRate(
               rollout.style.attacks,
               rollout.style.attack_opportunities)
          << " of chances · defend "
          << OpportunityRate(
               rollout.style.defenses,
               rollout.style.defense_opportunities)
          << " · swap "
          << OpportunityRate(
               rollout.style.swaps,
               rollout.style.swap_opportunities)
          << " · rear target "
          << OpportunityRate(
               rollout.style.rear_targets,
               rollout.style.opponent_targets)
          << " · " << std::fixed << std::setprecision(2)
          << Rate(rollout.style.cards_played, rollout.style.turns)
          << " cards/turn · top " << TopCards(rollout.style) << '\n'
          << "  HEALTH   │ norm-ent " << Percent(normalized_entropy)
          << " · confidence " << Percent(top_action_probability)
          << " · critic EV " << std::setprecision(3)
          << critic_explained_variance
          << " · grad " << update.gradient_norm
          << " · KL " << update.kl << '/' << config.target_kl
          << " · clip " << Percent(update.clip_fraction);
      if (mmd) {
        std::cout << " · KL→magnet " << std::setprecision(4)
                  << update.magnet_kl << " ("
                  << (magnet_iteration == 0
                          ? std::string("uniform")
                          : "iter " + std::to_string(magnet_iteration))
                  << ')';
      }
      std::cout
          << '\n' << std::flush;
    }
    if (rollout.truncated > 0) {
      std::cout << yellow << "  warning: " << rollout.truncated
                << " games reached the decision safety limit" << reset
                << '\n' << std::flush;
    }
    if (normalized_entropy < 0.10 && rollout.style.decisions > 100) {
      std::cout << yellow
                << "  warning: normalized policy entropy is below 10%; "
                   "exploration may be collapsing"
                << reset << '\n' << std::flush;
    }
    const double learner_side_gap = std::abs(
        Rate(rollout.learner_law_wins, rollout.learner_law_games) -
        Rate(rollout.learner_outlaw_wins, rollout.learner_outlaw_games));
    if (learner_side_gap > 0.20 &&
        rollout.learner_law_games >= 50 &&
        rollout.learner_outlaw_games >= 50) {
      std::cout << yellow
                << "  warning: learner seat win rates differ by "
                << Percent(learner_side_gap)
                << reset << '\n' << std::flush;
    }
    if (stop_requested.load() ||
        (!config.stop_file.empty() && std::filesystem::exists(config.stop_file))) {
      std::cout << yellow
                << "stop requested; iteration bundle committed at "
                << latest << reset << '\n' << std::flush;
      return 75;
    }
  }
  return 0;
}

EvaluationResult RunPairedEvaluation(
    const std::filesystem::path& candidate_path,
    const std::filesystem::path& opponent_path,
    int pairs,
    int seed,
    int torch_threads,
    int max_game_steps) {
  if (pairs <= 0) {
    throw std::invalid_argument("evaluation pairs must be positive");
  }
  torch::set_num_threads(std::max(1, torch_threads));
  BangActorCritic candidate = LoadPolicyFromCheckpoint(candidate_path);
  candidate->eval();
  BangActorCritic opponent{nullptr};
  BangActorCritic* opponent_pointer = nullptr;
  std::optional<StaticPolicyNetwork> static_opponent;
  // A .json opponent is an exported browser policy (tools/export_static_policy.py),
  // so a committed policy/static-policy.json can serve as a fixed reference.
  if (opponent_path.extension() == ".json") {
    static_opponent = LoadStaticPolicyFile(opponent_path);
  } else if (!opponent_path.empty()) {
    opponent = LoadPolicyFromCheckpoint(opponent_path);
    opponent->eval();
    opponent_pointer = &opponent;
  }
  return Evaluate(
      candidate,
      opponent_pointer,
      static_opponent ? &*static_opponent : nullptr,
      pairs,
      seed,
      max_game_steps);
}

JsonValue RunSelfPlayEvaluation(
    const std::filesystem::path& checkpoint,
    int games,
    int seed,
    int torch_threads,
    int max_game_steps) {
  if (games <= 0) {
    throw std::invalid_argument(
        "self-evaluation games must be positive");
  }
  if (max_game_steps <= 0) {
    throw std::invalid_argument(
        "self-evaluation max game steps must be positive");
  }
  torch::set_num_threads(std::max(1, torch_threads));
  BangActorCritic network = LoadPolicyFromCheckpoint(checkpoint);
  network->eval();
  return SelfPlayStatsJson(
      EvaluateSelfPlay(network, games, seed, max_game_steps),
      seed);
}

JsonValue RunStaticPolicyParity(
    const std::filesystem::path& checkpoint,
    const std::filesystem::path& static_policy,
    int decisions,
    int seed) {
  if (decisions <= 0) {
    throw std::invalid_argument("parity decisions must be positive");
  }
  torch::set_num_threads(1);
  BangActorCritic network = LoadPolicyFromCheckpoint(checkpoint);
  network->eval();
  const StaticPolicyNetwork exported = LoadStaticPolicyFile(static_policy);
  torch::NoGradGuard no_grad;
  FastRng rng(static_cast<std::uint64_t>(seed) ^ 0x9e3779b97f4a7c15ULL);
  int game = 0;
  GameState state(GameConfig{
      .seed = static_cast<std::uint64_t>(seed), .characters_per_side = 4});
  double max_abs_difference = 0.0;
  int argmax_disagreements = 0;
  // Largest gap between the top two torch logits at a disagreement: a real
  // export bug disagrees on clear choices, float rounding only on near-ties.
  double widest_disagreement_gap = 0.0;
  int compared = 0;
  while (compared < decisions) {
    if (state.IsTerminal()) {
      state = GameState(GameConfig{
          .seed = static_cast<std::uint64_t>(seed + ++game),
          .characters_per_side = 4});
    }
    const auto legal = state.LegalActions();
    if (legal.size() > 1) {
      std::vector<ActionFeature> features;
      std::vector<float> flat;
      for (const int action : legal) {
        features.push_back(EncodeAction(action));
        flat.insert(flat.end(), features.back().values.begin(),
                    features.back().values.end());
      }
      auto observation = state.TrainingObservationTensor(state.CurrentPlayer());
      std::vector<std::int64_t> ids(legal.size(), 0);
      const auto torch_logits = network->Forward(
          torch::from_blob(observation.data(), {1, kTrainingObservationSize},
                           torch::kFloat32).clone(),
          torch::from_blob(flat.data(),
                           {static_cast<long>(legal.size()), kActionFeatureSize},
                           torch::kFloat32).clone(),
          torch::from_blob(ids.data(), {static_cast<long>(ids.size())},
                           torch::kInt64).clone()).logits.contiguous();
      const float* reference = torch_logits.data_ptr<float>();
      const auto candidate = StaticPolicyLogits(exported, observation, features);
      std::size_t torch_best = 0;
      std::size_t static_best = 0;
      for (std::size_t row = 0; row < legal.size(); ++row) {
        max_abs_difference = std::max(
            max_abs_difference,
            static_cast<double>(std::abs(reference[row] - candidate[row])));
        if (reference[row] > reference[torch_best]) torch_best = row;
        if (candidate[row] > candidate[static_best]) static_best = row;
      }
      if (torch_best != static_best) {
        ++argmax_disagreements;
        widest_disagreement_gap = std::max(
            widest_disagreement_gap,
            static_cast<double>(reference[torch_best] - reference[static_best]));
      }
      ++compared;
    }
    state.ApplyAction(legal[rng.USize(legal.size())]);
  }
  return JsonValue::Object{
      {"decisions", compared},
      {"maxAbsLogitDifference", max_abs_difference},
      {"argmaxDisagreements", argmax_disagreements},
      {"widestDisagreementGap", widest_disagreement_gap},
  };
}

JsonValue RunCheckpointCanary(
    const std::filesystem::path& checkpoint) {
  torch::set_num_threads(1);
  BangActorCritic network = LoadPolicyFromCheckpoint(checkpoint);
  network->eval();
  GameState state(GameConfig{.seed = 77, .characters_per_side = 4});
  const auto legal = state.LegalActions();
  std::vector<float> features;
  for (const int action : legal) {
    const auto encoded = EncodeAction(action);
    features.insert(
        features.end(), encoded.values.begin(), encoded.values.end());
  }
  const auto observation = state.TrainingObservationTensor(
      state.CurrentPlayer());
  std::vector<std::int64_t> decisions(legal.size(), 0);
  torch::NoGradGuard no_grad;
  const auto output = network->Forward(
      torch::from_blob(
          const_cast<float*>(observation.data()),
          {1, kTrainingObservationSize},
          torch::kFloat32).clone(),
      torch::from_blob(
          features.data(),
          {static_cast<long>(legal.size()), kActionFeatureSize},
          torch::kFloat32).clone(),
      torch::from_blob(
          decisions.data(),
          {static_cast<long>(decisions.size())},
          torch::kInt64).clone());
  const bool finite =
      torch::isfinite(output.logits).all().item<bool>() &&
      torch::isfinite(output.value).all().item<bool>();
  const auto metadata = LoadCheckpointMetadata(checkpoint);
  return JsonValue::Object{
      {"ok", finite},
      {"checkpoint", checkpoint.string()},
      {"modelType", metadata.model_type},
      {"iteration", metadata.iteration},
      {"parameters", static_cast<double>(metadata.parameter_count)},
      {"legalActions", static_cast<int>(legal.size())},
      {"logits", static_cast<double>(output.logits.numel())},
      {"value", output.value.item<double>()},
  };
}

JsonValue RunCheckpointInference(
    const std::filesystem::path& checkpoint,
    int games,
    int seed,
    int torch_threads) {
  const auto result = RunPairedEvaluation(
      checkpoint, {}, std::max(1, (games + 1) / 2), seed,
      torch_threads);
  const auto json = result.ToJson();
  JsonValue::Object object =
      *std::get_if<JsonValue::Object>(&json.storage());
  object["ok"] = true;
  object["opponent"] = "heuristic";
  return object;
}

}  // namespace bang

#endif
