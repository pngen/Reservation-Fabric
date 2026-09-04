#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <reservation_fabric/identity.hpp>
#include <reservation_fabric/quantity.hpp>
#include <reservation_fabric/time.hpp>
#include <reservation_fabric/resource.hpp>
#include <reservation_fabric/request.hpp>
#include <reservation_fabric/hold.hpp>
#include <reservation_fabric/reservation.hpp>
#include <reservation_fabric/bundle.hpp>
#include <reservation_fabric/feasibility.hpp>
#include <reservation_fabric/capacity.hpp>

namespace reservation_fabric {

// A resource publication from a worker. A publication is evidence, not a
// commitment: it is bound to a fresh WorkerBootId and a resource generation so
// that stale evidence can never satisfy a current reservation.
struct ResourcePublication {
  WorkerId worker;
  WorkerBootId workerBoot;
  ResourceId id;
  ResourceGeneration generation;
  ResourceClass resourceClass = ResourceClass::Unknown;
  Unit unit = Unit::Count;
  Quantity totalCapacity = Quantity::zero(Unit::Count);
  ResourcePoolId pool;
  ResourcePoolGeneration poolGeneration;
  CapabilityGeneration capabilities;
  TopologyGeneration topology;
  ResourceContractId contract;
  ResourceContractGeneration contractGeneration;
  Instant availableFrom;
  Duration lifetime = Duration::seconds(0);
  std::vector<UnavailableWindow> unavailable;
  Ratio overbookRatio = Ratio::one();
  std::vector<ResourceClass> overbookableClasses;
};

enum class ResourceRegisterResult : std::uint8_t {
  Accepted = 1, AcceptedReplaced, RejectedStale, RejectedInvalid, RejectedDuplicate
};
inline const char* to_string(ResourceRegisterResult r) noexcept {
  switch (r) {
    case ResourceRegisterResult::Accepted: return "Accepted";
    case ResourceRegisterResult::AcceptedReplaced: return "AcceptedReplaced";
    case ResourceRegisterResult::RejectedStale: return "RejectedStale";
    case ResourceRegisterResult::RejectedInvalid: return "RejectedInvalid";
    case ResourceRegisterResult::RejectedDuplicate: return "RejectedDuplicate";
  }
  return "Unknown";
}

struct ActivationEvidence {
  WorkerId worker;
  WorkerBootId workerBoot;           // must be current for the resource owner
  ActivationGeneration activationGeneration;
  Instant at;
  std::uint64_t token = 0;           // opaque external authority token
};

struct ConsumeEvidence {
  WorkerId worker;
  WorkerBootId workerBoot;
  ConsumptionGeneration consumptionGeneration;
  Instant at;
};

struct ReleaseEvidence {
  WorkerId worker;
  WorkerBootId workerBoot;
  ReleaseGeneration releaseGeneration;
  Instant at;
};

struct ResultTrait { std::string errorMessage; };

struct ReservationResult : ResultTrait {
  bool committed = false;
  bool idempotent = false;
  ReservationId id;
  ReservationGeneration generation;
  FeasibilityReport feasibility;
};

struct CompositeResult : ResultTrait {
  bool committed = false;
  bool rolledBack = false;
  ReservationSetId setId;
  ReservationSetGeneration setGeneration;
  std::vector<ReservationId> memberIds;
  std::vector<FeasibilityReport> assessments;
};

struct HoldResult : ResultTrait {
  bool created = false;
  ReservationHoldId holdId;
  ReservationHoldGeneration holdGeneration;
  FeasibilityReport feasibility;
};

struct ActivationResult : ResultTrait {
  bool activated = false;
  bool revalidationRequired = false;
  ReservationId id;
  ReservationGeneration generation;
};

struct ConsumeResult : ResultTrait {
  bool consumed = false;
  bool idempotent = false;
  ReservationId id;
  ReservationGeneration generation;
  Quantity consumedQuantity = Quantity::zero(Unit::Count);
  Quantity remaining = Quantity::zero(Unit::Count);
};

struct ReleaseResult : ResultTrait {
  bool released = false;
  bool idempotent = false;
  ReservationId id;
  ReservationGeneration generation;
};

struct ModifyResult : ResultTrait {
  bool applied = false;
  ReservationId id;
  ReservationGeneration oldGeneration;      // superseded
  ReservationGeneration newGeneration;      // current
  std::int64_t releasedDelta = 0;           // >0 if reduced
  std::int64_t acquiredDelta = 0;           // >0 if increased
};

enum class ResourceCondition : std::uint8_t {
  Available = 1, Unavailable, Invalidated, Drained, Degraded, Unknown
};
inline const char* to_string(ResourceCondition c) noexcept {
  switch (c) { case ResourceCondition::Available: return "Available"; case ResourceCondition::Unavailable: return "Unavailable";
    case ResourceCondition::Invalidated: return "Invalidated"; case ResourceCondition::Drained: return "Drained";
    case ResourceCondition::Degraded: return "Degraded"; case ResourceCondition::Unknown: return "Unknown"; }
  return "Unknown";
}
struct InvalidationNotice {
  ResourceId resource;
  ResourceGeneration obsoleteAtGeneration;   // the generation being invalidated
  ResourceCondition condition = ResourceCondition::Unavailable;
  std::string reason;
};

struct RuntimeConfig {
  std::size_t maxHistoryPerReservation = 64;   // bounded history retention
  std::size_t maxHolds = 256;                  // bound provisional holds
  std::size_t maxResources = 1024;
  std::size_t maxReservations = 1000000;
  std::size_t maxReservationSetMembers = 32;   // bound bundle size
  Duration capacityHorizon = Duration::seconds(0); // default: 365 days
};

// The central, thread-safe reservation runtime. It is the single authority for
// commitment/activation/release state. Transport (the TCP coordinator) is a
// reference deployment mechanism layered on top; the runtime itself is usable
// directly without any transport.
class ReservationFabric {
 public:
  explicit ReservationFabric(RuntimeConfig config = RuntimeConfig{});
  ~ReservationFabric();
  ReservationFabric(const ReservationFabric&) = delete;
  ReservationFabric& operator=(const ReservationFabric&) = delete;

