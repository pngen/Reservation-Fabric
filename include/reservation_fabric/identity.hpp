#pragma once
#include <cstdint>
#include <functional>
#include <limits>

#include <reservation_fabric/error.hpp>

namespace reservation_fabric {

template <class Tag> class StrongId;
template <class Tag> class StrongGeneration;

// ---- Identity tags ---------------------------------------------------------
// Each semantic identity domain is a distinct Tag so that the compiler prevents
// cross-domain substitution (e.g. a HoldId where a ReservationId is needed).
struct ReservationIdTag {};
struct ReservationGenerationTag {};
struct ReservationRequestIdTag {};
struct ReservationRequestGenerationTag {};
struct ReservationSetIdTag {};
struct ReservationSetGenerationTag {};
struct ReservationGroupIdTag {};
struct ReservationGroupGenerationTag {};
struct ReservationHoldIdTag {};
struct ReservationHoldGenerationTag {};
struct ResourceIdTag {};
struct ResourceGenerationTag {};
struct ResourcePoolIdTag {};
struct ResourcePoolGenerationTag {};
struct ResourceContractIdTag {};
struct ResourceContractGenerationTag {};
struct CapacityGenerationTag {};
struct OwnerIdTag {};
struct OwnerGenerationTag {};
struct WorkloadIdTag {};
struct WorkloadGenerationTag {};
struct ExecutionIdTag {};
struct ExecutionGenerationTag {};
struct TenantIdTag {};
struct PlacementIdTag {};
struct TopologyGenerationTag {};
struct CapabilityGenerationTag {};
struct PolicyGenerationTag {};
struct PriorityGenerationTag {};
struct CoordinatorEpochTag {};
struct WorkerIdTag {};
struct WorkerBootIdTag {};
struct ActivationGenerationTag {};
struct ConsumptionGenerationTag {};
struct ReleaseGenerationTag {};
struct TransferGenerationTag {};
struct RecoveryGenerationTag {};
struct AuthorityGenerationTag {};
struct ReservationAuthorityIdTag {};

// A strong identity value never silently interchanges with another domain.
// Values are 1-based; 0 denotes "none".
template <class Tag>
class StrongId {
 public:
  using value_type = std::uint64_t;
  constexpr StrongId() noexcept = default;
  constexpr explicit StrongId(value_type value) noexcept : value_(value) {}
  constexpr bool valid() const noexcept { return value_ != 0; }
  constexpr value_type value() const noexcept { return value_; }
  constexpr bool operator==(StrongId other) const noexcept { return value_ == other.value_; }
  constexpr bool operator!=(StrongId other) const noexcept { return value_ != other.value_; }
  constexpr bool operator<(StrongId other) const noexcept { return value_ < other.value_; }
  constexpr bool operator<=(StrongId other) const noexcept { return value_ <= other.value_; }
  constexpr bool operator>(StrongId other) const noexcept { return value_ > other.value_; }
  constexpr bool operator>=(StrongId other) const noexcept { return value_ >= other.value_; }
  explicit constexpr operator bool() const noexcept { return valid(); }
 private:
  value_type value_ = 0;
};

// A monotonic generation. Generations never move backward and never wrap.
// next() throws QuantityOverflow/StaleGeneration-class errors on overflow so
// that authority can never silently restart.
template <class Tag>
class StrongGeneration {
 public:
  using value_type = std::uint64_t;
  constexpr StrongGeneration() noexcept = default;
  constexpr explicit StrongGeneration(value_type value) noexcept : value_(value) {}
  constexpr bool valid() const noexcept { return value_ != 0; }
  constexpr value_type value() const noexcept { return value_; }

  StrongGeneration next() const {
    if (value_ == std::numeric_limits<value_type>::max()) {
      throw_error(ErrorCode::QuantityOverflow,
                  "generation overflow: cannot advance a saturated generation");
    }
    return StrongGeneration(value_ + 1);
  }

