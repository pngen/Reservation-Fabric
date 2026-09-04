#include "impl.hpp"
#include <algorithm>
#include <sstream>

namespace reservation_fabric {

ReservationFabric::ReservationFabric(RuntimeConfig config) : impl_(std::make_unique<Impl>()) {
  impl_->config = config;
  impl_->horizon = config.capacityHorizon.isZero() ? detail::defaultHorizon() : config.capacityHorizon;
}
ReservationFabric::~ReservationFabric() = default;

// ---- authority ----
CoordinatorEpoch ReservationFabric::coordinatorEpoch() const {
  std::shared_lock<std::shared_mutex> l(impl_->lock);
  return impl_->epoch;
}
void ReservationFabric::setCoordinatorEpoch(CoordinatorEpoch epoch) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  if (epoch < impl_->epoch) throw_error(ErrorCode::StaleAuthority, "stale coordinator epoch rejected");
  impl_->epoch = epoch;
  impl_->authority = impl_->authority.next();
}
AuthorityGeneration ReservationFabric::authorityGeneration() const {
  std::shared_lock<std::shared_mutex> l(impl_->lock);
  return impl_->authority;
}

// ---- resource publications ----
ResourceRegisterResult ReservationFabric::publishResource(const ResourcePublication& pub, std::string* error) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  if (pub.totalCapacity.value() <= 0 || pub.totalCapacity.unit() != pub.unit) {
    if (error) *error = "invalid resource capacity";
    return ResourceRegisterResult::RejectedInvalid;
  }
  if (!pub.id.valid() || !pub.generation.valid() || !pub.worker.valid() || !pub.workerBoot.valid()) {
    if (error) *error = "invalid resource identity/authority";
    return ResourceRegisterResult::RejectedInvalid;
  }
  auto wb = impl_->workerBoots.find(pub.worker);
  if (wb != impl_->workerBoots.end() && wb->second > pub.workerBoot) {
    if (error) *error = "stale worker boot id";
    return ResourceRegisterResult::RejectedStale;
  }
  impl_->workerBoots[pub.worker] = pub.workerBoot;

  auto existingIt = impl_->resources.find(pub.id);
  if (existingIt != impl_->resources.end()) {
    if (pub.generation < existingIt->second.generation) {
      if (error) *error = "stale resource generation";
      return ResourceRegisterResult::RejectedStale;
    }
    if (pub.generation == existingIt->second.generation) {
      if (error) *error = "duplicate resource generation";
      return ResourceRegisterResult::RejectedDuplicate;
    }
  }

  Resource res;
  res.id = pub.id; res.generation = pub.generation; res.resourceClass = pub.resourceClass;
  res.unit = pub.unit; res.totalCapacity = pub.totalCapacity;
  res.pool = pub.pool; res.poolGeneration = pub.poolGeneration;
  res.owner = pub.worker; res.ownerBoot = pub.workerBoot;
  res.capabilities = pub.capabilities; res.topology = pub.topology;
  res.contract = pub.contract; res.contractGeneration = pub.contractGeneration;
  res.available = true;

  CapacityEnvelope env(pub.unit, pub.totalCapacity, pub.overbookRatio);
  if (!env.init(pub.availableFrom, impl_->horizon, pub.totalCapacity, pub.overbookRatio)) {
    if (error) *error = "capacity envelope init failed";
    return ResourceRegisterResult::RejectedInvalid;
  }
  for (const auto& w : pub.unavailable) {
    if (!(w.interval.end() <= pub.availableFrom || w.interval.start() >= pub.availableFrom + impl_->horizon)) {
      env.setUnavailable(w.interval, 0);
    }
  }

  bool advance = existingIt != impl_->resources.end();
  impl_->resources[pub.id] = res;
  impl_->envelopes[pub.id] = std::move(env);

  if (advance) {
    for (auto& [rid, r] : impl_->reservations) {
      if (r.resource == pub.id && lifecycle_holds_capacity(r.lifecycle)) {
        if (!r.contributesEnvelope) impl_->addContributionLocked(r);
        if (r.lifecycle == Lifecycle::Active || r.lifecycle == Lifecycle::PartiallyConsumed || r.lifecycle == Lifecycle::Consumed) {
          r.lifecycle = Lifecycle::RevalidationRequired;
        }
      }
    }
    return ResourceRegisterResult::AcceptedReplaced;
  }
  return ResourceRegisterResult::Accepted;
}

