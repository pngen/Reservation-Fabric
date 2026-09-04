#include "impl.hpp"
#include <algorithm>

namespace reservation_fabric {
namespace {

Reservation makeReservationFromRequest(const ReservationRequest& req, ResourceId resource, ResourceGeneration boundGen, ReservationId id, ReservationGeneration gen) {
  Reservation r;
  r.id = id; r.generation = gen;
  r.strength = req.strength;
  r.resourceClass = req.resourceClass; r.unit = req.unit;
  r.resource = resource; r.boundResourceGeneration = boundGen;
  r.quantity = req.quantity;
  r.window = req.window; r.activationMode = req.activationMode;
  r.lifecycle = Lifecycle::Committed;
  r.owner = req.owner; r.ownerGeneration = req.ownerGeneration;
  r.workload = req.workload; r.workloadGeneration = req.workloadGeneration;
  r.execution = req.execution; r.executionGeneration = req.executionGeneration;
  r.capability = req.capability; r.locality = req.locality; r.topology = req.topology; r.compatibility = req.compatibility;
  r.policyGeneration = req.policyGeneration; r.priorityGeneration = req.priorityGeneration;
  r.priorityClass = req.priorityClass; r.preemptible = req.preemptible;
  r.exclusivity = req.exclusivity; r.elasticity = req.elasticity;
  r.renewal = req.renewal; r.transfer = req.transfer;
  r.fragmentationTolerance = req.fragmentationTolerance; r.allOrNothing = req.allOrNothing;
  r.provenance = req.provenance;
  r.contributesEnvelope = false;   // applied to the envelope by the caller
  return r;
}

// Commit a single member given an (optional) acquired hold. Assumes exclusive lock.
ReservationResult commitOneLocked(ReservationFabric::Impl& d, const ReservationRequest& req, Hold* hold) {
  ReservationResult out;
  out.feasibility = d.assessOne(req);
  if (!out.feasibility.admissible()) {
    out.errorMessage = "not admissible";
    return out;
  }
  ResourceId resourceId = out.feasibility.resource;
  const Resource* res = d.findResourceLocked(resourceId);
  if (!res) { out.errorMessage = "selected resource missing"; return out; }
  if (hold) {
    if (hold->state != HoldState::Acquired) { out.errorMessage = "hold is not acquired"; return out; }
    if (hold->resource != resourceId) { out.errorMessage = "hold resource mismatch"; return out; }
  }

  ReservationId rid(++d.nextReservationId);
  auto cg = d.currentGen.find(rid);
  ReservationGeneration gen = (cg == d.currentGen.end()) ? ReservationGeneration(1) : cg->second.next();
  d.currentGen[rid] = gen;

  Reservation r = makeReservationFromRequest(req, resourceId, res->generation, rid, gen);
  d.addContributionLocked(r);
  d.reservations[rid] = r;
  auto existing = d.requestToReservation.find(req.requestId);
  if (existing == d.requestToReservation.end()) d.requestToReservation[req.requestId] = rid;

  if (hold) {
    if (CapacityEnvelope* e = d.envelopeLocked(hold->resource)) {
      e->addHeld(hold->spec.window, -hold->spec.quantity.value());
    }
    hold->appliedToEnvelope = false;
    hold->state = HoldState::Committed;
  }

  out.committed = true;
  out.id = rid;
  out.generation = gen;
  return out;
}

void releaseHoldLocked(ReservationFabric::Impl& d, ReservationHoldId id) {
  auto it = d.holds.find(id);
  if (it == d.holds.end()) return;
  if (it->second.state == HoldState::Released || it->second.state == HoldState::Expired || it->second.state == HoldState::Committed) {
    it->second.state = HoldState::Released;
    it->second.appliedToEnvelope = false;
    return;
  }
  if (it->second.appliedToEnvelope) {
    if (CapacityEnvelope* e = d.envelopeLocked(it->second.resource)) e->addHeld(it->second.spec.window, -it->second.spec.quantity.value());
    it->second.appliedToEnvelope = false;
  }
  it->second.state = HoldState::Released;
}

}  // namespace

ReservationResult ReservationFabric::commit(const ReservationRequest& req, std::optional<ReservationHoldId> hold) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  Hold* hp = nullptr;
  if (hold) {
    auto it = impl_->holds.find(*hold);
    if (it == impl_->holds.end()) {
      ReservationResult out; out.errorMessage = "hold not found"; return out;
    }
    hp = &it->second;
  }
  return commitOneLocked(*impl_, req, hp);
}