  // Is this generation strictly newer than (greater than) 'other'?
  constexpr bool isNewerThan(StrongGeneration other) const noexcept { return value_ > other.value_; }
  constexpr bool isSameAs(StrongGeneration other) const noexcept { return value_ == other.value_; }
  constexpr bool operator==(StrongGeneration other) const noexcept { return value_ == other.value_; }
  constexpr bool operator!=(StrongGeneration other) const noexcept { return value_ != other.value_; }
  constexpr bool operator<(StrongGeneration other) const noexcept { return value_ < other.value_; }
  constexpr bool operator<=(StrongGeneration other) const noexcept { return value_ <= other.value_; }
  constexpr bool operator>(StrongGeneration other) const noexcept { return value_ > other.value_; }
  constexpr bool operator>=(StrongGeneration other) const noexcept { return value_ >= other.value_; }
 private:
  value_type value_ = 0;
};

// ---- Strongly typed aliases ------------------------------------------------
using ReservationId = StrongId<ReservationIdTag>;
using ReservationGeneration = StrongGeneration<ReservationGenerationTag>;
using ReservationRequestId = StrongId<ReservationRequestIdTag>;
using ReservationRequestGeneration = StrongGeneration<ReservationRequestGenerationTag>;
using ReservationSetId = StrongId<ReservationSetIdTag>;
using ReservationSetGeneration = StrongGeneration<ReservationSetGenerationTag>;
using ReservationGroupId = StrongId<ReservationGroupIdTag>;
using ReservationGroupGeneration = StrongGeneration<ReservationGroupGenerationTag>;
using ReservationHoldId = StrongId<ReservationHoldIdTag>;
using ReservationHoldGeneration = StrongGeneration<ReservationHoldGenerationTag>;
using ResourceId = StrongId<ResourceIdTag>;
using ResourceGeneration = StrongGeneration<ResourceGenerationTag>;
using ResourcePoolId = StrongId<ResourcePoolIdTag>;
using ResourcePoolGeneration = StrongGeneration<ResourcePoolGenerationTag>;
using ResourceContractId = StrongId<ResourceContractIdTag>;
using ResourceContractGeneration = StrongGeneration<ResourceContractGenerationTag>;
using CapacityGeneration = StrongGeneration<CapacityGenerationTag>;
using OwnerId = StrongId<OwnerIdTag>;
using OwnerGeneration = StrongGeneration<OwnerGenerationTag>;
using WorkloadId = StrongId<WorkloadIdTag>;
using WorkloadGeneration = StrongGeneration<WorkloadGenerationTag>;
using ExecutionId = StrongId<ExecutionIdTag>;
using ExecutionGeneration = StrongGeneration<ExecutionGenerationTag>;
using TenantId = StrongId<TenantIdTag>;
using PlacementId = StrongId<PlacementIdTag>;
using TopologyGeneration = StrongGeneration<TopologyGenerationTag>;
using CapabilityGeneration = StrongGeneration<CapabilityGenerationTag>;
using PolicyGeneration = StrongGeneration<PolicyGenerationTag>;
using PriorityGeneration = StrongGeneration<PriorityGenerationTag>;
using CoordinatorEpoch = StrongGeneration<CoordinatorEpochTag>;
using WorkerId = StrongId<WorkerIdTag>;
using WorkerBootId = StrongGeneration<WorkerBootIdTag>;
using ActivationGeneration = StrongGeneration<ActivationGenerationTag>;
using ConsumptionGeneration = StrongGeneration<ConsumptionGenerationTag>;
using ReleaseGeneration = StrongGeneration<ReleaseGenerationTag>;
using TransferGeneration = StrongGeneration<TransferGenerationTag>;
using RecoveryGeneration = StrongGeneration<RecoveryGenerationTag>;
using AuthorityGeneration = StrongGeneration<AuthorityGenerationTag>;

}  // namespace reservation_fabric

namespace std {
template <class Tag>
struct hash<reservation_fabric::StrongId<Tag>> {
  size_t operator()(const reservation_fabric::StrongId<Tag>& id) const noexcept {
    return std::hash<std::uint64_t>{}(id.value());
  }
};
template <class Tag>
struct hash<reservation_fabric::StrongGeneration<Tag>> {
  size_t operator()(const reservation_fabric::StrongGeneration<Tag>& g) const noexcept {
    return std::hash<std::uint64_t>{}(g.value());
  }
};
}  // namespace std