const Resource* ReservationFabric::resource(ResourceId id) const {
  std::shared_lock<std::shared_mutex> l(impl_->lock);
  return impl_->findResourceLocked(id);
}
std::vector<ResourceId> ReservationFabric::resourceIds() const {
  std::shared_lock<std::shared_mutex> l(impl_->lock);
  std::vector<ResourceId> ids; ids.reserve(impl_->resources.size());
  for (const auto& [id, r] : impl_->resources) ids.push_back(id);
  return ids;
}
void ReservationFabric::invalidateResource(const InvalidationNotice& notice) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  auto it = impl_->resources.find(notice.resource);
  if (it == impl_->resources.end()) throw_error(ErrorCode::NotFound, "resource not found");
  it->second.available = false;
  for (auto& [rid, r] : impl_->reservations) {
    if (r.resource == notice.resource && lifecycle_holds_capacity(r.lifecycle) &&
        !(r.lifecycle == Lifecycle::Released || r.lifecycle == Lifecycle::Expired || r.lifecycle == Lifecycle::Cancelled)) {
      r.lifecycle = Lifecycle::RevalidationRequired;
    }
  }
}
std::size_t ReservationFabric::advanceResourceGeneration(ResourceId id, ResourceGeneration newGeneration, ResourceCondition condition) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  auto it = impl_->resources.find(id);
  if (it == impl_->resources.end()) throw_error(ErrorCode::NotFound, "resource not found");
  if (newGeneration <= it->second.generation) throw_error(ErrorCode::StaleGeneration, "resource generation must advance");
  it->second.generation = newGeneration;
  it->second.available = (condition != ResourceCondition::Unavailable && condition != ResourceCondition::Invalidated);
  std::size_t flagged = 0;
  for (auto& [rid, r] : impl_->reservations) {
    if (r.resource == id && lifecycle_holds_capacity(r.lifecycle)) {
      if (r.lifecycle == Lifecycle::Active || r.lifecycle == Lifecycle::PartiallyConsumed || r.lifecycle == Lifecycle::Consumed) {
        r.lifecycle = Lifecycle::RevalidationRequired;
      }
      ++flagged;
    }
  }
  return flagged;
}
std::size_t ReservationFabric::revalidateResource(ResourceId id, const ResourcePublication& fresh) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  auto it = impl_->resources.find(id);
  if (it == impl_->resources.end()) throw_error(ErrorCode::NotFound, "resource not found");
  auto wb = impl_->workerBoots.find(fresh.worker);
  if (wb != impl_->workerBoots.end() && wb->second > fresh.workerBoot) throw_error(ErrorCode::StaleAuthority, "stale worker boot id");
  impl_->workerBoots[fresh.worker] = fresh.workerBoot;
  it->second.generation = fresh.generation;
  it->second.owner = fresh.worker; it->second.ownerBoot = fresh.workerBoot;
  it->second.capabilities = fresh.capabilities; it->second.topology = fresh.topology;
  it->second.available = true;
  std::size_t reactivated = 0;
  for (auto& [rid, r] : impl_->reservations) {
    if (r.resource == id && r.lifecycle == Lifecycle::RevalidationRequired) {
      r.lifecycle = Lifecycle::PendingActivation;
      r.boundResourceGeneration = fresh.generation;   // fresh evidence re-binds the promise
      ++reactivated;
    }
  }
  return reactivated;
}

