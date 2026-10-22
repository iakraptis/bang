#include "bang/input_layout.hpp"

#include <stdexcept>

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

}  // namespace bang
