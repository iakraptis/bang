#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace bang {

// Layer widths for the structured actor-critic. Every preset is named by the
// checkpoint metadata `modelType`, so a checkpoint always carries enough
// information to rebuild the module that can load it. This header is
// deliberately free of LibTorch so the browser static-policy path can share
// the same definitions.
struct PolicyArchitecture {
  int public_hidden = 128;
  int identity_hidden = 64;
  int state_hidden = 128;
  int action_hidden = 64;
  int policy_hidden = 128;
  int residual_blocks = 2;

  friend constexpr bool operator==(
      const PolicyArchitecture&, const PolicyArchitecture&) = default;
};

inline constexpr std::string_view kModelTypeV2 = "bang_structured_v2";
inline constexpr std::string_view kModelTypeV3 = "bang_structured_v3";

// 222,722 deployable parameters; the architecture every checkpoint written
// before the v3 experiment uses.
constexpr PolicyArchitecture PolicyArchitectureV2() { return {}; }

// About ten times v2: 2,340,770 deployable parameters. The extra capacity is
// concentrated in the trunk, which runs once per decision, rather than in the
// action encoder and actor, which run once per legal action and therefore cost
// roughly eight times as much compute per parameter.
constexpr PolicyArchitecture PolicyArchitectureV3() {
  return {
      .public_hidden = 512,
      .identity_hidden = 256,
      .state_hidden = 512,
      .action_hidden = 160,
      .policy_hidden = 512,
      .residual_blocks = 4,
  };
}

inline bool IsSupportedModelType(std::string_view model_type) {
  return model_type == kModelTypeV2 || model_type == kModelTypeV3;
}

inline PolicyArchitecture ArchitectureForModelType(
    std::string_view model_type) {
  if (model_type == kModelTypeV2) return PolicyArchitectureV2();
  if (model_type == kModelTypeV3) return PolicyArchitectureV3();
  throw std::runtime_error(
      "unknown BANG policy model type: " + std::string(model_type));
}

// Accepts the short command-line spellings as well as the full metadata
// identifiers, so `--model v3` and `--model bang_structured_v3` agree.
inline std::string ResolveModelType(std::string_view name) {
  if (name == "v2" || name == kModelTypeV2) return std::string(kModelTypeV2);
  if (name == "v3" || name == kModelTypeV3) return std::string(kModelTypeV3);
  throw std::invalid_argument("unknown model: " + std::string(name));
}

}  // namespace bang
