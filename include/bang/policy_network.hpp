#pragma once

#ifdef BANG_ENABLE_TORCH

#include <filesystem>

#include <torch/torch.h>

#include "bang/checkpoint.hpp"
#include "bang/game.hpp"
#include "bang/policy_architecture.hpp"

namespace bang {

struct PolicyOutput {
  torch::Tensor logits;
  torch::Tensor value;
  torch::Tensor auxiliary_value;
};

class BangActorCriticImpl : public torch::nn::Module {
 public:
  BangActorCriticImpl() : BangActorCriticImpl(PolicyArchitectureV2()) {}
  explicit BangActorCriticImpl(const PolicyArchitecture& architecture);

  const PolicyArchitecture& Architecture() const { return architecture_; }

  PolicyOutput Forward(
      const torch::Tensor& observations,
      const torch::Tensor& legal_action_features,
      const torch::Tensor& decision_ids,
      const torch::Tensor& privileged = {});
  torch::Tensor EncodeState(const torch::Tensor& observations);

 private:
  PolicyArchitecture architecture_{};
  torch::nn::Sequential public_encoder_{nullptr};
  torch::nn::Sequential identity_encoder_{nullptr};
  torch::nn::Sequential state_fusion_{nullptr};
  torch::nn::ModuleList residuals_{nullptr};
  torch::nn::Sequential state_policy_{nullptr};
  torch::nn::Sequential action_encoder_{nullptr};
  torch::nn::Sequential actor_{nullptr};
  torch::nn::Sequential critic_{nullptr};
  torch::nn::Sequential privileged_encoder_{nullptr};
  torch::nn::Sequential auxiliary_critic_{nullptr};
};

TORCH_MODULE(BangActorCritic);

std::int64_t CountParameters(const torch::nn::Module& module);
void SavePolicyCheckpoint(
    const std::filesystem::path& path,
    BangActorCritic& network,
    const CheckpointMetadata& metadata);
void LoadPolicyCheckpoint(
    const std::filesystem::path& path,
    BangActorCritic& network);
// Builds the architecture named by the checkpoint's own metadata before
// loading it. Every load site that does not already own a matching module
// should use this rather than default-constructing one.
BangActorCritic LoadPolicyFromCheckpoint(const std::filesystem::path& path);
void SaveOptimizerCheckpoint(
    const std::filesystem::path& path,
    torch::optim::Optimizer& optimizer);
bool LoadOptimizerCheckpointIfPresent(
    const std::filesystem::path& path,
    torch::optim::Optimizer& optimizer,
    torch::Device device = torch::kCPU);
int SelectPolicyAction(
    BangActorCritic& network,
    const GameState& state,
    FastRng& rng,
    bool greedy);

}  // namespace bang

#endif
