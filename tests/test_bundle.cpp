#include "test_common.hpp"
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL: " << #cond << " (line " << __LINE__ << ")\n"; ++failures; } } while (0)

int main() {
  ReservationFabric rf;
  rf.publishResource(makeResource(ResourceId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(100)));
  rf.publishResource(makeResource(ResourceId(2), ResourceClass::AcceleratorMemory, Unit::Bytes, Quantity::bytes(10000000000LL)));
  rf.publishResource(makeResource(ResourceId(3), ResourceClass::PinnedHostMemory, Unit::Bytes, Quantity::bytes(1000000000LL)));
  rf.publishResource(makeResource(ResourceId(4), ResourceClass::NetworkBandwidth, Unit::BytesPerSecond, Quantity::bytesPerSecond(10000000LL)));
  Interval w(Instant(100), Duration::seconds(100));

  // Success bundle: one member per resource, all-or-nothing.
  std::vector<ReservationRequest> bundle;
  bundle.push_back(makeReq(ReservationRequestId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(50), w));
  bundle.push_back(makeReq(ReservationRequestId(2), ResourceClass::AcceleratorMemory, Unit::Bytes, Quantity::bytes(1000000000LL), w));
  bundle.push_back(makeReq(ReservationRequestId(3), ResourceClass::PinnedHostMemory, Unit::Bytes, Quantity::bytes(100000000LL), w));
  bundle.push_back(makeReq(ReservationRequestId(4), ResourceClass::NetworkBandwidth, Unit::BytesPerSecond, Quantity::bytesPerSecond(5000000LL), w));
  auto cr = rf.commitBundle(bundle);
  CHECK(cr.committed);
  CHECK(cr.memberIds.size() == 4);
  CHECK(rf.setCount() == 1);
  CHECK(rf.accounting(ResourceId(1)).committed.value() == 50);
  CHECK(rf.accounting(ResourceId(2)).committed.value() == 1000000000LL);
  CHECK(rf.accounting(ResourceId(3)).committed.value() == 100000000LL);
  CHECK(rf.accounting(ResourceId(4)).committed.value() == 5000000LL);

  // A failing bundle (last member over-subscribes an already-committed resource):
  // no reservation may survive, no leak.
  ReservationFabric rf2;
  rf2.publishResource(makeResource(ResourceId(5), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(100)));
  rf2.publishResource(makeResource(ResourceId(8), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(100)));
  rf2.publishResource(makeResource(ResourceId(9), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(100)));
  auto sat = rf2.commit(makeReq(ReservationRequestId(10), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(100), w));
  CHECK(sat.committed);
  std::vector<ReservationRequest> bad;
  bad.push_back(makeReq(ReservationRequestId(11), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(40), w));
  bad.push_back(makeReq(ReservationRequestId(12), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(40), w));
  bad.push_back(makeReq(ReservationRequestId(13), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(1), w));  // final component only has saturated R5
  auto br = rf2.commitBundle(bad);
  CHECK(!br.committed);
  CHECK(br.rolledBack);
  CHECK(rf2.accounting(ResourceId(5)).committed.value() == 100);   // unchanged; no partial commit
  CHECK(rf2.accounting(ResourceId(8)).committed.value() == 0);     // no leaked commits from leading feasible members
  CHECK(rf2.accounting(ResourceId(9)).committed.value() == 0);

  // Hold-rollback: two members on the SAME resource whose sum exceeds capacity
  // -> hold acquisition for the second releases the first; baseline preserved.
  ReservationFabric rf3;
  rf3.publishResource(makeResource(ResourceId(6), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(100)));
  std::vector<ReservationRequest> clash;
  clash.push_back(makeReq(ReservationRequestId(21), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(60), w));
  clash.push_back(makeReq(ReservationRequestId(22), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(50), w));  // 60+50>100
  auto kr = rf3.commitBundle(clash);
  CHECK(!kr.committed);
  CHECK(kr.rolledBack);
  CHECK(rf3.accounting(ResourceId(6)).committed.value() == 0);   // no leak
  CHECK(rf3.accounting(ResourceId(6)).headroom.value() == 100);
  CHECK(rf3.holdCount() == 0);   // all provisional holds released

  // Duplicate request identity rejected.
  ReservationFabric rf4;
  rf4.publishResource(makeResource(ResourceId(7), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(100)));
  auto d1 = rf4.commit(makeReq(ReservationRequestId(31), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(10), w));
  CHECK(d1.committed);
  auto d2 = rf4.commit(makeReq(ReservationRequestId(31), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(10), w));
  CHECK(!d2.committed);

  std::cout << "bundle tests done; failures=" << failures << "\n";
  return failures == 0 ? 0 : 1;
}
