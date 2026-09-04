#include "test_common.hpp"
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL: " << #cond << " (line " << __LINE__ << ")\n"; ++failures; } } while (0)
#define CHECK_THROW(expr) do { bool threw = false; try { (void)(expr); } catch (const FabricError&) { threw = true; } catch (...) { threw = true; } if (!threw) { std::cerr << "FAIL(no-throw): " << #expr << " (line " << __LINE__ << ")\n"; ++failures; } } while (0)

int main() {
  ReservationFabric rf;
  rf.publishResource(makeResource(ResourceId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(100)));
  // The publishing worker is WorkerId(1) with boot 1.
  Interval w(Instant(100), Duration::seconds(100));

  auto rc = rf.commit(makeReq(ReservationRequestId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(60), w));
  CHECK(rc.committed);
  ReservationId rid = rc.id;
  ReservationGeneration gen = rc.generation;
  const Reservation* r = rf.reservation(rid);
  CHECK(r && r->lifecycle == Lifecycle::Committed);

  // Illegal transitions are guarded by the state machine.
  CHECK(!can_transition(Lifecycle::Requested, Lifecycle::Active));
  CHECK(!can_transition(Lifecycle::Held, Lifecycle::Consumed));
  CHECK(!can_transition(Lifecycle::Released, Lifecycle::Active));
  CHECK(!can_transition(Lifecycle::Expired, Lifecycle::Active));
  CHECK(can_transition(Lifecycle::Committed, Lifecycle::PendingActivation));

  // Stale worker boot cannot activate a reservation that is not yet active.
  auto actStale = rf.activate(rid, gen, makeEvidence(WorkerId(1), WorkerBootId(99)));
  CHECK(!actStale.activated);

  // Activate with fresh evidence.
  auto act = rf.activate(rid, gen, makeEvidence());
  CHECK(act.activated);
  CHECK(rf.reservation(rid)->lifecycle == Lifecycle::Active);

  // Stale reservation generation rejected.
  auto actBadGen = rf.activate(rid, ReservationGeneration(999), makeEvidence());
  CHECK(!actBadGen.activated);

  // Consume.
  auto c1 = rf.consume(rid, gen, Quantity::count(25), makeConsumeEvidence());
  CHECK(c1.consumed);
  CHECK(rf.reservation(rid)->lifecycle == Lifecycle::PartiallyConsumed);
  auto c2 = rf.consume(rid, gen, Quantity::count(35), makeConsumeEvidence(WorkerId(1), WorkerBootId(1), ConsumptionGeneration(2)));
  CHECK(c2.consumed);
  CHECK(rf.reservation(rid)->lifecycle == Lifecycle::Consumed);
  auto c3 = rf.consume(rid, gen, Quantity::count(5), makeConsumeEvidence(WorkerId(1), WorkerBootId(1), ConsumptionGeneration(3)));
  CHECK(!c3.consumed);   // over-consumption rejected

  // Release.
  auto rel = rf.release(rid, gen, makeReleaseEvidence());
  CHECK(rel.released);
  CHECK(rf.reservation(rid)->lifecycle == Lifecycle::Released);
  auto rel2 = rf.release(rid, gen, makeReleaseEvidence(WorkerId(1), WorkerBootId(1), Instant(200), ReleaseGeneration(2)));
  CHECK(rel2.released && rel2.idempotent);

  // Expiration: window [100, 100+10s). Expire when now has passed the end.
  auto rcE = rf.commit(makeReq(ReservationRequestId(2), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(30), Interval(Instant(100), Duration::seconds(10))));
  CHECK(rcE.committed);
  CHECK(rf.expireDue(Instant(60000000000LL)) >= 1);   // 60s > end(10s)
  CHECK(rf.reservation(rcE.id)->lifecycle == Lifecycle::Expired);
  auto actE = rf.activate(rcE.id, rcE.generation, makeEvidence(WorkerId(1), WorkerBootId(1), Instant(120)));
  CHECK(!actE.activated);   // expired cannot activate

  // Cancellation.
  auto rcC = rf.commit(makeReq(ReservationRequestId(3), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(20), w));
  CHECK(rcC.committed);
  rf.cancel(rcC.id, rcC.generation);
  CHECK(rf.reservation(rcC.id)->lifecycle == Lifecycle::Cancelled);
  CHECK_THROW(rf.cancel(rcC.id, ReservationGeneration(999)));   // stale cancel

  // Resize (modify): 60 -> 40 releases exactly the delta; new gen is current.
  auto rcR = rf.commit(makeReq(ReservationRequestId(4), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(60), Interval(Instant(200), Duration::seconds(100))));
  CHECK(rcR.committed);
  auto m1 = rf.resize(rcR.id, rcR.generation, Quantity::count(40), PolicyGeneration(1));
  CHECK(m1.applied && m1.releasedDelta == 20);
  CHECK(rf.reservation(rcR.id)->generation == m1.newGeneration);
  CHECK(rf.reservation(rcR.id)->quantity.value() == 40);
  auto m2 = rf.resize(rcR.id, rcR.generation, Quantity::count(30), PolicyGeneration(1));
  CHECK(!m2.applied);   // stale generation

  // Renew creates a fresh generation with a new window.
  auto m3 = rf.renew(rcR.id, m1.newGeneration, Interval(Instant(500), Duration::seconds(100)), PolicyGeneration(1));
  CHECK(m3.applied);
  CHECK(rf.reservation(rcR.id)->window.start().nanoseconds() == 500);

  // Resize growth requires fresh admission: 40 -> 90 on a 100-capacity resource with
  // no other commitment is admissible; 40 -> 101 is not.
  auto rcG = rf.commit(makeReq(ReservationRequestId(5), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(10), Interval(Instant(700), Duration::seconds(100))));
  CHECK(rcG.committed);
  auto growOk = rf.resize(rcG.id, rcG.generation, Quantity::count(40), PolicyGeneration(1));
  CHECK(growOk.applied && growOk.acquiredDelta == 30);
  auto growBad = rf.resize(rcG.id, growOk.newGeneration, Quantity::count(200), PolicyGeneration(1));
  CHECK(!growBad.applied);   // exceeds 100

  std::cout << "lifecycle tests done; failures=" << failures << "\n";
  return failures == 0 ? 0 : 1;
}
