#include "impl.hpp"
#include <algorithm>
#include <sstream>

namespace reservation_fabric {

namespace {
// A reservation is counted once, in exactly one lifecycle category.
bool isRetiredLifecycle(Lifecycle l) {
  return l == Lifecycle::Released || l == Lifecycle::Expired || l == Lifecycle::Cancelled || l == Lifecycle::Superseded || l == Lifecycle::Retired;
}
}  // namespace

const Reservation* ReservationFabric::reservation(ReservationId id) const {
  std::shared_lock<std::shared_mutex> l(impl_->lock);
  auto it = impl_->reservations.find(id);
  return it == impl_->reservations.end() ? nullptr : &it->second;
}
const Reservation* ReservationFabric::reservation(ReservationId id, ReservationGeneration generation) const {
  std::shared_lock<std::shared_mutex> l(impl_->lock);
  auto it = impl_->reservations.find(id);
  if (it != impl_->reservations.end() && it->second.generation == generation) return &it->second;
  auto h = impl_->history.find(id);
  if (h != impl_->history.end()) {
    for (const auto& r : h->second) if (r.generation == generation) return &r;
  }
  return nullptr;
}
std::vector<ReservationId> ReservationFabric::reservationIds() const {
  std::shared_lock<std::shared_mutex> l(impl_->lock);
  std::vector<ReservationId> ids; ids.reserve(impl_->reservations.size());
  for (const auto& [id, r] : impl_->reservations) ids.push_back(id);
  return ids;
}
std::vector<ReservationId> ReservationFabric::reservationsForResource(ResourceId id) const {
  std::shared_lock<std::shared_mutex> l(impl_->lock);
  std::vector<ReservationId> ids;
  for (const auto& [rid, r] : impl_->reservations) if (r.resource == id) ids.push_back(rid);
  return ids;
}
std::vector<ReservationId> ReservationFabric::reservationsForOwner(OwnerId owner) const {
  std::shared_lock<std::shared_mutex> l(impl_->lock);
  std::vector<ReservationId> ids;
  for (const auto& [rid, r] : impl_->reservations) if (r.owner == owner) ids.push_back(rid);
  return ids;
}
std::size_t ReservationFabric::currentReservationCount() const {
  std::shared_lock<std::shared_mutex> l(impl_->lock);
  return impl_->reservations.size();
}
std::size_t ReservationFabric::resourceCount() const {
  std::shared_lock<std::shared_mutex> l(impl_->lock);
  return impl_->resources.size();
}
std::size_t ReservationFabric::holdCount() const {
  std::shared_lock<std::shared_mutex> l(impl_->lock);
  std::size_t n = 0;
  for (const auto& [id, h] : impl_->holds) if (h.state == HoldState::Acquired) ++n;
  return n;
}
std::size_t ReservationFabric::setCount() const {
  std::shared_lock<std::shared_mutex> l(impl_->lock);
  return impl_->sets.size();
}

CapacityAccounting ReservationFabric::accounting(ResourceId id) const {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  CapacityAccounting a;
  const Resource* res = impl_->findResourceLocked(id);
  if (!res) return a;
  a.resource = id; a.generation = res->generation; a.resourceClass = res->resourceClass;
  a.unit = res->unit; a.total = res->totalCapacity;

  std::int64_t committed = 0, softCommitted = 0, held = 0, active = 0, consumed = 0, released = 0, expired = 0, cancelled = 0;
  for (const auto& [rid, r] : impl_->reservations) {
    if (r.resource != id) continue;
    if (r.contributesEnvelope) {
      if (detail::isHard(r.strength)) committed += r.quantity.value();
      else softCommitted += r.quantity.value();
    }
    switch (r.lifecycle) {
      case Lifecycle::Active: case Lifecycle::PartiallyConsumed: case Lifecycle::Consumed:
        active += r.quantity.value(); consumed += r.consumed.value(); break;
      case Lifecycle::Released: released += r.quantity.value(); break;
      case Lifecycle::Expired: expired += r.quantity.value(); break;
      case Lifecycle::Cancelled: cancelled += r.quantity.value(); break;
      default: break;
    }
  }
  for (const auto& [hid, h] : impl_->holds) {
    if (h.appliedToEnvelope && h.resource == id) held += h.spec.quantity.value();
  }

  CapacityEnvelope* env = impl_->envelopeLocked(id);
  std::int64_t headroom = 0, peak = 0;
  if (env) {
    Interval whole(env->origin(), env->horizon());
    headroom = env->minHardHeadroom(whole);
    peak = env->peakCombinedHard(whole);
  }

  a.committed = Quantity(committed, a.unit);
  a.softCommitted = Quantity(softCommitted, a.unit);
  a.held = Quantity(held, a.unit);
  a.active = Quantity(active, a.unit);
  a.consumed = Quantity(consumed, a.unit);
  a.released = Quantity(released, a.unit);
  a.expired = Quantity(expired, a.unit);
  a.cancelled = Quantity(cancelled, a.unit);
  a.headroom = Quantity(headroom < 0 ? 0 : headroom, a.unit);
  a.peakCommittedFuture = Quantity(peak, a.unit);
  a.consistent = (committed + softCommitted + held) <= a.total.value() && headroom >= 0;
  return a;
}

std::string ReservationFabric::dumpInventory() const {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  std::ostringstream os;
  os << "epoch=" << impl_->epoch.value() << " authority=" << impl_->authority.value() << "\n";
  os << "resources=" << impl_->resources.size() << " reservations=" << impl_->reservations.size()
     << " holds=" << impl_->holds.size() << " sets=" << impl_->sets.size() << "\n";
  for (const auto& [id, res] : impl_->resources) {
    os << "  resource " << id.value() << " gen=" << res.generation.value() << " class=" << to_string(res.resourceClass)
       << " capacity=" << res.totalCapacity.value() << " worker=" << res.owner.value() << " boot=" << res.ownerBoot.value()
       << " available=" << (res.available ? 1 : 0) << "\n";
  }
  for (const auto& [rid, r] : impl_->reservations) {
    os << "  reservation " << rid.value() << " gen=" << r.generation.value() << " lifecycle=" << to_string(r.lifecycle)
       << " resource=" << r.resource.value() << " qty=" << r.quantity.value()
       << " window=[" << r.window.start().nanoseconds() << "," << r.window.end().nanoseconds() << ")"
       << " owner=" << r.owner.value() << " contributes=" << (r.contributesEnvelope ? 1 : 0) << "\n";
  }
  return os.str();
}

}  // namespace reservation_fabric
