#include "test_common.hpp"
#include <thread>
#include <atomic>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL: " << #cond << " (line " << __LINE__ << ")\n"; ++failures; } } while (0)

int main() {
  // Two concurrent reservations competing for the final units: capacity 10, each
  // requests 5 over the same window. Exactly two may win; no overcommit.
  const int THREADS = 8;
  ReservationFabric rf;
  rf.publishResource(makeResource(ResourceId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(10)));
  Interval w(Instant(100), Duration::seconds(100));
  std::atomic<int> wins{0};
  std::vector<ReservationId> winners;
  std::atomic<std::int64_t> committedTotal{0};

  std::vector<std::thread> threads;
  for (int i = 0; i < THREADS; ++i) {
    threads.emplace_back([&, i]() {
      ReservationRequest req = makeReq(ReservationRequestId(100 + i), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(5), w, OwnerId(1));
      req.targetResource = ResourceId(1);
      auto rc = rf.commit(req);
      if (rc.committed) {
        wins.fetch_add(1);
        committedTotal.fetch_add(5);
        winners.push_back(rc.id);
      }
    });
  }
  for (auto& t : threads) t.join();

  CHECK(wins.load() == 2);                    // exactly one winner per 5 units
  CHECK(committedTotal.load() == 10);
  auto acc = rf.accounting(ResourceId(1));
  CHECK(acc.headroom.value() == 0);
  CHECK(acc.consistent);
  CHECK(acc.committed.value() == 10);

  // Concurrent feasibility queries while writers commit on a separate resource.
  ReservationFabric rf2;
  rf2.publishResource(makeResource(ResourceId(2), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(50)));
  std::atomic<int> qwins{0};
  std::vector<std::thread> qthreads;
  std::atomic<bool> stop{false};
  for (int i = 0; i < 4; ++i) {
    qthreads.emplace_back([&]() {
      while (!stop.load()) {
        auto rep = rf2.assess(makeReq(ReservationRequestId(10000), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(10), w, OwnerId(1)));
        (void)rep;
      }
    });
  }
  for (int i = 0; i < 16; ++i) {
    ReservationRequest req = makeReq(ReservationRequestId(200 + i), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(3), w, OwnerId(1));
    req.targetResource = ResourceId(2);
    auto rc = rf2.commit(req);
    if (rc.committed) qwins.fetch_add(1);
  }
  stop = true;
  for (auto& t : qthreads) t.join();
  auto acc2 = rf2.accounting(ResourceId(2));
  CHECK(acc2.consistent);
  CHECK(acc2.headroom.value() >= 0);

  // Concurrent release/resize on a populated resource must keep accounting exact.
  ReservationFabric rf3;
  rf3.publishResource(makeResource(ResourceId(3), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(60)));
  std::vector<ReservationId> ids;
  std::vector<ReservationGeneration> gens;
  std::vector<std::thread> wthreads;
  std::atomic<int> win3{0};
  for (int i = 0; i < 6; ++i) {
    wthreads.emplace_back([&, i]() {
      ReservationRequest req = makeReq(ReservationRequestId(300 + i), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(10), w, OwnerId(1));
      req.targetResource = ResourceId(3);
      auto rc = rf3.commit(req);
      if (rc.committed) { win3.fetch_add(1); }
    });
  }
  for (auto& t : wthreads) t.join();
  CHECK(rf3.accounting(ResourceId(3)).committed.value() == 60);
  // Release all winners in parallel.
  ids = rf3.reservationsForResource(ResourceId(3));
  std::vector<std::thread> rthreads;
  for (auto id : ids) {
    auto r = rf3.reservation(id);
    rthreads.emplace_back([&, id, r]() {
      rf3.release(id, r->generation, makeReleaseEvidence(WorkerId(1), WorkerBootId(1)));
    });
  }
  for (auto& t : rthreads) t.join();
  CHECK(rf3.accounting(ResourceId(3)).committed.value() == 0);
  CHECK(rf3.accounting(ResourceId(3)).headroom.value() == 60);

  std::cout << "race tests done; failures=" << failures << "\n";
  return failures == 0 ? 0 : 1;
}