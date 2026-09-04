#include <reservation_fabric/runtime.hpp>
#include <cstdio>
using namespace reservation_fabric;

// A downstream consumer exercising real reservation API behavior against the
// installed package via find_package(ReservationFabric CONFIG REQUIRED).
int main() {
  ReservationFabric rf;
  ResourcePublication pub;
  pub.worker = WorkerId(1); pub.workerBoot = WorkerBootId(1);
  pub.id = ResourceId(1); pub.generation = ResourceGeneration(1);
  pub.resourceClass = ResourceClass::AcceleratorCompute; pub.unit = Unit::Count;
  pub.totalCapacity = Quantity::count(100);
  pub.availableFrom = Instant(0); pub.lifetime = Duration::seconds(365*24*3600);
  if (rf.publishResource(pub) != ResourceRegisterResult::Accepted) { std::fprintf(stderr, "publish failed\n"); return 1; }

  ReservationRequest req;
  req.requestId = ReservationRequestId(1); req.requestGeneration = ReservationRequestGeneration(1);
  req.resourceClass = ResourceClass::AcceleratorCompute; req.unit = Unit::Count;
  req.quantity = Quantity::count(60); req.window = Interval(Instant(100), Duration::seconds(100));
  req.activationMode = ActivationMode::FixedInterval; req.strength = ReservationStrength::Hard;
  req.owner = OwnerId(1); req.ownerGeneration = OwnerGeneration(1);
  req.policyGeneration = PolicyGeneration(1); req.priorityGeneration = PriorityGeneration(1);
  req.targetResource = ResourceId(1);

  auto rc = rf.commit(req);
  if (!rc.committed) { std::fprintf(stderr, "commit failed\n"); return 1; }
  ActivationEvidence ev; ev.worker = WorkerId(1); ev.workerBoot = WorkerBootId(1); ev.activationGeneration = ActivationGeneration(1); ev.at = Instant(150);
  if (!rf.activate(rc.id, rc.generation, ev).activated) { std::fprintf(stderr, "activate failed\n"); return 1; }
  auto acc = rf.accounting(ResourceId(1));
  if (acc.committed.value() != 60 || acc.headroom.value() != 40) { std::fprintf(stderr, "accounting mismatch\n"); return 1; }
  std::printf("downstream consumer OK: committed=%lld headroom=%lld\n", (long long)acc.committed.value(), (long long)acc.headroom.value());
  return 0;
}
