#include "bang/training_state.hpp"

#include <cstring>
#include <stdexcept>

namespace bang {
namespace {

void HashWord(std::uint64_t& hash, std::uint64_t value) {
  for (int byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (byte * 8)) & 0xffU;
    hash *= 1099511628211ULL;
  }
}

std::uint32_t FloatBits(float value) {
  static_assert(sizeof(value) == sizeof(std::uint32_t));
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

}  // namespace

ObserverHistory::ObserverHistory(int observer) : observer_(observer) {
  if (observer < 0 || observer >= kSideCount) {
    throw std::out_of_range("observer history observer is out of range");
  }
}

void ObserverHistory::Record(const GameState& state) {
  const auto observation = state.TrainingObservationTensor(observer_);
  ObserverHistoryEvent event{
      .current_player = state.IsTerminal() ? -1 : state.CurrentPlayer(),
      .turn_number = state.TurnNumber(),
      .phase = state.CurrentPhase(),
      .changed_indices = {},
      .changed_values = {},
      .legal_actions =
          !state.IsTerminal() && state.CurrentPlayer() == observer_
              ? state.LegalActions()
              : std::vector<int>{},
  };
  for (int index = 0; index < kTrainingObservationSize; ++index) {
    const float previous =
        has_observation_ ? latest_observation_[static_cast<std::size_t>(index)]
                         : 0.0F;
    const float current = observation[static_cast<std::size_t>(index)];
    if (!has_observation_ || previous != current) {
      event.changed_indices.push_back(index);
      event.changed_values.push_back(current);
    }
  }
  HashWord(rolling_hash_, static_cast<std::uint64_t>(event.current_player + 1));
  HashWord(rolling_hash_, static_cast<std::uint64_t>(event.turn_number));
  HashWord(rolling_hash_, static_cast<std::uint64_t>(event.phase));
  for (std::size_t index = 0; index < event.changed_indices.size(); ++index) {
    HashWord(
        rolling_hash_,
        static_cast<std::uint64_t>(event.changed_indices[index]));
    HashWord(
        rolling_hash_,
        FloatBits(event.changed_values[index]));
  }
  for (const int action : event.legal_actions) {
    HashWord(rolling_hash_, static_cast<std::uint64_t>(action));
  }
  event.snapshot_hash = rolling_hash_;
  latest_observation_ = observation;
  has_observation_ = true;
  events_.push_back(std::move(event));
}

}  // namespace bang