  // ---- authority ----
  CoordinatorEpoch coordinatorEpoch() const;
  // Advance the coordinator epoch. Rejects regression (stale authority).
  void setCoordinatorEpoch(CoordinatorEpoch epoch);
  AuthorityGeneration authorityGeneration() const;

  // ---- resource publications (from workers) ----
  ResourceRegisterResult publishResource(const ResourcePublication& pub, std::string* error = nullptr);
  const Resource* resource(ResourceId id) const;
  std::vector<ResourceId> resourceIds() const;
  void invalidateResource(const InvalidationNotice& notice);
  // Advance to a fresh generation; returns the number of reservations flagged.
  std::size_t advanceResourceGeneration(ResourceId id, ResourceGeneration newGeneration,
                                        ResourceCondition condition);
  std::size_t revalidateResource(ResourceId id, const ResourcePublication& fresh);

  // ---- feasibility / admission ----
  FeasibilityReport assess(const ReservationRequest& req) const;
  std::vector<FeasibilityReport> assess(const std::vector<ReservationRequest>& bundle) const;
  Quantity headroomAt(ResourceId id, Instant at, bool hard = true) const;
  Quantity headroomOverWindow(ResourceId id, const Interval& window, bool hard = true) const;
  Quantity capacityAt(ResourceId id, Instant at) const;

  // ---- holds ----
  HoldResult createHold(const HoldSpec& spec);
  void releaseHold(ReservationHoldId id, ReservationHoldGeneration generation);
  void expireHolds(Instant now);
  HoldState holdState(ReservationHoldId id) const;

  // ---- commit ----
  ReservationResult commit(const ReservationRequest& req, std::optional<ReservationHoldId> hold = std::nullopt);
  CompositeResult commitBundle(const std::vector<ReservationRequest>& members,
                               std::optional<ReservationHoldId> hold = std::nullopt);

  // ---- activation / consumption / release ----
  ActivationResult activate(ReservationId id, ReservationGeneration generation,
                            const ActivationEvidence& evidence);
  ConsumeResult consume(ReservationId id, ReservationGeneration generation, Quantity amount,
                        const ConsumeEvidence& evidence);
  ReleaseResult release(ReservationId id, ReservationGeneration generation,
                        const ReleaseEvidence& evidence);

  // ---- modification / renewal ----
  ModifyResult resize(ReservationId id, ReservationGeneration generation, Quantity newQuantity,
                      PolicyGeneration policyGeneration);
  ModifyResult renew(ReservationId id, ReservationGeneration generation, const Interval& newWindow,
                     PolicyGeneration policyGeneration);
  ModifyResult transfer(ReservationId id, ReservationGeneration generation, OwnerId newOwner,
                        OwnerGeneration newOwnerGeneration, const ActivationEvidence& evidence);

  // ---- expiration / cancellation ----
  void cancel(ReservationId id, ReservationGeneration generation);
  std::size_t expireDue(Instant now);

  // ---- queries ----
  const Reservation* reservation(ReservationId id) const;         // current generation
  const Reservation* reservation(ReservationId id, ReservationGeneration generation) const;  // a specific generation in history
  std::vector<ReservationId> reservationIds() const;
  std::vector<ReservationId> reservationsForResource(ResourceId id) const;
  std::vector<ReservationId> reservationsForOwner(OwnerId owner) const;
  std::size_t currentReservationCount() const;
  CapacityAccounting accounting(ResourceId id) const;

  // ---- persistence ----
  void save(const std::string& path) const;
  static std::shared_ptr<ReservationFabric> load(const std::string& path);

  // ---- introspection ----
  std::size_t resourceCount() const;
  std::size_t holdCount() const;
  std::size_t setCount() const;
  std::string dumpInventory() const;

  // The implementation is defined in the (non-installed) private source header.
  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace reservation_fabric
