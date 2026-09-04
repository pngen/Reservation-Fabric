#include <reservation_fabric/runtime.hpp>
#include <chrono>
#include <cstdio>
#include <string>
using namespace reservation_fabric;

static double ms(const std::chrono::steady_clock::time_point& a, const std::chrono::steady_clock::time_point& b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

int main() {
  const std::vector<std::int64_t> sizes = {1000, 10000, 100000};
  Interval w(Instant(100), Duration::seconds(3600));

  for (std::int64_t n : sizes) {
    ReservationFabric rf;
    ResourcePublication p;
    p.worker = WorkerId(1); p.workerBoot = WorkerBootId(1);
    p.id = ResourceId(1); p.generation = ResourceGeneration(1); p.resourceClass = ResourceClass::AcceleratorCompute; p.unit = Unit::Count;
    p.totalCapacity = Quantity::count(n + 100000); p.availableFrom = Instant(0); p.lifetime = Duration::seconds(365*24*3600);
    auto t0 = std::chrono::steady_clock::now();
    rf.publishResource(p);
    auto t1 = std::chrono::steady_clock::now();

    std::int64_t committed = 0;
    for (std::int64_t i = 0; i < n; ++i) {
      ReservationRequest req;
      req.requestId = ReservationRequestId(static_cast<std::uint64_t>(i + 1));
      req.requestGeneration = ReservationRequestGeneration(1);
      req.resourceClass = ResourceClass::AcceleratorCompute; req.unit = Unit::Count;
      req.quantity = Quantity::count(1);
      req.window = w; req.activationMode = ActivationMode::FixedInterval; req.strength = ReservationStrength::Hard;
      req.owner = OwnerId(1); req.ownerGeneration = OwnerGeneration(1); req.policyGeneration = PolicyGeneration(1); req.priorityGeneration = PriorityGeneration(1);
      req.targetResource = ResourceId(1);
      auto rc = rf.commit(req);
      if (rc.committed) ++committed;
    }
    auto t2 = std::chrono::steady_clock::now();

    auto tq0 = std::chrono::steady_clock::now();
    std::int64_t acc = 0;
    for (std::int64_t i = 0; i < 100000; ++i) acc += rf.headroomAt(ResourceId(1), Instant(100 + (i * 37) % 1000000000), true).value();
    auto tq1 = std::chrono::steady_clock::now();

    auto ta0 = std::chrono::steady_clock::now();
    auto accounting = rf.accounting(ResourceId(1));
    auto ta1 = std::chrono::steady_clock::now();

    std::string path = "bench_" + std::to_string(n) + ".bin";
    auto tp0 = std::chrono::steady_clock::now();
    rf.save(path);
    auto rf2 = ReservationFabric::load(path);
    auto tp1 = std::chrono::steady_clock::now();

    std::printf("n=%lld publish=%.2fms commit=%.2fms (committed=%lld) query100k=%.2fms accounting=%.3fms save+load=%.2fms acc_committed=%lld consistent=%d\n",
                (long long)n, ms(t0,t1), ms(t1,t2), (long long)committed, ms(tq0,tq1), ms(ta0,ta1), ms(tp0,tp1),
                (long long)accounting.committed.value(), accounting.consistent ? 1 : 0);
    std::remove(path.c_str());
    (void)acc; (void)rf2;
  }
  return 0;
}
