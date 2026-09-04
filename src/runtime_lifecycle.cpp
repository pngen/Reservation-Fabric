#include "impl.hpp"
#include <algorithm>

namespace reservation_fabric {
namespace {

bool workerEvidenceFreshLocked(ReservationFabric::Impl& d, const Reservation& r, const ActivationEvidence& e) {
  const Resource* res = d.findResourceLocked(r.resource);
  if (!res) return false;
  return res->available && e.worker == res->owner && e.workerBoot == res->ownerBoot;
}
bool workerEvidenceFreshConsumeLocked(ReservationFabric::Impl& d, const Reservation& r, const ConsumeEvidence& e) {
  const Resource* res = d.findResourceLocked(r.resource);
  if (!res) return false;
  return res->available && e.worker == res->owner && e.workerBoot == res->ownerBoot;
}
bool workerEvidenceFreshReleaseLocked(ReservationFabric::Impl& d, const Reservation& r, const ReleaseEvidence& e) {
  const Resource* res = d.findResourceLocked(r.resource);
  if (!res) return false;
  return res->available && e.worker == res->owner && e.workerBoot == res->ownerBoot;
}

}  // namespace

ActivationResult ReservationFabric::activate(ReservationId id, ReservationGeneration generation, const ActivationEvidence& evidence) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  ActivationResult out; out.id = id; out.generation = generation;
  Reservation* r = impl_->reservationLocked(id);
  if (!r || r->generation != generation) { out.errorMessage = "stale reservation generation"; return out; }
  if (r->lifecycle == Lifecycle::Active) { out.activated = true; return out; }

  const Resource* res = impl_->findResourceLocked(r->resource);
  if (!res) { out.errorMessage = "resource missing"; return out; }
  // Resource evidence current?
  bool resourceAdvanced = (r->boundResourceGeneration.has_value() && res->generation > *r->boundResourceGeneration);
  if (!res->available || resourceAdvanced) {
    r->lifecycle = Lifecycle::RevalidationRequired;
    out.revalidationRequired = true;
    return out;
  }
  // Fresh worker boot id required to publish activation evidence.
  if (!(evidence.worker == res->owner && evidence.workerBoot == res->ownerBoot)) {
    out.errorMessage = "stale worker boot id cannot publish activation";
    return out;
  }
  if (!r->window.contains(evidence.at)) { out.errorMessage = "activation instant outside reservation window"; return out; }
  if (r->activationGeneration.has_value() && evidence.activationGeneration < *r->activationGeneration) {
    out.errorMessage = "stale activation generation"; return out;
  }
  if (!(r->lifecycle == Lifecycle::Committed || r->lifecycle == Lifecycle::PendingActivation ||
        r->lifecycle == Lifecycle::Activating)) {
    out.errorMessage = "reservation cannot activate from its current lifecycle";
    return out;
  }
  r->lifecycle = Lifecycle::Active;
  r->activatedAt = evidence.at;
  r->activationGeneration = evidence.activationGeneration;
  r->activated = r->quantity;
  out.activated = true;
  return out;
}

ConsumeResult ReservationFabric::consume(ReservationId id, ReservationGeneration generation, Quantity amount, const ConsumeEvidence& evidence) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  ConsumeResult out; out.id = id; out.generation = generation;
  Reservation* r = impl_->reservationLocked(id);
  if (!r || r->generation != generation) { out.errorMessage = "stale reservation generation"; return out; }
  if (!(r->lifecycle == Lifecycle::Active || r->lifecycle == Lifecycle::PartiallyConsumed || r->lifecycle == Lifecycle::Consumed)) {
    out.errorMessage = "consumption requires an active reservation";
    return out;
  }
  if (amount.value() <= 0 || amount.unit() != r->unit) { out.errorMessage = "invalid consumption amount"; return out; }
  if (!workerEvidenceFreshConsumeLocked(*impl_, *r, evidence)) { out.errorMessage = "stale worker boot id cannot publish consumption"; return out; }
  if (r->consumptionGeneration.has_value() && evidence.consumptionGeneration < *r->consumptionGeneration) {
    out.errorMessage = "stale consumption generation"; return out;
  }
  if (r->consumptionGeneration.has_value() && evidence.consumptionGeneration == *r->consumptionGeneration) {
    out.idempotent = true; out.consumedQuantity = r->consumed; out.remaining = remaining_quantity(*r); return out;  // duplicate
  }
  if (amount.value() > r->quantity.value() - r->consumed.value()) {
    out.errorMessage = "consumption exceeds remaining reservation quantity";
    return out;
  }
  r->consumed = Quantity(r->consumed.value() + amount.value(), r->unit);
  r->consumptionGeneration = evidence.consumptionGeneration;
  if (r->consumed == r->quantity) r->lifecycle = Lifecycle::Consumed;
  else r->lifecycle = Lifecycle::PartiallyConsumed;
  out.consumed = true;
  out.consumedQuantity = r->consumed;
  out.remaining = remaining_quantity(*r);
  return out;
}

