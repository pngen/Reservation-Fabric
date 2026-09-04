#pragma once
#include <reservation_fabric/runtime.hpp>
#include <reservation_fabric/detail/serialization.hpp>

#include <algorithm>
#include <deque>
#include <limits>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace reservation_fabric {
namespace detail {

inline Duration defaultHoldLease() { return Duration::seconds(900); }
inline Duration defaultHorizon() { return Duration::seconds(365LL * 24 * 3600); }
inline bool isHard(ReservationStrength s) { return s == ReservationStrength::Hard; }

struct QualifiedResource {
  ResourceId id;
  ResourceGeneration generation;
  ResourceClass resourceClass;
  Unit unit;
  Quantity capacity;
  ResourcePoolId pool;
};

}  // namespace detail

struct ReservationFabric::Impl {
  mutable std::shared_mutex lock;

  RuntimeConfig config;

  CoordinatorEpoch epoch = CoordinatorEpoch(1);
  AuthorityGeneration authority = AuthorityGeneration(1);

  std::unordered_map<ResourceId, Resource> resources;
  std::unordered_map<ResourceId, CapacityEnvelope> envelopes;
  std::unordered_map<WorkerId, WorkerBootId> workerBoots;

  std::unordered_map<ReservationRequestId, ReservationId> requestToReservation;
  std::unordered_map<ReservationId, Reservation> reservations;
  std::unordered_map<ReservationId, ReservationGeneration> currentGen;
  std::unordered_map<ReservationId, std::deque<Reservation>> history;

  std::unordered_map<ReservationSetId, ReservationSet> sets;
  std::unordered_map<ReservationHoldId, Hold> holds;

  std::uint64_t nextReservationId = 1;
  std::uint64_t nextHoldId = 1;
  std::uint64_t nextSetId = 1;

  Duration horizon = detail::defaultHorizon();

  Resource* findResourceLocked(ResourceId id) {
    auto it = resources.find(id);
    return it == resources.end() ? nullptr : &it->second;
  }
  const Resource* findResourceLocked(ResourceId id) const {
    auto it = resources.find(id);
    return it == resources.end() ? nullptr : &it->second;
  }
  CapacityEnvelope* envelopeLocked(ResourceId id) {
    auto it = envelopes.find(id);
    return it == envelopes.end() ? nullptr : &it->second;
  }
  const CapacityEnvelope* envelopeLocked(ResourceId id) const {
    auto it = envelopes.find(id);
    return it == envelopes.end() ? nullptr : &it->second;
  }
  Reservation* reservationLocked(ReservationId id) {
    auto it = reservations.find(id);
    return it == reservations.end() ? nullptr : &it->second;
  }
  const Reservation* reservationLocked(ReservationId id) const {
    auto it = reservations.find(id);
    return it == reservations.end() ? nullptr : &it->second;
  }
  Reservation* reservationByGenLocked(ReservationId id, ReservationGeneration gen) {
    auto it = reservations.find(id);
    if (it != reservations.end() && it->second.generation == gen) return &it->second;
    auto h = history.find(id);
    if (h != history.end()) {
      for (auto& r : h->second) if (r.generation == gen) return &r;
    }
    return nullptr;
  }

  bool resourceGenerationSatisfiesLocked(const Resource& res, const ReservationRequest& req) const {
    if (req.minResourceGeneration.has_value() && res.generation < *req.minResourceGeneration) return false;
    return true;
  }
  bool resourceConstraintsSatisfiedLocked(const Resource& res, const ReservationRequest& req) const {
    if (req.resourceClass != ResourceClass::Unknown && res.resourceClass != req.resourceClass) return false;
    if (res.unit != req.unit) return false;
    if (req.poolConstraint.valid() && res.pool != req.poolConstraint) return false;
    if (!res.available) return false;
    if (req.capability.requiredCapabilityId != 0 && res.capabilities.value() < req.capability.requiredCapabilityId) return false;
    if (req.locality.preferredPool.valid() && res.pool != req.locality.preferredPool) return false;
    return resourceGenerationSatisfiesLocked(res, req);
  }

