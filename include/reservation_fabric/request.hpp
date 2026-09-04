#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <reservation_fabric/error.hpp>
#include <reservation_fabric/identity.hpp>
#include <reservation_fabric/quantity.hpp>
#include <reservation_fabric/resource.hpp>
#include <reservation_fabric/time.hpp>

namespace reservation_fabric {

// Reservation strength distinguishes a guaranteed commitment from a hint.
enum class ReservationStrength : std::uint8_t {
  Hard = 1,
  Soft,
  Opportunistic,
  BestEffort,
  Unknown
};

inline const char* to_string(ReservationStrength s) noexcept {
  switch (s) {
    case ReservationStrength::Hard: return "Hard";
    case ReservationStrength::Soft: return "Soft";
    case ReservationStrength::Opportunistic: return "Opportunistic";
    case ReservationStrength::BestEffort: return "BestEffort";
    case ReservationStrength::Unknown: return "Unknown";
  }
  return "Unknown";
}

// How the activation instant is chosen (if at all) within the reservation
// window. Recurrence is deliberately not modelled: it is not part of 1.0.0.
enum class ActivationMode : std::uint8_t {
  ActivateNotBefore = 1,     // may activate any instant at/after notBefore
  ImmediateAfterCommit,      // activates at commit instant
  FixedInterval,             // reserves exactly [start,end); activation within
  LatestStart,               // must start at/before latestStart
  DurationAfterActivation,   // duration measured from activation instant
  ExternalAuthority          // external authority triggers activation
};

inline const char* to_string(ActivationMode m) noexcept {
  switch (m) {
    case ActivationMode::ActivateNotBefore: return "ActivateNotBefore";
    case ActivationMode::ImmediateAfterCommit: return "ImmediateAfterCommit";
    case ActivationMode::FixedInterval: return "FixedInterval";
    case ActivationMode::LatestStart: return "LatestStart";
    case ActivationMode::DurationAfterActivation: return "DurationAfterActivation";
    case ActivationMode::ExternalAuthority: return "ExternalAuthority";
  }
  return "Unknown";
}

enum class ExclusivityMode : std::uint8_t {
  Shared = 1,
  Exclusive
};

enum class Elasticity : std::uint8_t {
  None = 1,
  ShrinkOnly,
  GrowShrink
};

enum class RenewalPermission : std::uint8_t {
  None = 1,
  RenewAllowed
};

enum class TransferPermission : std::uint8_t {
  None = 1,
  TransferAllowed
};

struct CapabilityConstraint {
  std::uint32_t requiredCapabilityId = 0;   // 0 = no constraint
  bool capacityRequiresCapability = false;
};

struct LocalityConstraint {
  ResourcePoolId preferredPool;
  PlacementId placement;                 // 0 = none
  ResourceClass preferredClass = ResourceClass::Unknown;
  std::int32_t localityWeight = 0;       // preference strength (0 = none)
};

struct TopologyConstraint {
  std::uint8_t topologyDomain = 0;        // 0 = none
  bool requireLocalPlacement = false;
};

struct CompatibilityConstraint {
  bool requireSameDriverGeneration = false;
};

// A request to reserve capacity. It is NOT a reservation: admission must succeed
// and a commitment must be created before any capacity is asserted.
struct ReservationRequest {
  ReservationRequestId requestId;
  ReservationRequestGeneration requestGeneration;
  ResourceClass resourceClass = ResourceClass::Unknown;
  Unit unit = Unit::Count;
  Quantity quantity = Quantity::zero(Unit::Count);
  bool partialAllowed = false;
  std::optional<Quantity> minAcceptable;   // valid only if partialAllowed
  Interval window;                         // the capacity window being requested
  ActivationMode activationMode = ActivationMode::ActivateNotBefore;
  std::optional<Duration> durationAfterActivation;
  ReservationStrength strength = ReservationStrength::Hard;
  OwnerId owner;
  OwnerGeneration ownerGeneration;
  std::optional<WorkloadId> workload;
  std::optional<WorkloadGeneration> workloadGeneration;
  std::optional<ExecutionId> execution;
  std::optional<ExecutionGeneration> executionGeneration;
  CapabilityConstraint capability;
  LocalityConstraint locality;
  TopologyConstraint topology;
  CompatibilityConstraint compatibility;
  std::optional<ResourceGeneration> minResourceGeneration;
  PolicyGeneration policyGeneration;
  PriorityGeneration priorityGeneration;
  std::int32_t priorityClass = 0;
  bool preemptible = false;
  ExclusivityMode exclusivity = ExclusivityMode::Shared;
  Elasticity elasticity = Elasticity::None;
  RenewalPermission renewal = RenewalPermission::None;
  TransferPermission transfer = TransferPermission::None;
  std::int32_t fragmentationTolerance = 0; // 0 = none
  bool allOrNothing = true;
  ResourcePoolId poolConstraint;          // 0 = any
  std::optional<ReservationGroupId> group;
  std::optional<ResourceId> targetResource; // 0/none = choose any admissible
  std::string provenance;
};

inline std::optional<ErrorCode> validate_request(const ReservationRequest& r) {
  if (!r.requestId.valid()) return ErrorCode::InvalidArgument;
  if (!r.requestGeneration.valid()) return ErrorCode::InvalidArgument;
  if (r.resourceClass == ResourceClass::Unknown) return ErrorCode::InvalidArgument;
  if (r.unit == Unit::None) return ErrorCode::InvalidQuantity;
  if (r.quantity.value() <= 0) return ErrorCode::InvalidQuantity;
  if (r.unit != unit_for(r.resourceClass)) return ErrorCode::InvalidQuantity;
  if (r.partialAllowed && !r.minAcceptable.has_value()) return ErrorCode::InvalidQuantity;
  if (r.partialAllowed && r.minAcceptable->unit() != r.unit) return ErrorCode::InvalidQuantity;
  if (r.partialAllowed && r.minAcceptable->value() <= 0) return ErrorCode::InvalidQuantity;
  if (r.partialAllowed && r.minAcceptable->value() > r.quantity.value()) return ErrorCode::InvalidQuantity;
  if (r.durationAfterActivation.has_value() && r.durationAfterActivation->isZero()) return ErrorCode::InvalidInterval;
  if (!r.owner.valid()) return ErrorCode::InvalidArgument;
  if (!r.ownerGeneration.valid()) return ErrorCode::InvalidArgument;
  if (r.strength == ReservationStrength::Unknown) return ErrorCode::InvalidArgument;  // never silently Hard
  if (r.quantity.unit() == Unit::Percent && (r.quantity.value() < 0 || r.quantity.value() > 100)) return ErrorCode::InvalidQuantity;
  return std::nullopt;
}

}  // namespace reservation_fabric
