#include "test_common.hpp"
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL: " << #cond << " (line " << __LINE__ << ")\n"; ++failures; } } while (0)
#define CHECK_THROW(expr) do { bool threw = false; try { (void)(expr); } catch (...) { threw = true; } if (!threw) { std::cerr << "FAIL(no-throw): " << #expr << " (line " << __LINE__ << ")\n"; ++failures; } } while (0)

int main() {
  ReservationFabric rf;
  // Publishing worker is WorkerId(1) with a fresh boot id 5.
  rf.publishResource(makeResource(ResourceId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(100), WorkerId(1), ResourceGeneration(1), WorkerBootId(5)));
  Interval w(Instant(100), Duration::seconds(100));

  // A valid but older boot id is a stale publication.
  std::string err;
  auto stalePub = makeResource(ResourceId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(100), WorkerId(1), ResourceGeneration(1), WorkerBootId(3));
  CHECK(rf.publishResource(stalePub, &err) == ResourceRegisterResult::RejectedStale);

  // A duplicate same-generation publication is rejected as duplicate.
  auto dupPub = makeResource(ResourceId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(100), WorkerId(1), ResourceGeneration(1), WorkerBootId(5));
  CHECK(rf.publishResource(dupPub, &err) == ResourceRegisterResult::RejectedDuplicate);

  // Stale coordinator epoch rejected.
  rf.setCoordinatorEpoch(CoordinatorEpoch(2));
  CHECK_THROW(rf.setCoordinatorEpoch(CoordinatorEpoch(1)));

  auto rc = rf.commit(makeReq(ReservationRequestId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(60), w));
  CHECK(rc.committed);

  // Stale reservation generation on release.
  auto rel = rf.release(rc.id, ReservationGeneration(999), makeReleaseEvidence(WorkerId(1), WorkerBootId(5)));
  CHECK(!rel.released);

  // Activate with current (fresh) evidence: WorkerId(1), boot 5.
  CHECK(rf.activate(rc.id, rc.generation, makeEvidence(WorkerId(1), WorkerBootId(5))).activated);

  // Resource generation advance invalidates the reservation (RevalidationRequired).
  rf.advanceResourceGeneration(ResourceId(1), ResourceGeneration(2), ResourceCondition::Available);
  CHECK(rf.reservation(rc.id)->lifecycle == Lifecycle::RevalidationRequired);

  // Getting revalidated with a fresh worker boot 6 returns it to PendingActivation.
  auto fresh = makeResource(ResourceId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(100), WorkerId(1), ResourceGeneration(3), WorkerBootId(6));
  rf.revalidateResource(ResourceId(1), fresh);
  CHECK(rf.reservation(rc.id)->lifecycle == Lifecycle::PendingActivation);

  // Stale boot (5) cannot publish activation; fresh boot (6) can.
  auto actStale = rf.activate(rc.id, rc.generation, makeEvidence(WorkerId(1), WorkerBootId(5)));
  CHECK(!actStale.activated);
  auto actFresh = rf.activate(rc.id, rc.generation, makeEvidence(WorkerId(1), WorkerBootId(6)));
  CHECK(actFresh.activated);

  // A request requiring a newer resource generation than current must be rejected.
  ReservationRequest req = makeReq(ReservationRequestId(2), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(1), w);
  req.minResourceGeneration = ResourceGeneration(99);
  auto st = rf.assess(req);
  CHECK(st.outcome != Feasibility::Admissible);

  std::cout << "stale tests done; failures=" << failures << "\n";
  return failures == 0 ? 0 : 1;
}
