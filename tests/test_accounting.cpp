#include "test_common.hpp"
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL: " << #cond << " (line " << __LINE__ << ")\n"; ++failures; } } while (0)

int main() {
  ReservationFabric rf;
  rf.publishResource(makeResource(ResourceId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(100)));
  Interval w(Instant(100), Duration::seconds(100));

  auto ra = rf.commit(makeReq(ReservationRequestId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(60), w));
  CHECK(ra.committed);
  auto rb = rf.commit(makeReq(ReservationRequestId(2), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(40), w));
  CHECK(rb.committed);

  auto acc = rf.accounting(ResourceId(1));
  CHECK(acc.committed.value() == 100);
  CHECK(acc.headroom.value() == 0);
  CHECK(acc.consistent);

  // Capacity never negative and no overcommit through the envelope.
  CHECK(rf.headroomOverWindow(ResourceId(1), w, true).value() == 0);
  CHECK(rf.headroomAt(ResourceId(1), Instant(150), true).value() == 0);

  // A third reservation of 1 unit must be rejected (no headroom).
  auto rc = rf.commit(makeReq(ReservationRequestId(3), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(1), w));
  CHECK(!rc.committed);

  // Release A -> computed headroom returns exactly to 60.
  auto rel = rf.release(ra.id, ra.generation, makeReleaseEvidence());
  CHECK(rel.released);
  auto acc2 = rf.accounting(ResourceId(1));
  CHECK(acc2.committed.value() == 40);
  CHECK(acc2.headroom.value() == 60);
  CHECK(acc2.consistent);

  // Released reservation no longer reduces capacity.
  auto rd = rf.commit(makeReq(ReservationRequestId(4), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(55), w));
  CHECK(rd.committed);   // 40 + 55 = 95 <= 100; A is released so it doesn't block.

  return failures == 0 ? 0 : 1;
}
