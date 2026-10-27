#include "bang/static_policy.hpp"

#include <cmath>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

#include "bang/json.hpp"

namespace bang {
namespace {

const JsonValue::Object& ObjectAt(
    const JsonValue& value, std::string_view context) {
  const auto* object = std::get_if<JsonValue::Object>(&value.storage());
  if (object == nullptr) {
    throw std::runtime_error(std::string(context) + " must be an object");
  }
  return *object;
}

const JsonValue::Array& ArrayAt(
    const JsonValue& value, std::string_view context) {
  const auto* array = std::get_if<JsonValue::Array>(&value.storage());
  if (array == nullptr) {
    throw std::runtime_error(std::string(context) + " must be an array");
  }
  return *array;
}

const JsonValue& Required(
    const JsonValue::Object& object,
    const char* key,
    std::string_view context) {
  const auto found = object.find(key);
  if (found == object.end()) {
    throw std::runtime_error(
        std::string(context) + " is missing " + key);
  }
  return found->second;
}

std::string StringField(
    const JsonValue::Object& object,
    const char* key,
    std::string_view context) {
  const auto* value =
      std::get_if<std::string>(&Required(object, key, context).storage());
  if (value == nullptr) {
    throw std::runtime_error(
        std::string(context) + "." + key + " must be text");
  }
  return *value;
}

int IntField(
    const JsonValue::Object& object,
    const char* key,
    std::string_view context) {
  const auto* value =
      std::get_if<double>(&Required(object, key, context).storage());
  if (value == nullptr) {
    throw std::runtime_error(
        std::string(context) + "." + key + " must be numeric");
  }
  return static_cast<int>(*value);
}

struct Tensor {
  std::vector<int> shape;
  std::vector<float> values;
};

Tensor TensorFromJson(const JsonValue& value, std::string_view context) {
  const auto& object = ObjectAt(value, context);
  Tensor tensor;
  for (const JsonValue& dimension :
       ArrayAt(Required(object, "shape", context), "tensor shape")) {
    const auto* number = std::get_if<double>(&dimension.storage());
    if (number == nullptr || *number <= 0) {
      throw std::runtime_error(
          std::string(context) + " has an invalid shape");
    }
    tensor.shape.push_back(static_cast<int>(*number));
  }
  std::size_t expected = 1;
  for (const int dimension : tensor.shape) {
    expected *= static_cast<std::size_t>(dimension);
  }
  const auto& values =
      ArrayAt(Required(object, "values", context), "tensor values");
  if (values.size() != expected) {
    throw std::runtime_error(
        std::string(context) + " value count does not match its shape");
  }
  tensor.values.reserve(values.size());
  for (const JsonValue& value_item : values) {
    const auto* number = std::get_if<double>(&value_item.storage());
    if (number == nullptr || !std::isfinite(*number)) {
      throw std::runtime_error(
          std::string(context) + " contains a non-finite value");
    }
    tensor.values.push_back(static_cast<float>(*number));
  }
  return tensor;
}

std::unordered_map<std::string, Tensor> TensorMap(
    const JsonValue& value) {
  std::unordered_map<std::string, Tensor> tensors;
  for (const JsonValue& item : ArrayAt(value, "parameters")) {
    const auto& object = ObjectAt(item, "parameters[]");
    const std::string name =
        StringField(object, "name", "parameters[]");
    if (!tensors.emplace(
            name,
            TensorFromJson(
                Required(object, "tensor", "parameters[]"), name))
             .second) {
      throw std::runtime_error("duplicate static policy tensor: " + name);
    }
  }
  return tensors;
}

Tensor TakeTensor(
    std::unordered_map<std::string, Tensor>& tensors,
    const std::string& name) {
  const auto found = tensors.find(name);
  if (found == tensors.end()) {
    throw std::runtime_error("static policy is missing tensor: " + name);
  }
  Tensor result = std::move(found->second);
  tensors.erase(found);
  return result;
}

StaticPolicyLinear TakeLinear(
    std::unordered_map<std::string, Tensor>& tensors,
    const std::string& prefix,
    int input_size,
    int output_size) {
  Tensor weight = TakeTensor(tensors, prefix + ".weight");
  Tensor bias = TakeTensor(tensors, prefix + ".bias");
  if (weight.shape != std::vector<int>{output_size, input_size} ||
      bias.shape != std::vector<int>{output_size}) {
    throw std::runtime_error(
        "static policy tensor shape mismatch for " + prefix);
  }
  return {
      .input_size = input_size,
      .output_size = output_size,
      .weights = std::move(weight.values),
      .bias = std::move(bias.values),
  };
}

// Moves a first layer trained on an older input layout onto the current one:
// each old column lands where the column map says, new inputs get zero weight.
// The column order is kept, so the layer computes the same sums as before.
void WidenColumns(StaticPolicyLinear& layer, const std::vector<int>& columns,
                  int input_size) {
  if (static_cast<int>(columns.size()) != layer.input_size) {
    throw std::logic_error("static policy column map does not match its layer");
  }
  std::vector<float> weights(
      static_cast<std::size_t>(layer.output_size) * static_cast<std::size_t>(input_size), 0.0F);
  for (int row = 0; row < layer.output_size; ++row) {
    for (int column = 0; column < layer.input_size; ++column) {
      weights[static_cast<std::size_t>(row) * static_cast<std::size_t>(input_size) +
              static_cast<std::size_t>(columns[static_cast<std::size_t>(column)])] =
          layer.weights[static_cast<std::size_t>(row * layer.input_size + column)];
    }
  }
  layer.weights = std::move(weights);
  layer.input_size = input_size;
}

std::vector<float> Linear(
    const StaticPolicyLinear& layer,
    std::span<const float> input) {
  if (input.size() != static_cast<std::size_t>(layer.input_size)) {
    throw std::runtime_error("static policy linear input size mismatch");
  }
  std::vector<float> output(
      static_cast<std::size_t>(layer.output_size));
  for (int row = 0; row < layer.output_size; ++row) {
    float value = layer.bias[static_cast<std::size_t>(row)];
    const std::size_t offset =
        static_cast<std::size_t>(row * layer.input_size);
    for (int column = 0; column < layer.input_size; ++column) {
      value += layer.weights[offset + static_cast<std::size_t>(column)] *
               input[static_cast<std::size_t>(column)];
    }
    output[static_cast<std::size_t>(row)] = value;
  }
  return output;
}

void TanhInPlace(std::vector<float>& values) {
  for (float& value : values) value = std::tanh(value);
}

std::vector<float> LinearTanh(
    const StaticPolicyLinear& layer,
    std::span<const float> input) {
  std::vector<float> result = Linear(layer, input);
  TanhInPlace(result);
  return result;
}

}  // namespace

StaticPolicyNetwork LoadStaticPolicyJson(const std::string& text) {
  const auto parsed = ParseJson(text);
  if (!parsed.has_value()) {
    throw std::runtime_error("static policy is invalid JSON");
  }
  const auto& root = ObjectAt(*parsed, "static policy");
  if (StringField(root, "format", "static policy") !=
      "bang_static_policy_v1") {
    throw std::runtime_error("unsupported static policy format");
  }
  const auto& metadata =
      ObjectAt(Required(root, "metadata", "static policy"), "metadata");
  const std::string model_type = StringField(metadata, "modelType", "metadata");
  // The input layout it was trained on: recorded since ABI v2, and the base
  // layout for everything exported before.
  const std::string observation_abi =
      StringField(metadata, "observationAbi", "metadata");
  const std::string action_feature_abi =
      StringField(metadata, "actionFeatureAbi", "metadata");
  LayoutDims layout = kBaseLayoutDims;
  if (observation_abi == kObservationAbi && action_feature_abi == kActionFeatureAbi) {
    layout = LayoutDimsFromJson(Required(metadata, "layout", "metadata"));
  } else if (observation_abi != kObservationAbiV1 ||
             action_feature_abi != kActionFeatureAbiV1) {
    throw std::runtime_error("static policy has an unknown input ABI");
  }
  if (!IsSupportedModelType(model_type) ||
      StringField(metadata, "ruleset", "metadata") !=
          "base_four_character" ||
      !LayoutFits(layout, kLayoutDims) ||
      IntField(metadata, "observationSize", "metadata") !=
          TrainingObservationSizeFor(layout) ||
      IntField(metadata, "actionFeatureSize", "metadata") !=
          ActionFeatureSizeFor(layout)) {
    throw std::runtime_error(
        "static policy is incompatible with the current training ABI");
  }

  auto tensors =
      TensorMap(Required(root, "parameters", "static policy"));
  StaticPolicyNetwork network;
  network.iteration = IntField(metadata, "iteration", "metadata");
  network.source_checkpoint =
      StringField(root, "sourceCheckpoint", "static policy");
  network.model_type = model_type;
  const PolicyArchitecture architecture = ArchitectureForModelType(model_type);
  const int public_size = PublicObservationSizeFor(layout);
  network.public_encoder = TakeLinear(
      tensors, "public_encoder.0", public_size, architecture.public_hidden);
  network.identity_encoder = TakeLinear(
      tensors, "identity_encoder.0",
      TrainingObservationSizeFor(layout) - public_size,
      architecture.identity_hidden);
  network.state_fusion = TakeLinear(
      tensors, "state_fusion.0",
      architecture.public_hidden + architecture.identity_hidden,
      architecture.state_hidden);
  network.residuals.clear();
  network.residuals.reserve(
      static_cast<std::size_t>(architecture.residual_blocks));
  for (int block = 0; block < architecture.residual_blocks; ++block) {
    network.residuals.push_back(TakeLinear(
        tensors, "residuals." + std::to_string(block),
        architecture.state_hidden, architecture.state_hidden));
  }
  network.state_policy = TakeLinear(
      tensors, "state_policy.0",
      architecture.state_hidden, architecture.action_hidden);
  network.action_encoder = TakeLinear(
      tensors, "action_encoder.0",
      ActionFeatureSizeFor(layout), architecture.action_hidden);
  network.actor =
      TakeLinear(tensors, "actor.0", architecture.action_hidden * 4, 1);
  if (!tensors.empty()) {
    throw std::runtime_error(
        "static policy contains unexpected actor tensors");
  }
  if (layout != kLayoutDims) {
    const PolicyInputColumnMaps columns = PolicyInputColumns(layout, kLayoutDims);
    WidenColumns(network.public_encoder, columns.public_encoder,
                 kTrainingObservationOwnHandOffset);
    WidenColumns(network.identity_encoder, columns.identity_encoder,
                 kTrainingObservationSize - kTrainingObservationOwnHandOffset);
    WidenColumns(network.action_encoder, columns.action_encoder, kActionFeatureSize);
  }
  return network;
}

StaticPolicyNetwork LoadStaticPolicyFile(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error(
        "failed to open static policy file: " + path.string());
  }
  return LoadStaticPolicyJson(std::string(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>()));
}

