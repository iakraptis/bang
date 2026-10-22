#include "bang/policy_network.hpp"

#ifdef BANG_ENABLE_TORCH

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace bang {
namespace {

torch::nn::Linear Linear(
    int input,
    int output,
    double gain = 1.0) {
  auto layer = torch::nn::Linear(input, output);
  torch::nn::init::orthogonal_(layer->weight, gain);
  torch::nn::init::constant_(layer->bias, 0.0);
  return layer;
}

std::filesystem::path TemporaryPath(
    const std::filesystem::path& destination) {
  static std::atomic<std::uint64_t> sequence{0};
  return destination.string() + ".tmp." +
      std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()) +
      "." + std::to_string(sequence.fetch_add(1));
}

}  // namespace

BangActorCriticImpl::BangActorCriticImpl(
    const PolicyArchitecture& architecture)
    : architecture_(architecture) {
  const int kPublicHidden = architecture.public_hidden;
  const int kIdentityHidden = architecture.identity_hidden;
  const int kStateHidden = architecture.state_hidden;
  const int kActionHidden = architecture.action_hidden;
  const int kPolicyHidden = architecture.policy_hidden;
  if (kPublicHidden <= 0 || kIdentityHidden <= 0 || kStateHidden <= 0 ||
      kActionHidden <= 0 || kPolicyHidden <= 0 ||
      architecture.residual_blocks < 0) {
    throw std::invalid_argument("invalid BANG policy architecture");
  }
  constexpr int kIdentitySize =
      kTrainingObservationSize - kTrainingObservationOwnHandOffset;
  public_encoder_ = register_module(
      "public_encoder",
      torch::nn::Sequential(
          Linear(kTrainingObservationOwnHandOffset, kPublicHidden),
          torch::nn::Tanh()));
  identity_encoder_ = register_module(
      "identity_encoder",
      torch::nn::Sequential(
          Linear(kIdentitySize, kIdentityHidden),
          torch::nn::Tanh()));
  state_fusion_ = register_module(
      "state_fusion",
      torch::nn::Sequential(
          Linear(kPublicHidden + kIdentityHidden, kStateHidden),
          torch::nn::Tanh()));
  residuals_ = register_module("residuals", torch::nn::ModuleList());
  for (int block = 0; block < architecture.residual_blocks; ++block) {
    residuals_->push_back(Linear(kStateHidden, kStateHidden, 0.5));
  }
  state_policy_ = register_module(
      "state_policy",
      torch::nn::Sequential(
          Linear(kStateHidden, kActionHidden),
          torch::nn::Tanh()));
  action_encoder_ = register_module(
      "action_encoder",
      torch::nn::Sequential(
          Linear(kActionFeatureSize, kActionHidden),
          torch::nn::Tanh()));
  actor_ = register_module(
      "actor",
      torch::nn::Sequential(
          // Both inputs have already passed through nonlinear encoders. A
          // linear score over their interaction terms is expressive while
          // avoiding a large hidden layer for every legal action.
          Linear(kActionHidden * 4, 1, 0.01)));
  critic_ = register_module(
      "critic",
      torch::nn::Sequential(
          Linear(kStateHidden, kPolicyHidden),
          torch::nn::Tanh(),
          Linear(kPolicyHidden, 1)));
  privileged_encoder_ = register_module(
      "privileged_encoder",
      torch::nn::Sequential(
          Linear(kPrivilegedObservationSize, kIdentityHidden),
          torch::nn::Tanh()));
  auxiliary_critic_ = register_module(
      "auxiliary_critic",
      torch::nn::Sequential(
          Linear(kStateHidden + kIdentityHidden, kPolicyHidden),
          torch::nn::Tanh(),
          Linear(kPolicyHidden, 1)));
}

torch::Tensor BangActorCriticImpl::EncodeState(
    const torch::Tensor& observations) {
  using torch::indexing::Slice;
  const auto public_state = public_encoder_->forward(
      observations.index(
          {Slice(), Slice(0, kTrainingObservationOwnHandOffset)}));
  const auto identities = identity_encoder_->forward(
      observations.index(
          {Slice(), Slice(
              kTrainingObservationOwnHandOffset,
              kTrainingObservationSize)}));
  auto state = state_fusion_->forward(
      torch::cat({public_state, identities}, -1));
  for (const auto& module : *residuals_) {
    state = torch::tanh(
        state + module->as<torch::nn::Linear>()->forward(state));
  }
  return state;
}

