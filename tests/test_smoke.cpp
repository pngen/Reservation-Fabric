#include <reservation_fabric/runtime.hpp>
#include <iostream>
using namespace reservation_fabric;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL: " << #cond << " (line " << __LINE__ << ")\n"; ++failures; } } while (0)

int main() {
  ReservationFabric rf;
  // Publish a resource: 100 units of accelerator compute, available from t=0.
  ResourcePublication pub;
  pub.worker = WorkerId(1); pub.workerBoot = WorkerBootId(1);
  pub.id = ResourceId(1); pub.generation = ResourceGeneration(1);
  pub.resourceClass = ResourceClass::AcceleratorCompute;
  pub.unit = Unit::Count;
  pub.totalCapacity = Quantity::count(100);
  pub.pool = ResourcePoolId(1); pub.poolGeneration = ResourcePoolGeneration(1);
  pub.capabilities = CapabilityGeneration(1); pub.topology = TopologyGeneration(1);
  pub.contract = ResourceContractId(1); pub.contractGeneration = ResourceContractGeneration(1);
  pub.availableFrom = Instant(0);
  pub.lifetime = Duration::seconds(365 * 24 * 3600);
  auto reg = rf.publishResource(pub);
  CHECK(reg == ResourceRegisterResult::Accepted);

  // Reservation A: 60 units over [100,200).
  ReservationRequest req;
  req.requestId = ReservationRequestId(1);
  req.requestGeneration = ReservationRequestGeneration(1);
  req.resourceClass = ResourceClass::AcceleratorCompute;
  req.unit = Unit::Count;
  req.quantity = Quantity::count(60);
  req.window = Interval(Instant(100), Duration::seconds(100));
  req.activationMode = ActivationMode::FixedInterval;
  req.strength = ReservationStrength::Hard;
  req.owner = OwnerId(1); req.ownerGeneration = OwnerGeneration(1);
  req.policyGeneration = PolicyGeneration(1); req.priorityGeneration = PriorityGeneration(1);

  auto rep = rf.assess(req);
  CHECK(rep.outcome == Feasibility::Admissible);
  auto rc = rf.commit(req);
  CHECK(rc.committed);

  // Reservation B: 50 units over the same window -> must be rejected.
  ReservationRequest reqB = req;
  reqB.requestId = ReservationRequestId(2);
  reqB.quantity = Quantity::count(50);
  auto repB = rf.assess(reqB);
  CHECK(repB.outcome == Feasibility::RejectConflict || repB.outcome == Feasibility::RejectCapacity);
  auto rcB = rf.commit(reqB);
  CHECK(!rcB.committed);

  // Reservation C: 40 units -> should fit (60+40=100).
  ReservationRequest reqC = req;
  reqC.requestId = ReservationRequestId(3);
  reqC.quantity = Quantity::count(40);
  auto rcC = rf.commit(reqC);
  CHECK(rcC.committed);

  auto acc = rf.accounting(ResourceId(1));
  CHECK(acc.committed.value() == 100);
  CHECK(acc.headroom.value() == 0);

  std::cout << "smoke done; failures=" << failures << "\n";
  return failures == 0 ? 0 : 1;
}
