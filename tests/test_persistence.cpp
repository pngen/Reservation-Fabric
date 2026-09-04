#include "test_common.hpp"
#include <fstream>
#include <vector>
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL: " << #cond << " (line " << __LINE__ << ")\n"; ++failures; } } while (0)
#define CHECK_THROW(expr) do { bool threw = false; try { (void)(expr); } catch (const FabricError& e) { threw = (e.code() == ErrorCode::CorruptData); } catch (...) { threw = true; } if (!threw) { std::cerr << "FAIL(no-throw): " << #expr << " (line " << __LINE__ << ")\n"; ++failures; } } while (0)

int main() {
  const char* path = "test_persist.bin";
  {
    ReservationFabric rf;
    rf.publishResource(makeResource(ResourceId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(100)));
    Interval w(Instant(100), Duration::seconds(100));
    auto a = rf.commit(makeReq(ReservationRequestId(1), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(60), w));
    CHECK(a.committed);
    auto c = rf.commit(makeReq(ReservationRequestId(2), ResourceClass::AcceleratorCompute, Unit::Count, Quantity::count(30), w));
    CHECK(c.committed);
    // Activate A so recovery must conservatively demote it.
    CHECK(rf.activate(a.id, a.generation, makeEvidence()).activated);
    rf.save(path);
  }

  // Round-trip preserves the committed schedule.
  {
    auto rf2 = ReservationFabric::load(path);
    CHECK(rf2 != nullptr);
    CHECK(rf2->currentReservationCount() == 2);
    CHECK(rf2->accounting(ResourceId(1)).committed.value() == 90);
    // Active reservation must NOT be restored as Active without fresh evidence.
    auto ids = rf2->reservationIds();
    for (auto id : ids) {
      const Reservation* r = rf2->reservation(id);
      if (r && r->quantity.value() == 60) {
        CHECK(r->lifecycle == Lifecycle::RevalidationRequired);
      }
    }
  }

  // Corruption: truncate -> CorruptData.
  {
    std::ifstream is(path, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    std::vector<unsigned char> truncated(data.begin(), data.begin() + data.size() / 2);
    std::ofstream os("test_trunc.bin", std::ios::binary);
    os.write(reinterpret_cast<const char*>(truncated.data()), static_cast<std::streamsize>(truncated.size()));
    CHECK_THROW(ReservationFabric::load("test_trunc.bin"));
  }
  // Corruption: flip a byte (checksum mismatch).
  {
    std::ifstream is(path, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    data[data.size() - 5] ^= 0xFF;
    std::ofstream os("test_corrupt.bin", std::ios::binary);
    os.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    CHECK_THROW(ReservationFabric::load("test_corrupt.bin"));
  }
  // Corruption: trailing garbage.
  {
    std::ifstream is(path, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    data.push_back(0xAB); data.push_back(0xCD);
    std::ofstream os("test_trail.bin", std::ios::binary);
    os.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    CHECK_THROW(ReservationFabric::load("test_trail.bin"));
  }
  // Bad magic.
  {
    std::ofstream os("test_badmagic.bin", std::ios::binary);
    os.write("XXXX", 4);
    os.write("\x01\x00\x00\x00\x00\x00\x00\x00\x00", 9);
    CHECK_THROW(ReservationFabric::load("test_badmagic.bin"));
  }

  std::cout << "persistence tests done; failures=" << failures << "\n";
  return failures == 0 ? 0 : 1;
}