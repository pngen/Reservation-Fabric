#include "test_common.hpp"
#include <limits>
#include <stdexcept>
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL: " << #cond << " (line " << __LINE__ << ")\n"; ++failures; } } while (0)
#define CHECK_THROW(expr) do { bool threw = false; try { (void)(expr); } catch (...) { threw = true; } if (!threw) { std::cerr << "FAIL(no-throw): " << #expr << " (line " << __LINE__ << ")\n"; ++failures; } } while (0)

int main() {
  try {
    ReservationFabric rf;
    rf.publishResource(makeResource(ResourceId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(100)));
    Interval w(Instant(100), Duration::seconds(100));

    CHECK_THROW(Quantity::count(-1));
    CHECK_THROW(Quantity::bytes(5) + Quantity::count(5));
    {
      ReservationRequest bad = makeReq(ReservationRequestId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(0), w);
      auto rep = rf.assess(bad);
      CHECK(rep.outcome != Feasibility::Admissible);
    }
    CHECK_THROW(Interval(Instant(500), Instant(100)));

    // Quantity overflow rejected rather than wrapping.
    {
      ReservationRequest big = makeReq(ReservationRequestId(2), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(std::numeric_limits<std::int64_t>::max() - 1), w);
      auto rc = rf.commit(big);
      CHECK(!rc.committed);
    }
    // Invalid resource publication (capacity unit mismatch) is rejected.
    {
      std::string err;
      auto pub = makeResource(ResourceId(9), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::bytes(100));
      CHECK(rf.publishResource(pub, &err) == ResourceRegisterResult::RejectedInvalid);
    }

    auto rc = rf.commit(makeReq(ReservationRequestId(3), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(10), w));
    CHECK(rc.committed);
    CHECK(!rf.activate(rc.id, rc.generation, makeEvidence(WorkerId(99), WorkerBootId(1))).activated);

    auto rc2 = rf.commit(makeReq(ReservationRequestId(4), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(10), w));
    rf.cancel(rc2.id, rc2.generation);
    CHECK(!rf.activate(rc2.id, rc2.generation, makeEvidence()).activated);

    auto rc3 = rf.commit(makeReq(ReservationRequestId(5), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(10), w));
    rf.expireDue(Instant(200000000000LL));
    CHECK(!rf.activate(rc3.id, rc3.generation, makeEvidence()).activated);

    auto m = rf.renew(rc3.id, rc3.generation, Interval(Instant(500), Duration::seconds(100)), PolicyGeneration(1));
    CHECK(m.applied);

    auto rc4 = rf.commit(makeReq(ReservationRequestId(6), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(10), w));
    CHECK(!rf.release(rc4.id, rc4.generation, makeReleaseEvidence(WorkerId(55), WorkerBootId(1))).released);
    CHECK(rf.release(rc4.id, rc4.generation, makeReleaseEvidence(WorkerId(1), WorkerBootId(1))).released);
    CHECK(rf.release(rc4.id, rc4.generation, makeReleaseEvidence(WorkerId(1), WorkerBootId(1))).idempotent);
    CHECK(rf.accounting(ResourceId(1)).headroom.value() >= 0);

    auto rc5 = rf.commit(makeReq(ReservationRequestId(7), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(10), w, OwnerId(2)));
    CHECK(rc5.committed);
    auto m2 = rf.resize(rc5.id, rc5.generation, Quantity::count(20), PolicyGeneration(1));
    CHECK(m2.applied);

    rf.setCoordinatorEpoch(CoordinatorEpoch(2));
    CHECK_THROW(rf.setCoordinatorEpoch(CoordinatorEpoch(1)));
  } catch (const std::exception& e) {
    std::cerr << "adversarial uncaught exception: " << e.what() << "\n";
    ++failures;
  }
  std::cout << "adversarial tests done; failures=" << failures << "\n";
  return failures == 0 ? 0 : 1;
}