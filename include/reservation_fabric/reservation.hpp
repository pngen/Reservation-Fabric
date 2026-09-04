#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <reservation_fabric/identity.hpp>
#include <reservation_fabric/quantity.hpp>
#include <reservation_fabric/time.hpp>
#include <reservation_fabric/lifecycle.hpp>
#include <reservation_fabric/request.hpp>
#include <reservation_fabric/resource.hpp>

namespace reservation_fabric {

// A committed reservation generation. This is an immutable value record: any
// modification creates the next generation and supersedes this one. Identity,
// generation, resource binding, quantity, interval, ownership and authority are
// all explicit.
struct Reservation {
  ReservationId id;
  ReservationGeneration generation;
  ReservationSetId setId;              // 0 = standalone reservation
  ReservationGroupId groupId;
  ReservationStrength strength = ReservationStrength::Hard;
  ResourceClass resourceClass = ResourceClass::Unknown;
  Unit unit = Unit::Count;
  // Binding (single-resource commitment). Composite sets are many of these.
  ResourceId resource;
  std::optional<ResourceGeneration> boundResourceGeneration;
  Quantity quantity = Quantity::zero(Unit::Count);   // reserved/committed quantity
  Quantity activated = Quantity::zero(Unit::Count);  // activated quantity
  Quantity consumed = Quantity::zero(Unit::Count);   // consumed quantity
  Interval window;
  ActivationMode activationMode = ActivationMode::ActivateNotBefore;
  Lifecycle lifecycle = Lifecycle::Requested;

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
  PolicyGeneration policyGeneration;
  PriorityGeneration priorityGeneration;
  std::int32_t priorityClass = 0;
  bool preemptible = false;
  ExclusivityMode exclusivity = ExclusivityMode::Shared;
  Elasticity elasticity = Elasticity::None;
  RenewalPermission renewal = RenewalPermission::None;
  TransferPermission transfer = TransferPermission::None;
  std::int32_t fragmentationTolerance = 0;
  bool allOrNothing = true;
  std::string provenance;

  // Generation-bounded authority markers.
  std::optional<ActivationGeneration> activationGeneration;
  std::optional<ConsumptionGeneration> consumptionGeneration;
  std::optional<ReleaseGeneration> releaseGeneration;
  std::optional<TransferGeneration> transferGeneration;
  std::optional<RecoveryGeneration> recoveryGeneration;

  // Runtime bookkeeping: whether this reservation is currently applied to the
  // capacity envelope, and any resource evidence that must be revalidated.
  bool contributesEnvelope = false;
  bool persisted = false;
  std::optional<Instant> activatedAt;
  std::optional<Instant> releasedAt;
};

inline Quantity remaining_quantity(const Reservation& r) noexcept {
  return (r.quantity.value() >= r.consumed.value()) ? Quantity(r.quantity.value() - r.consumed.value(), r.unit)
                                                     : Quantity::zero(r.unit);
}

}  // namespace reservation_fabric
