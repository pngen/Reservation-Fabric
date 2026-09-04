#include <reservation_fabric/detail/interval.hpp>
#include <iostream>
using namespace reservation_fabric;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL: " << #cond << " (line " << __LINE__ << ")\n"; ++failures; } } while (0)

int main() {
  // Basic envelope: capacity 100 over [0, 1e9 ns). Commit 60 in [100, 200).
  {
    detail::IntervalAggregator a;
    CHECK(a.initialize(Instant(0), Duration::seconds(1000), 100));
    a.addCommittedHard(Instant(100), Instant(200), 60);
    CHECK(a.minHard(Instant(100), Instant(200)) == 40);
    CHECK(a.minHard(Instant(200), Instant(300)) == 100);
    CHECK(a.minHard(Instant(0), Instant(100)) == 100);
    // Boundary-touch: [199,200) still reduced, [200,201) not.
    CHECK(a.minHard(Instant(199), Instant(200)) == 40);
    CHECK(a.minHard(Instant(200), Instant(201)) == 100);
    // Release returns headroom.
    a.addCommittedHard(Instant(100), Instant(200), -60);
    CHECK(a.minHard(Instant(100), Instant(200)) == 100);
  }

  // Two non-overlapping reservations.
  {
    detail::IntervalAggregator a;
    a.initialize(Instant(0), Duration::seconds(1000), 100);
    a.addCommittedHard(Instant(100), Instant(200), 40);
    a.addCommittedHard(Instant(300), Instant(400), 30);
    CHECK(a.minHard(Instant(150), Instant(350)) == 60);  // min headroom: max committed=40 in [150,350)
    CHECK(a.minHard(Instant(100), Instant(200)) == 60);
    CHECK(a.minHard(Instant(300), Instant(400)) == 70);
    CHECK(a.minHard(Instant(200), Instant(300)) == 100);
  }

  // Soft vs hard separation.
  {
    detail::IntervalAggregator a;
    a.initialize(Instant(0), Duration::seconds(1000), 100);
    a.addCommittedHard(Instant(100), Instant(200), 60);   // hard takes 60
    a.addCommittedSoft(Instant(100), Instant(200), 60);   // soft capacity*ratio(1)=100, hard+soft=120>100
    CHECK(a.minHard(Instant(100), Instant(200)) == 40);   // hard headroom ignores soft
    CHECK(a.minSoft(Instant(100), Instant(200)) == -20);  // soft overbooked
  }

  // Maintenance / unavailable window sets capacity to 0.
  {
    detail::IntervalAggregator a;
    a.initialize(Instant(0), Duration::seconds(1000), 100);
    a.setCapacity(Instant(300), Instant(400), 0);
    CHECK(a.minHard(Instant(300), Instant(400)) == 0);
    CHECK(a.minHard(Instant(200), Instant(300)) == 100);
    CHECK(a.minHard(Instant(400), Instant(500)) == 100);
  }

  // Overbooking ratio 2.0: hard headroom still physical, soft allows up to 200.
  {
    detail::IntervalAggregator a(20000);
    a.initialize(Instant(0), Duration::seconds(1000), 100);
    a.addCommittedHard(Instant(100), Instant(200), 60);
    a.addCommittedSoft(Instant(100), Instant(200), 80);
    CHECK(a.minHard(Instant(100), Instant(200)) == 40);
    CHECK(a.minSoft(Instant(100), Instant(200)) == 60);   // 200 - 60 - 80 = 60
    // Adding more soft to breach soft envelope.
    a.addCommittedSoft(Instant(100), Instant(200), 60);
    CHECK(a.minSoft(Instant(100), Instant(200)) == 0);
    a.addCommittedSoft(Instant(100), Instant(200), 1);
    CHECK(a.minSoft(Instant(100), Instant(200)) == -1);
  }

  // Max combined hard (committed+held) for accounting checks.
  {
    detail::IntervalAggregator a;
    a.initialize(Instant(0), Duration::seconds(1000), 100);
    a.addCommittedHard(Instant(100), Instant(200), 60);
    a.addHeld(Instant(100), Instant(200), 20);
    CHECK(a.maxCombinedHard(Instant(0), Instant(1000)) == 80);
    CHECK(a.maxCombinedHard(Instant(200), Instant(300)) == 0);
  }

  std::cout << "interval tests done; failures=" << failures << "\n";
  return failures == 0 ? 0 : 1;
}