CompositeResult ReservationFabric::commitBundle(const std::vector<ReservationRequest>& members, std::optional<ReservationHoldId> hold) {
  (void)hold;
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  CompositeResult out;
  ReservationSetId sid(++impl_->nextSetId);
  out.setId = sid;

  // Phase 1: assess every member.
  std::vector<FeasibilityReport> reps;
  reps.reserve(members.size());
  for (const auto& m : members) reps.push_back(impl_->assessOne(m));
  out.assessments = reps;
  for (const auto& rep : reps) {
    if (!rep.admissible()) {
      out.committed = false; out.rolledBack = true;
      out.errorMessage = "bundle member not admissible; nothing committed";
      return out;
    }
  }

  // Phase 2: acquire a provisional hold for every member.
  std::vector<ReservationHoldId> holdIds;
  holdIds.reserve(members.size());
  std::vector<ReservationRequest> memberReqs = members;
  for (std::size_t i = 0; i < memberReqs.size(); ++i) {
    const ReservationRequest& m = memberReqs[i];
    HoldSpec hs;
    hs.holdId = ReservationHoldId(++impl_->nextHoldId);
    hs.holdGeneration = ReservationHoldGeneration(1);
    hs.owner = m.owner; hs.ownerGeneration = m.ownerGeneration;
    hs.resourceClass = m.resourceClass; hs.unit = m.unit;
    hs.quantity = m.quantity; hs.window = m.window;
    hs.policyGeneration = m.policyGeneration;
    hs.commitAuthority = true;
    HoldResult hr = acquireHoldLocked(*impl_, hs);
    if (!hr.created) {
      for (const auto& hid : holdIds) releaseHoldLocked(*impl_, hid);
      out.committed = false; out.rolledBack = true;
      out.errorMessage = "bundle hold acquisition failed; all provisional holds released";
      return out;
    }
    holdIds.push_back(hs.holdId);
  }

  // Phase 3: commit every member atomically. Holds guarantee capacity, so this
  // succeeds unless an unexpected error occurs.
  ReservationSet set;
  set.id = sid; set.generation = ReservationSetGeneration(1);
  set.owner = members.empty() ? OwnerId(0) : members.front().owner;
  set.ownerGeneration = members.empty() ? OwnerGeneration(0) : members.front().ownerGeneration;
  set.allOrNothing = true;
  set.tx = SetTransactionState::Committing;

  std::vector<ReservationId> memberIds;
  for (std::size_t i = 0; i < memberReqs.size(); ++i) {
    Hold* hp = &impl_->holds[holdIds[i]];
    ReservationResult rr = commitOneLocked(*impl_, memberReqs[i], hp);
    if (!rr.committed) {
      // Roll back already-committed members and remaining holds.
      for (auto& mid : memberIds) {
        auto rit = impl_->reservations.find(mid);
        if (rit != impl_->reservations.end()) {
          impl_->removeContributionLocked(rit->second);
          rit->second.lifecycle = Lifecycle::Cancelled;
        }
      }
      for (std::size_t j = i; j < holdIds.size(); ++j) releaseHoldLocked(*impl_, holdIds[j]);
      out.committed = false; out.rolledBack = true;
      out.errorMessage = "bundle commit failed; rolled back";
      return out;
    }
    memberIds.push_back(rr.id);
  }

  set.committed = true; set.rolledBack = false; set.members = memberIds;
  set.tx = SetTransactionState::Committed;
  impl_->sets[sid] = set;

  out.committed = true;
  out.setGeneration = set.generation;
  out.memberIds = std::move(memberIds);
  return out;
}

}  // namespace reservation_fabric