ReleaseResult ReservationFabric::release(ReservationId id, ReservationGeneration generation, const ReleaseEvidence& evidence) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  ReleaseResult out; out.id = id; out.generation = generation;
  Reservation* r = impl_->reservationLocked(id);
  if (!r || r->generation != generation) { out.errorMessage = "stale reservation generation"; return out; }
  if (r->lifecycle == Lifecycle::Released) { out.released = true; out.idempotent = true; return out; }
  if (r->lifecycle == Lifecycle::Expired || r->lifecycle == Lifecycle::Cancelled) { out.released = true; out.idempotent = true; return out; }
  if (!workerEvidenceFreshReleaseLocked(*impl_, *r, evidence)) { out.errorMessage = "stale worker boot id cannot publish release evidence"; return out; }
  if (r->releaseGeneration.has_value() && evidence.releaseGeneration < *r->releaseGeneration) {
    out.errorMessage = "stale release generation"; return out;
  }
  impl_->removeContributionLocked(*r);
  r->lifecycle = Lifecycle::Released;
  r->releasedAt = evidence.at;
  r->releaseGeneration = evidence.releaseGeneration;
  out.released = true;
  return out;
}

void ReservationFabric::cancel(ReservationId id, ReservationGeneration generation) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  Reservation* r = impl_->reservationLocked(id);
  if (!r || r->generation != generation) throw_error(ErrorCode::StaleGeneration, "stale reservation generation");
  if (r->lifecycle == Lifecycle::Cancelled || r->lifecycle == Lifecycle::Released || r->lifecycle == Lifecycle::Expired) return;
  impl_->removeContributionLocked(*r);
  r->lifecycle = Lifecycle::Cancelled;
}

std::size_t ReservationFabric::expireDue(Instant now) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  std::size_t count = 0;
  for (auto& [rid, r] : impl_->reservations) {
    if (!lifecycle_holds_capacity(r.lifecycle)) continue;
    if (r.lifecycle == Lifecycle::Released || r.lifecycle == Lifecycle::Expired ||
        r.lifecycle == Lifecycle::Cancelled || r.lifecycle == Lifecycle::Superseded) continue;
    if (r.window.end() < now || (r.activationMode == ActivationMode::ImmediateAfterCommit && false)) {
      impl_->removeContributionLocked(r);
      r.lifecycle = Lifecycle::Expired;
      r.releasedAt = now;
      ++count;
    }
  }
  return count;
}

ModifyResult ReservationFabric::resize(ReservationId id, ReservationGeneration generation, Quantity newQuantity, PolicyGeneration policyGeneration) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  ModifyResult out; out.id = id; out.oldGeneration = generation;
  Reservation* r = impl_->reservationLocked(id);
  if (!r || r->generation != generation) { out.errorMessage = "stale reservation generation"; return out; }
  if (newQuantity.value() <= 0 || newQuantity.unit() != r->unit) { out.errorMessage = "invalid new quantity"; return out; }
  if (policyGeneration < r->policyGeneration) { out.errorMessage = "stale policy generation"; return out; }

  const std::int64_t oldV = r->quantity.value();
  const std::int64_t newV = newQuantity.value();

  impl_->removeContributionLocked(*r);
  std::int64_t headroom = 0;
  if (CapacityEnvelope* e = impl_->envelopeLocked(r->resource)) {
    headroom = detail::isHard(r->strength) ? e->minHardHeadroom(r->window) : e->minSoftHeadroom(r->window);
  }
  if (headroom < newV) {
    impl_->addContributionLocked(*r);
    out.errorMessage = "resize rejected: proposed quantity not admissible";
    return out;
  }

  Reservation old = *r;
  old.contributesEnvelope = false;
  impl_->history[id].push_back(old);
  if (impl_->history[id].size() > impl_->config.maxHistoryPerReservation) impl_->history[id].pop_front();

  Reservation nr = *r;
  nr.generation = impl_->currentGen[id].next();
  nr.quantity = newQuantity;
  nr.policyGeneration = policyGeneration;
  nr.lifecycle = Lifecycle::Committed;
  nr.activationGeneration.reset();
  nr.consumed = Quantity::zero(nr.unit);
  nr.activated = Quantity::zero(nr.unit);
  nr.contributesEnvelope = true;
  impl_->addContributionLocked(nr);
  impl_->currentGen[id] = nr.generation;
  impl_->reservations[id] = std::move(nr);

  if (newV > oldV) out.acquiredDelta = newV - oldV;
  else out.releasedDelta = oldV - newV;
  out.newGeneration = impl_->currentGen[id];
  out.applied = true;
  return out;
}

