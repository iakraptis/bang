#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "bang/action_space.hpp"
#include "bang/game.hpp"
#include "bang/policy_architecture.hpp"

namespace bang {

struct StaticPolicyLinear {
  int input_size = 0;
  int output_size = 0;
  std::vector<float> weights;
  std::vector<float> bias;
};

struct StaticPolicyNetwork {
  int iteration = 0;
  std::string source_checkpoint;
  std::string model_type{kModelTypeV2};
  StaticPolicyLinear public_encoder;
  StaticPolicyLinear identity_encoder;
  StaticPolicyLinear state_fusion;
  std::vector<StaticPolicyLinear> residuals;
  StaticPolicyLinear state_policy;
  StaticPolicyLinear action_encoder;
  StaticPolicyLinear actor;
};

StaticPolicyNetwork LoadStaticPolicyJson(const std::string& text);
StaticPolicyNetwork LoadStaticPolicyFile(const std::filesystem::path& path);

std::vector<float> StaticPolicyLogits(
    const StaticPolicyNetwork& network,
    const std::array<float, kTrainingObservationSize>& observation,
    const std::vector<ActionFeature>& legal_action_features);

}  // namespace bang
