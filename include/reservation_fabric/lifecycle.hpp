#pragma once
#include <cstdint>
#include <array>
#include <reservation_fabric/error.hpp>

namespace reservation_fabric {

// Clean final lifecycle taxonomy. States are either stable (a durable point in
// the reservation's history) or transient (an in-progress operation that may be
// interrupted and must be reconciled by recovery).
enum class Lifecycle : std::uint8_t {
  Requested = 1,             // created, not yet validated
  Planned,                   // validated, admissible plan exists
  Holding,                   // acquiring provisional holds (transient)
  Held,                      // provisional holds acquired; not committed
  Committing,                // authoritatively committing (transient)
  Committed,                 // durable advance commitment; pending activation
  PendingActivation,         // committed and awaiting activation conditions
  Activating,                // activation in progress (transient)
  Active,                    // activated; reservation-consuming capacity
  PartiallyConsumed,         // some but not all quantity consumed
  Consumed,                  // fully consumed within this generation
  Modifying,                 // mutation to a new generation in progress (transient)
  Renewing,                  // renewal in progress (transient)
  Releasing,                 // release in progress (transient)
  Released,                  // disengaged; no longer reduces capacity
  Expired,                   // window elapsed before activation/consume
  Cancelled,                 // explicit cancellation by authority
  Superseded,                // a newer authoritative generation replaced this
  Failed,                    // an operation failed; requires explicit healing
  Recovering,                // recovery/rebind in progress (transient)
  RevalidationRequired,      // durable promise remains; dynamic evidence stale
  Retired                    // archived history; safe to purge per policy
};

inline const char* to_string(Lifecycle l) noexcept {
  switch (l) {
    case Lifecycle::Requested: return "Requested";
    case Lifecycle::Planned: return "Planned";
    case Lifecycle::Holding: return "Holding";
    case Lifecycle::Held: return "Held";
    case Lifecycle::Committing: return "Committing";
    case Lifecycle::Committed: return "Committed";
    case Lifecycle::PendingActivation: return "PendingActivation";
    case Lifecycle::Activating: return "Activating";
    case Lifecycle::Active: return "Active";
    case Lifecycle::PartiallyConsumed: return "PartiallyConsumed";
    case Lifecycle::Consumed: return "Consumed";
    case Lifecycle::Modifying: return "Modifying";
    case Lifecycle::Renewing: return "Renewing";
    case Lifecycle::Releasing: return "Releasing";
    case Lifecycle::Released: return "Released";
    case Lifecycle::Expired: return "Expired";
    case Lifecycle::Cancelled: return "Cancelled";
    case Lifecycle::Superseded: return "Superseded";
    case Lifecycle::Failed: return "Failed";
    case Lifecycle::Recovering: return "Recovering";
    case Lifecycle::RevalidationRequired: return "RevalidationRequired";
    case Lifecycle::Retired: return "Retired";
  }
  return "Unknown";
}

// States that still hold capacity against the envelope/accounting.
inline bool lifecycle_holds_capacity(Lifecycle l) noexcept {
  switch (l) {
    case Lifecycle::Holding:
    case Lifecycle::Held:
    case Lifecycle::Committing:
    case Lifecycle::Committed:
    case Lifecycle::PendingActivation:
    case Lifecycle::Activating:
    case Lifecycle::Active:
    case Lifecycle::PartiallyConsumed:
    case Lifecycle::Consumed:
    case Lifecycle::Modifying:
    case Lifecycle::Renewing:
    case Lifecycle::Releasing:
    case Lifecycle::Recovering:
    case Lifecycle::RevalidationRequired:
      return true;
    default:
      return false;
  }
}

// An explicit, deterministic transition table. Transitions not listed are
// illegal and rejected. By design a reservation instance is immutable after
// Commit; modifications operate on a fresh generation rather than rewriting an
// existing generation's history.
inline bool can_transition(Lifecycle from, Lifecycle to) noexcept {
  switch (from) {
    case Lifecycle::Requested:
      return to == Lifecycle::Planned || to == Lifecycle::Failed || to == Lifecycle::Cancelled;
    case Lifecycle::Planned:
      return to == Lifecycle::Holding || to == Lifecycle::Cancelled || to == Lifecycle::Failed;
    case Lifecycle::Holding:
      return to == Lifecycle::Held || to == Lifecycle::Cancelled || to == Lifecycle::Failed;
    case Lifecycle::Held:
      return to == Lifecycle::Committing || to == Lifecycle::Cancelled || to == Lifecycle::Releasing || to == Lifecycle::Failed || to == Lifecycle::Expired;
    case Lifecycle::Committing:
      return to == Lifecycle::Committed || to == Lifecycle::Failed || to == Lifecycle::Cancelled;
    case Lifecycle::Committed:
      return to == Lifecycle::PendingActivation || to == Lifecycle::Cancelled || to == Lifecycle::Expired || to == Lifecycle::Modifying || to == Lifecycle::Renewing || to == Lifecycle::RevalidationRequired;
    case Lifecycle::PendingActivation:
      return to == Lifecycle::Activating || to == Lifecycle::Cancelled || to == Lifecycle::Expired || to == Lifecycle::Modifying || to == Lifecycle::Renewing || to == Lifecycle::RevalidationRequired || to == Lifecycle::Superseded;
    case Lifecycle::Activating:
      return to == Lifecycle::Active || to == Lifecycle::RevalidationRequired || to == Lifecycle::Cancelled || to == Lifecycle::Failed;
    case Lifecycle::Active:
      return to == Lifecycle::PartiallyConsumed || to == Lifecycle::Consumed || to == Lifecycle::Releasing || to == Lifecycle::RevalidationRequired || to == Lifecycle::Expired || to == Lifecycle::Modifying || to == Lifecycle::Renewing;
    case Lifecycle::PartiallyConsumed:
      return to == Lifecycle::Consumed || to == Lifecycle::PartiallyConsumed || to == Lifecycle::Releasing || to == Lifecycle::RevalidationRequired || to == Lifecycle::Modifying || to == Lifecycle::Renewing || to == Lifecycle::Expired;
    case Lifecycle::Consumed:
      return to == Lifecycle::Releasing || to == Lifecycle::Released || to == Lifecycle::RevalidationRequired || to == Lifecycle::Expired;
    case Lifecycle::Modifying:
      return to == Lifecycle::Committed || to == Lifecycle::PendingActivation || to == Lifecycle::Active || to == Lifecycle::Failed || to == Lifecycle::Superseded;
    case Lifecycle::Renewing:
      return to == Lifecycle::Committed || to == Lifecycle::PendingActivation || to == Lifecycle::Active || to == Lifecycle::Failed || to == Lifecycle::Superseded;
    case Lifecycle::Releasing:
      return to == Lifecycle::Released || to == Lifecycle::Failed;
    case Lifecycle::Released:
      return to == Lifecycle::Retired || to == Lifecycle::Superseded;
    case Lifecycle::Expired:
      return to == Lifecycle::Released || to == Lifecycle::Retired || to == Lifecycle::Superseded || to == Lifecycle::RevalidationRequired;
    case Lifecycle::Cancelled:
      return to == Lifecycle::Released || to == Lifecycle::Retired;
    case Lifecycle::Superseded:
      return to == Lifecycle::Retired;
    case Lifecycle::Failed:
      return to == Lifecycle::Requested || to == Lifecycle::Planned || to == Lifecycle::Retired || to == Lifecycle::Recovering;
    case Lifecycle::Recovering:
      return to == Lifecycle::Committed || to == Lifecycle::PendingActivation || to == Lifecycle::RevalidationRequired || to == Lifecycle::Failed;
    case Lifecycle::RevalidationRequired:
      return to == Lifecycle::PendingActivation || to == Lifecycle::Activating || to == Lifecycle::Committed || to == Lifecycle::Recovering || to == Lifecycle::Cancelled || to == Lifecycle::Releasing || to == Lifecycle::Expired;
    case Lifecycle::Retired:
      return false;
  }
  return false;
}

inline void require_transition(Lifecycle from, Lifecycle to) {
  if (!can_transition(from, to)) {
    throw_error(ErrorCode::InvalidTransition,
                std::string("illegal lifecycle transition: ") + to_string(from) + " -> " + to_string(to));
  }
}

}  // namespace reservation_fabric