ModifyResult ReservationFabric::renew(ReservationId id, ReservationGeneration generation, const Interval& newWindow, PolicyGeneration policyGeneration) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  ModifyResult out; out.id = id; out.oldGeneration = generation;
  Reservation* r = impl_->reservationLocked(id);
  if (!r || r->generation != generation) { out.errorMessage = "stale reservation generation"; return out; }
  if (policyGeneration < r->policyGeneration) { out.errorMessage = "stale policy generation"; return out; }

  impl_->removeContributionLocked(*r);
  std::int64_t headroom = 0;
  if (CapacityEnvelope* e = impl_->envelopeLocked(r->resource)) {
    headroom = detail::isHard(r->strength) ? e->minHardHeadroom(newWindow) : e->minSoftHeadroom(newWindow);
  }
  if (headroom < r->quantity.value()) {
    impl_->addContributionLocked(*r);
    out.errorMessage = "renew rejected: new window not admissible";
    return out;
  }

  Reservation old = *r;
  old.contributesEnvelope = false;
  impl_->history[id].push_back(old);
  if (impl_->history[id].size() > impl_->config.maxHistoryPerReservation) impl_->history[id].pop_front();

  Reservation nr = *r;
  nr.generation = impl_->currentGen[id].next();
  nr.window = newWindow;
  nr.policyGeneration = policyGeneration;
  nr.lifecycle = Lifecycle::Committed;
  nr.activationGeneration.reset();
  nr.contributesEnvelope = true;
  impl_->addContributionLocked(nr);
  impl_->currentGen[id] = nr.generation;
  impl_->reservations[id] = std::move(nr);

  out.newGeneration = impl_->currentGen[id];
  out.applied = true;
  return out;
}

ModifyResult ReservationFabric::transfer(ReservationId id, ReservationGeneration generation, OwnerId newOwner, OwnerGeneration newOwnerGeneration, const ActivationEvidence& evidence) {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  ModifyResult out; out.id = id; out.oldGeneration = generation;
  Reservation* r = impl_->reservationLocked(id);
  if (!r || r->generation != generation) { out.errorMessage = "stale reservation generation"; return out; }
  if (r->transfer != TransferPermission::TransferAllowed) { out.errorMessage = "transfer not permitted by reservation policy"; return out; }
  if (!(newOwner.valid() && newOwnerGeneration.valid())) { out.errorMessage = "invalid destination authority"; return out; }
  if (!workerEvidenceFreshLocked(*impl_, *r, evidence)) { out.errorMessage = "stale transfer authority"; return out; }

  Reservation old = *r;
  old.contributesEnvelope = false;
  impl_->history[id].push_back(old);
  if (impl_->history[id].size() > impl_->config.maxHistoryPerReservation) impl_->history[id].pop_front();

  Reservation nr = *r;
  nr.generation = impl_->currentGen[id].next();
  nr.owner = newOwner;
  nr.ownerGeneration = newOwnerGeneration;
  nr.transferGeneration = TransferGeneration(1);
  nr.lifecycle = Lifecycle::Committed;
  nr.activationGeneration.reset();
  impl_->currentGen[id] = nr.generation;
  impl_->reservations[id] = std::move(nr);

  out.newGeneration = impl_->currentGen[id];
  out.applied = true;
  return out;
}

}  // namespace reservation_fabric