PolicyOutput BangActorCriticImpl::Forward(
    const torch::Tensor& observations,
    const torch::Tensor& legal_action_features,
    const torch::Tensor& decision_ids,
    const torch::Tensor& privileged) {
  const auto state = EncodeState(observations);
  const auto state_policy = state_policy_->forward(state);
  const auto repeated_state =
      state_policy.index_select(0, decision_ids.to(torch::kLong));
  const auto actions = action_encoder_->forward(legal_action_features);
  const auto actor_input = torch::cat(
      {repeated_state,
       actions,
       repeated_state * actions,
       repeated_state - actions},
      -1);
  torch::Tensor auxiliary;
  if (privileged.defined() && privileged.numel() > 0) {
    auxiliary = auxiliary_critic_->forward(
        torch::cat(
            {state, privileged_encoder_->forward(privileged)},
            -1)).squeeze(-1);
  }
  return {
      .logits = actor_->forward(actor_input).squeeze(-1),
      .value = critic_->forward(state).squeeze(-1),
      .auxiliary_value = auxiliary,
  };
}

std::int64_t CountParameters(const torch::nn::Module& module) {
  std::int64_t count = 0;
  for (const auto& parameter : module.parameters()) {
    count += parameter.numel();
  }
  return count;
}

void SavePolicyCheckpoint(
    const std::filesystem::path& path,
    BangActorCritic& network,
    const CheckpointMetadata& metadata) {
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }
  torch::serialize::OutputArchive archive;
  network->save(archive);
  const auto temporary = TemporaryPath(path);
  try {
    archive.save_to(temporary.string());
    std::filesystem::rename(temporary, path);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
  WriteTextFile(
      path.string() + ".metadata.json",
      CheckpointMetadataJson(metadata).Dump() + "\n");
}

namespace {

// The layers that read raw inputs, and which column map widens each.
const std::vector<int>* InputColumns(const std::string& name,
                                     const PolicyInputColumnMaps& columns) {
  if (name == "public_encoder.0.weight") return &columns.public_encoder;
  if (name == "identity_encoder.0.weight") return &columns.identity_encoder;
  if (name == "action_encoder.0.weight") return &columns.action_encoder;
  if (name == "privileged_encoder.0.weight") return &columns.privileged_encoder;
  return nullptr;
}

// Reads a checkpoint saved on an older, smaller input layout into a module
// laid out for the current one. Every tensor keeps its values; the layers that
// read raw inputs get each old column at its mapped position and zero weight
// for inputs that did not exist when the policy was trained.
void LoadWidened(torch::nn::Module& module,
                 torch::serialize::InputArchive& archive,
                 const std::string& prefix,
                 const PolicyInputColumnMaps& columns) {
  torch::NoGradGuard no_grad;
  auto parameters = module.named_parameters(/*recurse=*/false);
  for (auto& item : parameters) {
    torch::Tensor stored;
    archive.read(item.key(), stored);
    torch::Tensor target = item.value();
    const std::string name = prefix + item.key();
    if (stored.sizes() == target.sizes()) {
      target.copy_(stored);
      continue;
    }
    const std::vector<int>* map = InputColumns(name, columns);
    if (map == nullptr || stored.dim() != 2 || target.dim() != 2 ||
        stored.size(0) != target.size(0) ||
        stored.size(1) != static_cast<std::int64_t>(map->size())) {
      throw std::runtime_error(
          "checkpoint tensor " + name + " does not fit the current input layout");
    }
    target.zero_();
    target.index_copy_(
        1, torch::tensor(std::vector<std::int64_t>(map->begin(), map->end()), torch::kLong),
        stored.to(target.dtype()));
  }
  auto buffers = module.named_buffers(/*recurse=*/false);
  for (auto& item : buffers) {
    torch::Tensor stored;
    archive.read(item.key(), stored, /*is_buffer=*/true);
    item.value().copy_(stored);
  }
  for (auto& child : module.named_children()) {
    torch::serialize::InputArchive child_archive;
    archive.read(child.key(), child_archive);
    LoadWidened(*child.value(), child_archive, prefix + child.key() + ".", columns);
  }
}

}  // namespace

