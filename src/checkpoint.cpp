#include "bang/checkpoint.hpp"

#ifdef BANG_ENABLE_TORCH

#include <fstream>
#include <stdexcept>
#include <variant>

#include "bang/action_space.hpp"
#include "bang/game.hpp"
#include "bang/policy_architecture.hpp"

namespace bang {
namespace {

const JsonValue::Object& Object(const JsonValue& value, const char* label) {
  const auto* object = std::get_if<JsonValue::Object>(&value.storage());
  if (object == nullptr) {
    throw std::runtime_error(std::string(label) + " must be an object");
  }
  return *object;
}

const JsonValue& Field(
    const JsonValue::Object& object,
    const char* key) {
  const auto found = object.find(key);
  if (found == object.end()) {
    throw std::runtime_error(std::string("checkpoint metadata missing ") + key);
  }
  return found->second;
}

std::string StringField(
    const JsonValue::Object& object,
    const char* key) {
  const auto* value = std::get_if<std::string>(&Field(object, key).storage());
  if (value == nullptr) {
    throw std::runtime_error(std::string("checkpoint field is not text: ") + key);
  }
  return *value;
}

double NumberField(
    const JsonValue::Object& object,
    const char* key) {
  const auto* value = std::get_if<double>(&Field(object, key).storage());
  if (value == nullptr) {
    throw std::runtime_error(std::string("checkpoint field is not numeric: ") + key);
  }
  return *value;
}

}  // namespace

void WriteTextFile(
    const std::filesystem::path& path,
    const std::string& text) {
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }
  const auto temporary = path.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      throw std::runtime_error("failed to write " + temporary);
    }
    output << text;
    output.flush();
    if (!output) {
      throw std::runtime_error("failed to flush " + temporary);
    }
  }
  std::filesystem::rename(temporary, path);
}

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to read " + path.string());
  }
  return std::string(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

JsonValue CheckpointMetadataJson(const CheckpointMetadata& metadata) {
  return JsonValue::Object{
      {"schemaVersion", 1},
      {"modelType", metadata.model_type},
      {"ruleset", metadata.ruleset},
      {"observationAbi", metadata.observation_abi},
      {"actionFeatureAbi", metadata.action_feature_abi},
      {"layout", LayoutDimsJson(metadata.layout)},
      {"observationSize", metadata.observation_size},
      {"actionFeatureSize", metadata.action_feature_size},
      {"privilegedSize", metadata.privileged_size},
      {"iteration", metadata.iteration},
      {"environmentSteps", static_cast<double>(metadata.environment_steps)},
      {"learnerDecisions", static_cast<double>(metadata.learner_decisions)},
      {"parameterCount", static_cast<double>(metadata.parameter_count)},
      {"seed", metadata.seed},
      {"learningRate", metadata.learning_rate},
      {"elapsedSeconds", metadata.elapsed_seconds},
  };
}

CheckpointMetadata LoadCheckpointMetadata(
    const std::filesystem::path& checkpoint) {
  const auto parsed = ParseJson(
      ReadTextFile(checkpoint.string() + ".metadata.json"));
  if (!parsed.has_value()) {
    throw std::runtime_error("checkpoint metadata is invalid JSON");
  }
  const auto& object = Object(*parsed, "checkpoint metadata");
  CheckpointMetadata metadata{
      .model_type = StringField(object, "modelType"),
      .ruleset = StringField(object, "ruleset"),
      .observation_abi = StringField(object, "observationAbi"),
      .action_feature_abi = StringField(object, "actionFeatureAbi"),
      .observation_size =
          static_cast<int>(NumberField(object, "observationSize")),
      .action_feature_size =
          static_cast<int>(NumberField(object, "actionFeatureSize")),
      .privileged_size =
          static_cast<int>(NumberField(object, "privilegedSize")),
      .iteration = static_cast<int>(NumberField(object, "iteration")),
      .environment_steps = static_cast<std::int64_t>(
          NumberField(object, "environmentSteps")),
      .learner_decisions = static_cast<std::int64_t>(
          NumberField(object, "learnerDecisions")),
      .parameter_count = static_cast<std::int64_t>(
          NumberField(object, "parameterCount")),
      .seed = static_cast<int>(NumberField(object, "seed")),
      .learning_rate = NumberField(object, "learningRate"),
      .elapsed_seconds = NumberField(object, "elapsedSeconds"),
  };
  metadata.layout = metadata.observation_abi == kObservationAbi
                        ? LayoutDimsFromJson(Field(object, "layout"))
                        : kBaseLayoutDims;
  ValidateCheckpointCompatibility(metadata);
  return metadata;
}

// A checkpoint is compatible when its recorded layout fits within the
// current one: an older, smaller layout is widened on load.
void ValidateCheckpointCompatibility(const CheckpointMetadata& metadata) {
  const bool v2 = metadata.observation_abi == kObservationAbi &&
                  metadata.action_feature_abi == kActionFeatureAbi;
  const bool v1 = metadata.observation_abi == kObservationAbiV1 &&
                  metadata.action_feature_abi == kActionFeatureAbiV1 &&
                  metadata.layout == kBaseLayoutDims;
  if (!IsSupportedModelType(metadata.model_type) ||
      metadata.ruleset != "base_four_character" || !(v1 || v2) ||
      !LayoutFits(metadata.layout, kLayoutDims) ||
      metadata.observation_size != TrainingObservationSizeFor(metadata.layout) ||
      metadata.action_feature_size != ActionFeatureSizeFor(metadata.layout) ||
      metadata.privileged_size != PrivilegedSizeFor(metadata.layout)) {
    throw std::runtime_error(
        "checkpoint is incompatible with the current BANG training ABI");
  }
}

}  // namespace bang

#endif
