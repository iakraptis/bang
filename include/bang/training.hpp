#pragma once

#ifdef BANG_ENABLE_TORCH

#include <filesystem>
#include <string>
#include <vector>

#include "bang/json.hpp"
#include "bang/policy_architecture.hpp"

namespace bang {

enum class TrainingOpponentKind {
  kRandom,
  kHeuristic,
  kCheckpoint,
};

struct TrainingOpponentSpec {
  std::string id;
  TrainingOpponentKind kind = TrainingOpponentKind::kHeuristic;
  std::filesystem::path checkpoint;
  double probability = 1.0;
};

struct TrainingConfig {
  int iterations = 1;
  int batch_games = 256;
  int update_epochs = 4;
  int minibatches = 8;
  int seed = 470001;
  int characters_per_side = 4;
  int torch_threads = 6;
  int max_game_steps = 5000;
  std::string model_type{kModelTypeV2};
  double learning_rate = 3e-4;
  double clip_range = 0.2;
  double entropy_coef = 0.01;
  double value_loss_coef = 0.5;
  double auxiliary_value_loss_coef = 0.25;
  double target_kl = 0.015;
  float gae_lambda = 0.97F;
  std::filesystem::path checkpoint_dir = "runs/bang_league/main";
  std::filesystem::path metrics_path =
      "runs/bang_league/main/metrics.jsonl";
  std::filesystem::path resume_from;
  // Checked after each committed iteration. Windows cannot deliver SIGINT to
  // one child process, so the controller requests a stop by creating it.
  std::filesystem::path stop_file;
  // Where PPO updates run ("cpu", "cuda", "cuda:N"). Rollouts, opponents
  // and checkpoints always stay on the CPU.
  std::string device = "cpu";
  // Magnetic Mirror Descent: alpha * KL(policy || magnet) joins the PPO loss.
  // The magnet starts as uniform play over legal actions and, when
  // magnet_interval > 0, moves to the learner every that many iterations.
  // alpha = 0 is plain PPO.
  double mmd_alpha = 0.0;
  int magnet_interval = 0;
  std::vector<TrainingOpponentSpec> opponents;
  bool compact_logging = false;
};

struct EvaluationResult {
  int pairs = 0;
  int games = 0;
  int candidate_wins = 0;
  int candidate_law_wins = 0;
  int candidate_outlaw_wins = 0;
  int total_steps = 0;
  int truncated = 0;

  double WinRate() const;
  JsonValue ToJson() const;
};

int RunTraining(const TrainingConfig& config);
EvaluationResult RunPairedEvaluation(
    const std::filesystem::path& candidate,
    const std::filesystem::path& opponent,
    int pairs,
    int seed,
    int torch_threads = 6,
    int max_game_steps = 5000);
JsonValue RunSelfPlayEvaluation(
    const std::filesystem::path& checkpoint,
    int games,
    int seed,
    int torch_threads = 6,
    int max_game_steps = 5000);
JsonValue RunCheckpointCanary(const std::filesystem::path& checkpoint);
// Compares a checkpoint's legal-action logits with its static (browser) export
// across `decisions` positions from randomly played games.
JsonValue RunStaticPolicyParity(
    const std::filesystem::path& checkpoint,
    const std::filesystem::path& static_policy,
    int decisions,
    int seed);
JsonValue RunCheckpointInference(
    const std::filesystem::path& checkpoint,
    int games,
    int seed,
    int torch_threads = 6);

}  // namespace bang

#endif
