#pragma once

#ifdef BANG_ENABLE_TORCH

#include <cstdint>
#include <filesystem>
#include <string>

#include "bang/action_space.hpp"
#include "bang/json.hpp"

namespace bang {

struct CheckpointMetadata {
  std::string model_type = "bang_structured_v2";
  std::string ruleset = "base_four_character";
  std::string observation_abi = kObservationAbi;
  std::string action_feature_abi = kActionFeatureAbi;
  // The input layout the policy was trained on (ABI v1 metadata: the base one).
  LayoutDims layout = kLayoutDims;
  int observation_size = 0;
  int action_feature_size = 0;
  int privileged_size = 0;
  int iteration = 0;
  std::int64_t environment_steps = 0;
  std::int64_t learner_decisions = 0;
  std::int64_t parameter_count = 0;
  int seed = 0;
  double learning_rate = 0.0;
  double elapsed_seconds = 0.0;
};

JsonValue CheckpointMetadataJson(const CheckpointMetadata& metadata);
CheckpointMetadata LoadCheckpointMetadata(
    const std::filesystem::path& checkpoint);
void WriteTextFile(
    const std::filesystem::path& path,
    const std::string& text);
std::string ReadTextFile(const std::filesystem::path& path);
void ValidateCheckpointCompatibility(const CheckpointMetadata& metadata);

}  // namespace bang

#endif