void LoadPolicyCheckpoint(
    const std::filesystem::path& path,
    BangActorCritic& network) {
  const auto metadata = LoadCheckpointMetadata(path);
  if (ArchitectureForModelType(metadata.model_type) !=
      network->Architecture()) {
    throw std::runtime_error(
        "checkpoint " + path.string() + " holds a " + metadata.model_type +
        " policy, which does not match the module it is loaded into");
  }
  torch::serialize::InputArchive archive;
  archive.load_from(path.string());
  if (metadata.layout == kLayoutDims) {
    network->load(archive);
  } else {
    LoadWidened(*network, archive, "", PolicyInputColumns(metadata.layout, kLayoutDims));
  }
}

BangActorCritic LoadPolicyFromCheckpoint(const std::filesystem::path& path) {
  const auto metadata = LoadCheckpointMetadata(path);
  BangActorCritic network(ArchitectureForModelType(metadata.model_type));
  LoadPolicyCheckpoint(path, network);
  return network;
}

void SaveOptimizerCheckpoint(
    const std::filesystem::path& path,
    torch::optim::Optimizer& optimizer) {
  torch::serialize::OutputArchive archive;
  optimizer.save(archive);
  const auto temporary = TemporaryPath(path);
  try {
    archive.save_to(temporary.string());
    std::filesystem::rename(temporary, path);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}

bool LoadOptimizerCheckpointIfPresent(
    const std::filesystem::path& path,
    torch::optim::Optimizer& optimizer,
    torch::Device device) {
  if (!std::filesystem::is_regular_file(path)) return false;
  torch::serialize::InputArchive archive;
  // Adam moments must sit beside the parameters, whichever device saved them.
  archive.load_from(path.string(), device);
  optimizer.load(archive);
  return true;
}

int SelectPolicyAction(
    BangActorCritic& network,
    const GameState& state,
    FastRng& rng,
    bool greedy) {
  const auto legal = state.LegalActions();
  if (legal.empty()) {
    throw std::runtime_error("policy cannot act without legal actions");
  }
  if (legal.size() == 1) return legal.front();
  const auto observation =
      state.TrainingObservationTensor(state.CurrentPlayer());
  std::vector<float> features;
  features.reserve(legal.size() * kActionFeatureSize);
  for (const int action : legal) {
    const auto feature = EncodeAction(action);
    features.insert(
        features.end(), feature.values.begin(), feature.values.end());
  }
  std::vector<std::int64_t> decision_ids(legal.size(), 0);
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
          decision_ids.data(),
          {static_cast<long>(decision_ids.size())},
          torch::kInt64).clone());
  const auto logits = output.logits.to(torch::kCPU).contiguous();
  const float* values = logits.data_ptr<float>();
  int selected = 0;
  if (greedy) {
    for (int row = 1; row < static_cast<int>(legal.size()); ++row) {
      if (values[row] > values[selected]) selected = row;
    }
  } else {
    float maximum = values[0];
    for (int row = 1; row < static_cast<int>(legal.size()); ++row) {
      maximum = std::max(maximum, values[row]);
    }
    std::vector<double> probabilities(legal.size());
    double total = 0.0;
    for (std::size_t row = 0; row < legal.size(); ++row) {
      probabilities[row] = std::exp(values[row] - maximum);
      total += probabilities[row];
    }
    double choice =
        static_cast<double>(rng.NextU64() >> 11U) /
        static_cast<double>(1ULL << 53U) * total;
    selected = static_cast<int>(legal.size()) - 1;
    for (std::size_t row = 0; row < legal.size(); ++row) {
      choice -= probabilities[row];
      if (choice <= 0.0) {
        selected = static_cast<int>(row);
        break;
      }
    }
  }
  return legal[static_cast<std::size_t>(selected)];
}

}  // namespace bang

#endif
