#include <reservation_fabric/runtime.hpp>
#include <cstdio>
using namespace reservation_fabric;

static void publish(ReservationFabric& rf, ResourceId id, std::int64_t cap) {
  ResourcePublication pub;
  pub.worker = WorkerId(1); pub.workerBoot = WorkerBootId(1);
  pub.id = id; pub.generation = ResourceGeneration(1);
  pub.resourceClass = ResourceClass::AcceleratorCompute; pub.unit = Unit::Count;
  pub.totalCapacity = Quantity::count(cap);
  pub.availableFrom = Instant(0); pub.lifetime = Duration::seconds(365 * 24 * 3600);
  rf.publishResource(pub);
}

int main() {
  ReservationFabric rf;
  publish(rf, ResourceId(1), 100);
  publish(rf, ResourceId(2), 100*1000);
  publish(rf, ResourceId(3), 100*1000);
  publish(rf, ResourceId(4), 1000*1000);
  Interval w(Instant(100), Duration::seconds(100));

  auto req = [](ReservationRequestId rid, ResourceClass cls, Unit u, std::int64_t q, Interval win) {
    ReservationRequest r;
    r.requestId = rid; r.requestGeneration = ReservationRequestGeneration(1);
    r.resourceClass = cls; r.unit = u; r.quantity = Quantity::count(q); r.window = win;
    r.activationMode = ActivationMode::FixedInterval; r.strength = ReservationStrength::Hard;
    r.owner = OwnerId(1); r.ownerGeneration = OwnerGeneration(1);
    r.policyGeneration = PolicyGeneration(1); r.priorityGeneration = PriorityGeneration(1);
    return r;
  };

  // Overlapping conflict.
  auto a = rf.commit(req(ReservationRequestId(1), ResourceClass::AcceleratorCompute, Unit::Count, 60, w));
  // Composite all-or-nothing bundle.
  std::vector<ReservationRequest> bundle;
  bundle.push_back(req(ReservationRequestId(2), ResourceClass::AcceleratorCompute, Unit::Count, 50, w));
  bundle.push_back(req(ReservationRequestId(3), ResourceClass::AcceleratorMemory, Unit::Bytes, 10*1024*1024, w));
  bundle.push_back(req(ReservationRequestId(4), ResourceClass::PinnedHostMemory, Unit::Bytes, 1024*1024, w));
  bundle.push_back(req(ReservationRequestId(5), ResourceClass::NetworkBandwidth, Unit::BytesPerSecond, 5*1000*1000, w));
  auto cb = rf.commitBundle(bundle);
  std::printf("basic example: reservationA committed=%d, compositeBundle committed=%d members=%zu\n",
              a.committed ? 1 : 0, cb.committed ? 1 : 0, cb.memberIds.size());
  return 0;
}