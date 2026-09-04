#include "test_common.hpp"
#include <random>
#include <map>

// Reference is keyed by reservation id so bookkeeping is exact.
struct RefModel {
  struct Entry { std::int64_t s, e, qty; bool soft; };
  std::map<std::uint64_t, Entry> live;
  void add(std::uint64_t id, std::int64_t s, std::int64_t e, std::int64_t qty, bool soft) { live[id] = {s, e, qty, soft}; }
  void erase(std::uint64_t id) { live.erase(id); }
  std::int64_t hardAt(std::int64_t t) const {
    std::int64_t sum = 0;
    for (const auto& [id, x] : live) if (!x.soft && t >= x.s && t < x.e) sum += x.qty;
    return sum;
  }
};

int main() {
  int failures = 0;
  std::uint64_t seed = 0x9E3779B97F4A7C15ULL;
  std::mt19937_64 rng(seed);
  auto report = [&]() { std::fprintf(stderr, "property seed=%llu failures=%d\n", (unsigned long long)seed, failures); };

  const std::int64_t horizon = 1000000;
  const int trials = 4000;
  const std::int64_t cap = 100;

  for (int iter = 0; iter < trials; ++iter) {
    ReservationFabric rf;
    rf.publishResource(makeResource(ResourceId(7), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(cap)));
    RefModel ref;
    int nOps = 1 + (int)(rng() % 14);
    for (int op = 0; op < nOps; ++op) {
      std::int64_t s = 100 + (std::int64_t)(rng() % horizon);
      std::int64_t dur = 1 + (std::int64_t)(rng() % 20000);
      std::int64_t qty = 1 + (std::int64_t)(rng() % 25);
      bool soft = (rng() % 4 == 0);
      ReservationRequest req = makeReq(ReservationRequestId(1000 + iter * 100 + op), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(qty), Interval(Instant(s), Duration::nanoseconds(dur)), OwnerId(1), soft ? ReservationStrength::Soft : ReservationStrength::Hard);
      req.targetResource = ResourceId(7);
      auto rc = rf.commit(req);
      if (rc.committed) {
        const Reservation* r = rf.reservation(rc.id);
        ref.add(rc.id.value(), r->window.start().nanoseconds(), r->window.end().nanoseconds(), qty, soft);
        // One current authoritative generation.
        if (rf.reservation(rc.id)->generation != rc.generation) ++failures;
      }
    }
    // Cross-check headroomAt (hard) against the exact reference at many instants.
    for (int probe = 0; probe < 8; ++probe) {
      std::int64_t t = 100 + (std::int64_t)(rng() % horizon);
      std::int64_t expected = cap - ref.hardAt(t);
      std::int64_t envAt = rf.headroomAt(ResourceId(7), Instant(t), true).value();
      if (envAt < 0) { ++failures; continue; }
      if (envAt != expected) {
        std::fprintf(stderr, "MISMATCH iter=%d t=%lld env=%lld expected=%lld refhard=%lld nlive=%d\n", iter, (long long)t, (long long)envAt, (long long)expected, (long long)ref.hardAt(t), (int)ref.live.size());
        for (const auto& [id, e] : ref.live) std::fprintf(stderr, "  id=%llu [%lld,%lld) qty=%lld soft=%d\n", (unsigned long long)id, (long long)e.s, (long long)e.e, (long long)e.qty, e.soft?1:0);
        ++failures;
      }
    }
    // Release exactness: releasing a reservation restores its contribution exactly,
    // and the released reservation no longer reduces headroom.
    for (auto it = ref.live.begin(); it != ref.live.end(); ) {
      std::uint64_t id = it->first;
      std::int64_t es = it->second.s;
      ReservationId rid(id);
      const Reservation* r = rf.reservation(rid);
      if (!r) { ++it; continue; }
      auto rel = rf.release(rid, r->generation, makeReleaseEvidence(WorkerId(1), WorkerBootId(1)));
      if (rel.released) {
        it = ref.live.erase(it);
        std::int64_t expected = cap - ref.hardAt(es);
        std::int64_t h = rf.headroomAt(ResourceId(7), Instant(es), true).value();
        if (h != expected) { std::fprintf(stderr, "RELEASE-MISMATCH id=%llu h=%lld expected=%lld\n", (unsigned long long)id, (long long)h, (long long)expected); ++failures; }
      } else { ++it; }
    }
    if (failures > 20) break;
  }
  report();
  return failures == 0 ? 0 : 1;
}