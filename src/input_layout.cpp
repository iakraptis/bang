#include "bang/input_layout.hpp"

#include <stdexcept>
#include <string>
#include <variant>

namespace bang {
namespace {

template <std::size_t N>
std::vector<int> BlockwiseMap(const std::array<int, N>& from,
                              const std::array<int, N>& to) {
  std::vector<int> map;
  map.reserve(static_cast<std::size_t>(BlockTotal(from)));
  int to_start = 0;
  for (std::size_t block = 0; block < N; ++block) {
    if (from[block] > to[block]) {
      throw std::invalid_argument("a layout can only be mapped onto a larger one");
    }
    for (int index = 0; index < from[block]; ++index) map.push_back(to_start + index);
    to_start += to[block];
  }
  return map;
}

}  // namespace

std::vector<int> LayoutIndexMap(LayoutKind kind, const LayoutDims& from, const LayoutDims& to) {
  switch (kind) {
    case LayoutKind::kObservation:
      return BlockwiseMap(ObservationBlockSizes(from), ObservationBlockSizes(to));
    case LayoutKind::kTraining: {
      // The base observation is one block here; map it block by block too.
      std::vector<int> map = BlockwiseMap(ObservationBlockSizes(from), ObservationBlockSizes(to));
      const auto from_sizes = TrainingBlockSizes(from);
      const auto to_sizes = TrainingBlockSizes(to);
      int to_start = to_sizes[0];
      for (std::size_t block = 1; block < from_sizes.size(); ++block) {
        if (from_sizes[block] > to_sizes[block]) {
          throw std::invalid_argument("a layout can only be mapped onto a larger one");
        }
        for (int index = 0; index < from_sizes[block]; ++index) map.push_back(to_start + index);
        to_start += to_sizes[block];
      }
      return map;
    }
    case LayoutKind::kPrivileged:
      return BlockwiseMap(PrivilegedBlockSizes(from), PrivilegedBlockSizes(to));
    case LayoutKind::kActionFeature:
      return BlockwiseMap(ActionFeatureBlockSizes(from), ActionFeatureBlockSizes(to));
  }
  throw std::invalid_argument("unknown layout kind");
}

PolicyInputColumnMaps PolicyInputColumns(const LayoutDims& from, const LayoutDims& to) {
  const std::vector<int> training = LayoutIndexMap(LayoutKind::kTraining, from, to);
  const int from_public = PublicObservationSizeFor(from);
  const int to_public = PublicObservationSizeFor(to);
  PolicyInputColumnMaps maps;
  for (int column = 0; column < static_cast<int>(training.size()); ++column) {
    const int target = training[static_cast<std::size_t>(column)];
    if (column < from_public) {
      if (target >= to_public) throw std::logic_error("public input mapped outside the public part");
      maps.public_encoder.push_back(target);
    } else {
      if (target < to_public) throw std::logic_error("identity input mapped into the public part");
      maps.identity_encoder.push_back(target - to_public);
    }
  }
  maps.action_encoder = LayoutIndexMap(LayoutKind::kActionFeature, from, to);
  maps.privileged_encoder = LayoutIndexMap(LayoutKind::kPrivileged, from, to);
  return maps;
}

int TrainingObservationSizeFor(const LayoutDims& dims) {
  return BlockTotal(TrainingBlockSizes(dims));
}

int ActionFeatureSizeFor(const LayoutDims& dims) {
  return BlockTotal(ActionFeatureBlockSizes(dims));
}

int PrivilegedSizeFor(const LayoutDims& dims) {
  return BlockTotal(PrivilegedBlockSizes(dims));
}

int PublicObservationSizeFor(const LayoutDims& dims) {
  return TrainingStart(dims, TrainingBlock::kOwnHand);
}

JsonValue LayoutDimsJson(const LayoutDims& dims) {
  return JsonValue::Object{
      {"cardNames", dims.card_names},
      {"characters", dims.characters},
      {"cardInstances", dims.card_instances},
      {"sides", dims.sides},
      {"phases", dims.phases},
      {"cardKinds", dims.card_kinds},
      {"actionKinds", dims.action_kinds},
      {"revealReasons", dims.reveal_reasons},
      {"movementTriggers", dims.movement_triggers},
  };
}

LayoutDims LayoutDimsFromJson(const JsonValue& value) {
  const auto* object = std::get_if<JsonValue::Object>(&value.storage());
  if (object == nullptr) throw std::runtime_error("policy layout must be an object");
  const auto field = [&](const char* name) {
    const auto found = object->find(name);
    const double* number = found == object->end()
                               ? nullptr
                               : std::get_if<double>(&found->second.storage());
    if (number == nullptr || *number < 1 || *number != static_cast<int>(*number)) {
      throw std::runtime_error(std::string("policy layout has no valid ") + name);
    }
    return static_cast<int>(*number);
  };
  LayoutDims dims{field("cardNames"), field("characters"), field("cardInstances"),
                  field("sides"), field("phases"), field("cardKinds"), field("actionKinds")};
  // Recorded since the Whiskey reveal; earlier layouts had the base three.
  dims.reveal_reasons = object->contains("revealReasons") ? field("revealReasons")
                                                          : kBaseLayoutDims.reveal_reasons;
  dims.movement_triggers = object->contains("movementTriggers")
                               ? field("movementTriggers")
                               : kBaseLayoutDims.movement_triggers;
  return dims;
}

}  // namespace bang