// ---- feasibility ----
FeasibilityReport ReservationFabric::Impl::assessOne(const ReservationRequest& req) {
  FeasibilityReport rep;
  rep.resourceClass = req.resourceClass;
  rep.unit = req.unit;
  rep.requestedQuantity = req.quantity;
  rep.requestedInterval = req.window;
  rep.policyGeneration = req.policyGeneration;

  if (auto err = validate_request(req); err) {
    FeasibilityReport bad = FeasibilityReport::rejected(Feasibility::RejectInvalidQuantity, "invalid reservation request");
    rep = bad;
    rep.resourceClass = req.resourceClass; rep.unit = req.unit;
    rep.requestedQuantity = req.quantity; rep.requestedInterval = req.window;
    return rep;
  }
  // Duplicate request identity.
  auto dup = requestToReservation.find(req.requestId);
  if (dup != requestToReservation.end()) {
    auto it = reservations.find(dup->second);
    if (it != reservations.end() && lifecycle_holds_capacity(it->second.lifecycle)) {
      return FeasibilityReport::rejected(Feasibility::RejectDuplicate, "duplicate reservation request identity");
    }
  }

  auto matches = matchingResourcesLocked(req);
  if (matches.empty()) {
    if (!hasAnyResourceOfClassLocked(req.resourceClass)) {
      FeasibilityReport r = FeasibilityReport::rejected(Feasibility::RejectCapability, "no resource of the requested class is present");
      r.resourceClass = req.resourceClass; r.unit = req.unit; r.requestedQuantity = req.quantity; r.requestedInterval = req.window;
      return r;
    }
    if (!resourceConstraintsSatisfiedLocked(resources.begin()->second, req)) {
      FeasibilityReport r = FeasibilityReport::rejected(Feasibility::RejectTopology, "no matching resource satisfies topology/constraints");
      r.resourceClass = req.resourceClass; r.unit = req.unit; r.requestedQuantity = req.quantity; r.requestedInterval = req.window;
      return r;
    }
    FeasibilityReport r = FeasibilityReport::rejected(Feasibility::RejectCapacity, "no matching resource has capacity");
    r.resourceClass = req.resourceClass; r.unit = req.unit; r.requestedQuantity = req.quantity; r.requestedInterval = req.window;
    return r;
  }

  // Choose the matching resource with greatest headroom (deterministic on ties).
  ResourceId best;
  std::int64_t bestHeadroom = std::numeric_limits<std::int64_t>::min();
  for (const auto& m : matches) {
    std::int64_t h = headroomForLocked(req, m.id);
    if (h > bestHeadroom || (h == bestHeadroom && (!best.valid() || m.id < best))) {
      bestHeadroom = h; best = m.id;
    }
  }
  const Resource* chosen = findResourceLocked(best);
  rep.resource = best;
  rep.currentResourceGeneration = chosen ? chosen->generation : ResourceGeneration(0);
  rep.availableAtBottleneck = Quantity(bestHeadroom > 0 ? bestHeadroom : 0, req.unit);
  rep.bottleneckResources.push_back(best);

  rep.admissibleQuantity = Quantity(bestHeadroom > 0 ? bestHeadroom : 0, req.unit);
  if (bestHeadroom >= req.quantity.value()) {
    rep.outcome = Feasibility::Admissible;
    return rep;
  }
  if (req.partialAllowed && req.minAcceptable.has_value() && bestHeadroom >= req.minAcceptable->value()) {
    rep.outcome = Feasibility::AdmissibleWithModification;
    std::int64_t maxN = std::min(req.quantity.value(), bestHeadroom);
    rep.admissibleQuantity = Quantity(maxN, req.unit);
    rep.reasons.push_back("full requested quantity not available; reduced quantity admissible");
    return rep;
  }
  // Only when rejecting do we enumerate conflicting commitments for the
  // explanation, and we bound it so a hostile/huge schedule cannot make
  // admission O(N) per request.
  for (const auto& [rid, r] : reservations) {
    if (r.resource == best && lifecycle_holds_capacity(r.lifecycle) && r.window.overlaps(req.window)) {
      rep.conflictingReservations.push_back(rid);
      if (!detail::isHard(r.strength)) rep.blockedBySoftOnly = true;
      if (rep.conflictingReservations.size() >= 64) break;   // bounded explanation
    }
  }
  std::sort(rep.conflictingReservations.begin(), rep.conflictingReservations.end());
  rep.outcome = (rep.blockedBySoftOnly && bestHeadroom > 0) ? Feasibility::RejectCapacity : Feasibility::RejectConflict;
  rep.reasons.push_back("requested quantity exceeds remaining headroom over the window");
  return rep;
}

