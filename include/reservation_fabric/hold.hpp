#pragma once
#include <cstdint>
#include <optional>
#include <vector>
#include <reservation_fabric/identity.hpp>
#include <reservation_fabric/quantity.hpp>
#include <reservation_fabric/time.hpp>
#include <reservation_fabric/resource.hpp>
#include <reservation_fabric/request.hpp>

namespace reservation_fabric {

// Hold lifecycle. A hold is bounded and must be released, expire, or be
// committed into a reservation.
enum class HoldState : std::uint8_t {
  Acquired = 1,
  Releasing,
  Released,
  Expired,
  Committed
};

inline const char* to_string(HoldState s) noexcept {
  switch (s) {
    case HoldState::Acquired: return "Acquired";
    case HoldState::Releasing: return "Releasing";
    case HoldState::Released: return "Released";
    case HoldState::Expired: return "Expired";
    case HoldState::Committed: return "Committed";
  }
  return "Unknown";
}

// A provisional, bounded hold. A hold is first-class: it reserves candidate
// capacity for atomic planning WITHOUT creating a durable commitment. Holds must
// be bounded and must release on expiry.
struct HoldSpec {
  ReservationHoldId holdId;
  ReservationHoldGeneration holdGeneration;
  OwnerId owner;
  OwnerGeneration ownerGeneration;
  ResourceClass resourceClass = ResourceClass::Unknown;
  Unit unit = Unit::Count;
  Quantity quantity = Quantity::zero(Unit::Count);
  Interval window;
  PolicyGeneration policyGeneration;
  bool commitAuthority = false;          // may commit this hold into a reservation
  std::string provenance;

  bool operator==(const HoldSpec& o) const noexcept {
    return holdId == o.holdId && holdGeneration == o.holdGeneration && owner == o.owner &&
           ownerGeneration == o.ownerGeneration && resourceClass == o.resourceClass &&
           unit == o.unit && quantity == o.quantity && window == o.window &&
           policyGeneration == o.policyGeneration && commitAuthority == o.commitAuthority;
  }
};

// A live hold with its state and envelope-application tracking.
struct Hold {
  HoldSpec spec;
  HoldState state = HoldState::Acquired;
  ResourceId resource;                 // resource this hold was applied to
  bool appliedToEnvelope = false;   // false until the hold reduces headroom
  std::optional<ActivationMode> activationMode;
  Instant expiry;                   // when an unreserved hold must release
  bool persisted = false;
};

}  // namespace reservation_fabric