  std::vector<detail::QualifiedResource> matchingResourcesLocked(const ReservationRequest& req) const {
    std::vector<detail::QualifiedResource> out;
    for (const auto& [id, res] : resources) {
      if (req.targetResource.has_value() && id != *req.targetResource) continue;
      if (req.resourceClass != ResourceClass::Unknown && res.resourceClass != req.resourceClass) continue;
      if (res.unit != req.unit) continue;
      if (!res.available) continue;
      if (req.minResourceGeneration.has_value() && res.generation < *req.minResourceGeneration) continue;
      out.push_back(detail::QualifiedResource{ id, res.generation, res.resourceClass, res.unit, res.totalCapacity, res.pool });
    }
    return out;
  }

  bool hasAnyResourceOfClassLocked(ResourceClass cls) const {
    for (const auto& [id, res] : resources) if (res.resourceClass == cls) return true;
    return false;
  }

  void addContributionLocked(Reservation& r) {
    CapacityEnvelope* e = envelopeLocked(r.resource);
    if (!e || r.contributesEnvelope) return;
    if (detail::isHard(r.strength)) e->addCommittedHard(r.window, r.quantity.value());
    else e->addCommittedSoft(r.window, r.quantity.value());
    r.contributesEnvelope = true;
  }
  void removeContributionLocked(Reservation& r) {
    CapacityEnvelope* e = envelopeLocked(r.resource);
    if (!e || !r.contributesEnvelope) return;
    if (detail::isHard(r.strength)) e->removeCommittedHard(r.window, r.quantity.value());
    else e->removeCommittedSoft(r.window, r.quantity.value());
    r.contributesEnvelope = false;
  }

  std::int64_t headroomForLocked(const ReservationRequest& req, ResourceId id) const {
    const CapacityEnvelope* e = envelopeLocked(id);
    if (e == nullptr) return 0;
    return detail::isHard(req.strength) ? e->minHardHeadroom(req.window) : e->minSoftHeadroom(req.window);
  }

  // Admission/feasibility evaluation for one request. Assume the caller holds
  // an exclusive lock.
  FeasibilityReport assessOne(const ReservationRequest& req);
};

// Acquire a provisional hold. Must be called with the exclusive lock held.
inline HoldResult acquireHoldLocked(ReservationFabric::Impl& d, const HoldSpec& spec) {
  HoldResult result;
  if (!spec.holdId.valid() || !spec.holdGeneration.valid()) { result.errorMessage = "invalid hold identity"; return result; }
  if (spec.quantity.value() <= 0) { result.errorMessage = "invalid hold quantity"; return result; }
  if (spec.unit != spec.quantity.unit() || spec.unit != unit_for(spec.resourceClass)) { result.errorMessage = "hold unit mismatch"; return result; }
  if (d.holds.size() >= d.config.maxHolds) { result.errorMessage = "hold count bound exceeded"; return result; }

  ReservationRequest probe;
  probe.requestId = ReservationRequestId(0);
  probe.requestGeneration = ReservationRequestGeneration(1);
  probe.resourceClass = spec.resourceClass;
  probe.unit = spec.unit;
  probe.quantity = spec.quantity;
  probe.window = spec.window;
  probe.strength = ReservationStrength::Hard;
  probe.owner = spec.owner;
  probe.ownerGeneration = spec.ownerGeneration;
  probe.policyGeneration = spec.policyGeneration;

  auto matches = d.matchingResourcesLocked(probe);
  ResourceId best;
  std::int64_t bestHeadroom = std::numeric_limits<std::int64_t>::min();
  for (const auto& m : matches) {
    std::int64_t h = d.headroomForLocked(probe, m.id);
    if (h > bestHeadroom || (h == bestHeadroom && (!best.valid() || m.id < best))) { bestHeadroom = h; best = m.id; }
  }
  if (!best.valid() || bestHeadroom < spec.quantity.value()) { result.errorMessage = "hold not admissible"; return result; }

  Hold h;
  h.spec = spec;
  h.state = HoldState::Acquired;
  h.resource = best;
  h.expiry = AbsoluteClock::now() + detail::defaultHoldLease();
  h.appliedToEnvelope = true;
  d.holds[spec.holdId] = h;
  CapacityEnvelope* e = d.envelopeLocked(best);
  if (e) e->addHeld(spec.window, spec.quantity.value());
  result.created = true;
  result.holdId = spec.holdId;
  result.holdGeneration = spec.holdGeneration;
  return result;
}

}  // namespace reservation_fabric