std::vector<float> StaticPolicyLogits(
    const StaticPolicyNetwork& network,
    const std::array<float, kTrainingObservationSize>& observation,
    const std::vector<ActionFeature>& legal_action_features) {
  const std::span<const float> public_observation(
      observation.data(), kTrainingObservationOwnHandOffset);
  const std::span<const float> identity_observation(
      observation.data() + kTrainingObservationOwnHandOffset,
      kTrainingObservationSize - kTrainingObservationOwnHandOffset);
  const std::vector<float> public_state =
      LinearTanh(network.public_encoder, public_observation);
  const std::vector<float> identity_state =
      LinearTanh(network.identity_encoder, identity_observation);

  std::vector<float> fused_input;
  fused_input.reserve(public_state.size() + identity_state.size());
  fused_input.insert(
      fused_input.end(), public_state.begin(), public_state.end());
  fused_input.insert(
      fused_input.end(), identity_state.begin(), identity_state.end());
  std::vector<float> state =
      LinearTanh(network.state_fusion, fused_input);
  for (const StaticPolicyLinear& residual : network.residuals) {
    const std::vector<float> update = Linear(residual, state);
    for (std::size_t index = 0; index < state.size(); ++index) {
      state[index] = std::tanh(state[index] + update[index]);
    }
  }
  const std::vector<float> state_policy =
      LinearTanh(network.state_policy, state);

  std::vector<float> logits;
  logits.reserve(legal_action_features.size());
  for (const ActionFeature& feature : legal_action_features) {
    const std::vector<float> action =
        LinearTanh(network.action_encoder, feature.values);
    // The actor scores [state, action, state * action, state - action]; each
    // block is action_hidden wide (64 in v2, 160 in v3).
    const std::size_t width = action.size();
    float score = network.actor.bias.front();
    for (std::size_t index = 0; index < width; ++index) {
      score += network.actor.weights[index] * state_policy[index];
      score += network.actor.weights[width + index] * action[index];
      score += network.actor.weights[2 * width + index] *
               state_policy[index] * action[index];
      score += network.actor.weights[3 * width + index] *
               (state_policy[index] - action[index]);
    }
    logits.push_back(score);
  }
  return logits;
}

}  // namespace bang
