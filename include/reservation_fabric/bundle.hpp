#pragma once
#include <cstdint>
#include <vector>
#include <reservation_fabric/identity.hpp>
#include <reservation_fabric/lifecycle.hpp>

namespace reservation_fabric {

// Transaction state of an atomic (all-or-nothing) composite reservation set.
enum class SetTransactionState : std::uint8_t {
  None = 1,
  Validating,
  HoldsAcquired,
  Committing,
  Committed,
  RolledBack,
  Failed,
  Persisted
};

inline const char* to_string(SetTransactionState s) noexcept {
  switch (s) {
    case SetTransactionState::None: return "None";
    case SetTransactionState::Validating: return "Validating";
    case SetTransactionState::HoldsAcquired: return "HoldsAcquired";
    case SetTransactionState::Committing: return "Committing";
    case SetTransactionState::Committed: return "Committed";
    case SetTransactionState::RolledBack: return "RolledBack";
    case SetTransactionState::Failed: return "Failed";
    case SetTransactionState::Persisted: return "Persisted";
  }
  return "Unknown";
}

// An atomic composite reservation. At minimum ALL_OR_NOTHING semantics. The set
// is the unit of atomic admission: either every member reservation commits or
// none does, and no capacity leaks from a partially committed set.
struct ReservationSet {
  ReservationSetId id;
  ReservationSetGeneration generation;
  ReservationGroupId groupId;
  OwnerId owner;
  OwnerGeneration ownerGeneration;
  bool allOrNothing = true;
  SetTransactionState tx = SetTransactionState::None;
  bool committed = false;
  bool rolledBack = false;
  bool persisted = false;
  std::vector<ReservationId> members;     // ordered component reservations
  bool operator==(const ReservationSet& o) const noexcept {
    return id == o.id && generation == o.generation && groupId == o.groupId &&
           owner == o.owner && ownerGeneration == o.ownerGeneration &&
           allOrNothing == o.allOrNothing && committed == o.committed &&
           rolledBack == o.rolledBack && members == o.members;
  }
};

}  // namespace reservation_fabric
