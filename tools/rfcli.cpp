#include <reservation_fabric/runtime.hpp>
#include <cstdio>
#include <string>
using namespace reservation_fabric;

// A small inspection tool. Exercises the public API and prints accountable state.
static void publish100(ReservationFabric& rf, ResourceId id) {
  ResourcePublication pub;
  pub.worker = WorkerId(1); pub.workerBoot = WorkerBootId(1);
  pub.id = id; pub.generation = ResourceGeneration(1);
  pub.resourceClass = ResourceClass::AcceleratorCompute; pub.unit = Unit::Count;
  pub.totalCapacity = Quantity::count(100);
  pub.availableFrom = Instant(0); pub.lifetime = Duration::seconds(365 * 24 * 3600);
  rf.publishResource(pub);
}

int main(int argc, char** argv) {
  std::string cmd = (argc > 1) ? argv[1] : "check";
  ReservationFabric rf;
  if (cmd == "inventory") { std::printf("%s", rf.dumpInventory().c_str()); return 0; }

  // Default self-check scenario.
  publish100(rf, ResourceId(1));
  Interval w(Instant(100), Duration::seconds(100));
  ReservationRequest req;
  req.requestId = ReservationRequestId(1); req.requestGeneration = ReservationRequestGeneration(1);
  req.resourceClass = ResourceClass::AcceleratorCompute; req.unit = Unit::Count;
  req.quantity = Quantity::count(60); req.window = w;
  req.activationMode = ActivationMode::FixedInterval; req.strength = ReservationStrength::Hard;
  req.owner = OwnerId(1); req.ownerGeneration = OwnerGeneration(1);
  req.policyGeneration = PolicyGeneration(1); req.priorityGeneration = PriorityGeneration(1);
  req.targetResource = ResourceId(1);

  auto rc = rf.commit(req);
  if (rc.committed) {
    ActivationEvidence ev; ev.worker = WorkerId(1); ev.workerBoot = WorkerBootId(1); ev.activationGeneration = ActivationGeneration(1); ev.at = Instant(150);
    bool active = rf.activate(rc.id, rc.generation, ev).activated;
    ConsumeEvidence ce; ce.worker = WorkerId(1); ce.workerBoot = WorkerBootId(1); ce.consumptionGeneration = ConsumptionGeneration(1); ce.at = Instant(200);
    bool consumed = rf.consume(rc.id, rc.generation, Quantity::count(25), ce).consumed;
    auto acc = rf.accounting(ResourceId(1));
    std::printf("reservation %llu gen %llu active=%d consumed=%d committed=%lld headroom=%lld\n",
                (unsigned long long)rc.id.value(), (unsigned long long)rc.generation.value(),
                active ? 1 : 0, consumed ? 1 : 0, (long long)acc.committed.value(), (long long)acc.headroom.value());
  } else {
    std::printf("commit rejected: %s\n", rc.errorMessage.c_str());
  }
  std::printf("%s", rf.dumpInventory().c_str());
  return 0;
}
