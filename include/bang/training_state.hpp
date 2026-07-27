#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "bang/game.hpp"

namespace bang {

struct ObserverHistoryEvent {
  int current_player = -1;
  int turn_number = 0;
  Phase phase = Phase::kChooseActive;
  std::vector<int> changed_indices;
  std::vector<float> changed_values;
  std::vector<int> legal_actions;
  std::uint64_t snapshot_hash = 0;
};

// Records only observer-visible tensors and legal actions. It is safe to use
// as recurrent policy input and deliberately has no access to private fields.
class ObserverHistory {
 public:
  explicit ObserverHistory(int observer);

  void Record(const GameState& state);
  int Observer() const { return observer_; }
  std::uint64_t Hash() const { return rolling_hash_; }
  const std::vector<ObserverHistoryEvent>& Events() const { return events_; }
  const std::array<float, kTrainingObservationSize>& LatestObservation() const {
    return latest_observation_;
  }

 private:
  int observer_ = 0;
  bool has_observation_ = false;
  std::array<float, kTrainingObservationSize> latest_observation_{};
  std::uint64_t rolling_hash_ = 1469598103934665603ULL;
  std::vector<ObserverHistoryEvent> events_;
};

}  // namespace bang