FeasibilityReport ReservationFabric::assess(const ReservationRequest& req) const {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  return impl_->assessOne(req);
}
std::vector<FeasibilityReport> ReservationFabric::assess(const std::vector<ReservationRequest>& bundle) const {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  std::vector<FeasibilityReport> out;
  out.reserve(bundle.size());
  for (const auto& r : bundle) out.push_back(impl_->assessOne(r));
  return out;
}

Quantity ReservationFabric::headroomAt(ResourceId id, Instant at, bool hard) const {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  CapacityEnvelope* e = impl_->envelopeLocked(id);
  if (!e) return Quantity::zero(Unit::Count);
  Interval win(at, Duration::nanoseconds(1));
  std::int64_t h = hard ? e->minHardHeadroom(win) : e->minSoftHeadroom(win);
  return Quantity(h < 0 ? 0 : h, e->unit());
}
Quantity ReservationFabric::headroomOverWindow(ResourceId id, const Interval& window, bool hard) const {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  CapacityEnvelope* e = impl_->envelopeLocked(id);
  if (!e) return Quantity::zero(Unit::Count);
  std::int64_t h = hard ? e->minHardHeadroom(window) : e->minSoftHeadroom(window);
  return Quantity(h < 0 ? 0 : h, e->unit());
}
Quantity ReservationFabric::capacityAt(ResourceId id, Instant at) const {
  (void)at;
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  const Resource* r = impl_->findResourceLocked(id);
  if (!r) return Quantity::zero(Unit::Count);
  return r->totalCapacity;
}

// ---- holds ----
HoldResult ReservationFabric::createHold(const HoldSpec& spec) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  return acquireHoldLocked(*impl_, spec);
}

void ReservationFabric::releaseHold(ReservationHoldId id, ReservationHoldGeneration generation) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  auto it = impl_->holds.find(id);
  if (it == impl_->holds.end()) throw_error(ErrorCode::NotFound, "hold not found");
  if (it->second.spec.holdGeneration != generation) throw_error(ErrorCode::StaleGeneration, "stale hold generation");
  if (it->second.state == HoldState::Released || it->second.state == HoldState::Expired) return;  // idempotent
  if (it->second.appliedToEnvelope) {
    if (CapacityEnvelope* e = impl_->envelopeLocked(it->second.resource)) {
      e->addHeld(it->second.spec.window, -it->second.spec.quantity.value());
    }
    it->second.appliedToEnvelope = false;
  }
  it->second.state = HoldState::Released;
}
void ReservationFabric::expireHolds(Instant now) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  for (auto& [id, h] : impl_->holds) {
    if (h.state == HoldState::Acquired && h.expiry < now) {
      if (h.appliedToEnvelope) {
        if (CapacityEnvelope* e = impl_->envelopeLocked(h.resource)) {
          e->addHeld(h.spec.window, -h.spec.quantity.value());
        }
        h.appliedToEnvelope = false;
      }
      h.state = HoldState::Expired;
    }
  }
}
HoldState ReservationFabric::holdState(ReservationHoldId id) const {
  std::shared_lock<std::shared_mutex> l(impl_->lock);
  auto it = impl_->holds.find(id);
  return it == impl_->holds.end() ? HoldState::Released : it->second.state;
}

}  // namespace reservation_fabric