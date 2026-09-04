#include <reservation_fabric/detail/serialization.hpp>
#include <reservation_fabric/quantity.hpp>
#include <cstdio>
using namespace reservation_fabric;
using namespace reservation_fabric::detail;

// Regression: reading an EMPTY string as the last field of a payload used to
// access &data_[pos_] with pos_==data_.size(), which is a Debug-only bounds
// assert (vector::operator[] at size()) and UB in any configuration. That made
// the coordinator abort while deserializing a ReservationRequest whose terminal
// provenance string is empty, closing the framed channel under Debug.
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL: %s (line %d)\n", #c, __LINE__); ++failures; } } while (0)

int main() {
  // Empty terminal string: must not assert/abort and must read back empty.
  {
    ByteWriter w;
    w.u32(7);
    w.str("");                          // terminal empty string
    ByteReader r(w.data());
    CHECK(r.u32() == 7);
    CHECK(r.str().empty());             // the previously-crashing path
    CHECK(r.empty());
  }
  // struct-like payload ending in an empty provenance string.
  {
    ByteWriter w;
    w.u64(42);
    w.i64(100);
    w.u8(static_cast<std::uint8_t>(Unit::Count));
    w.str("");
    ByteReader r(w.data());
    CHECK(r.u64() == 42);
    CHECK(r.quant().value() == 100);
    CHECK(r.str().empty());
    CHECK(r.empty());
  }
  // Empty string in the middle, then trailing data: still correct.
  {
    ByteWriter w;
    w.str("");
    w.u64(99);
    ByteReader r(w.data());
    CHECK(r.str().empty());
    CHECK(r.u64() == 99);
    CHECK(r.empty());
  }
  // A non-empty string, then an empty terminal string.
  {
    ByteWriter w;
    w.str("hello");
    w.str("");
    ByteReader r(w.data());
    CHECK(r.str() == "hello");
    CHECK(r.str().empty());
  }
  std::fprintf(stderr, "serialization tests done; failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}
