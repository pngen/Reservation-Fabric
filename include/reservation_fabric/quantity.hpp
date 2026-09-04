#pragma once
#include <cstdint>
#include <limits>
#include <reservation_fabric/error.hpp>

namespace reservation_fabric {

// The base unit of measurement for a resource class. Quantities are integer
// counts measured in exactly one unit; no raw untyped doubles are used for
// bytes, bandwidth, capacity, or time.
enum class Unit : std::uint8_t {
  None = 0,
  Count,            // discrete units (accelerators, slots, queues)
  Bytes,            // memory / storage capacity
  BytesPerSecond,   // transfer bandwidth
  BitsPerSecond,    // network bandwidth
  Nodes,            // hosts
  Percent,          // capacity fraction (as integer percentage points)
  Hertz             // frequency
};

inline const char* to_string(Unit u) noexcept {
  switch (u) {
    case Unit::Count: return "Count";
    case Unit::Bytes: return "Bytes";
    case Unit::BytesPerSecond: return "BytesPerSecond";
    case Unit::BitsPerSecond: return "BitsPerSecond";
    case Unit::Nodes: return "Nodes";
    case Unit::Percent: return "Percent";
    case Unit::Hertz: return "Hertz";
    case Unit::None: return "None";
  }
  return "Unknown";
}

namespace detail {
inline void check_quantity_rep(std::int64_t value, const char* what) {
  if (value < 0) throw_error(ErrorCode::InvalidQuantity, std::string(what) + " must be non-negative");
}
inline std::int64_t checked_add(std::int64_t a, std::int64_t b, const char* what) {
  if (a > std::numeric_limits<std::int64_t>::max() - b) throw_error(ErrorCode::QuantityOverflow, std::string(what) + " addition overflow");
  return a + b;
}
inline std::int64_t checked_mul(std::int64_t a, std::int64_t b, const char* what) {
  if (a != 0 && b > std::numeric_limits<std::int64_t>::max() / a) throw_error(ErrorCode::QuantityOverflow, std::string(what) + " multiplication overflow");
  return a * b;
}
}  // namespace detail

// A non-negative integer quantity measured in a Unit. Strongly typed by unit so
// that a Bytes quantity cannot be silently added to a Count quantity.
class Quantity {
 public:
  using rep = std::int64_t;
  constexpr Quantity() noexcept = default;
  Quantity(rep value, Unit unit) : value_(value), unit_(unit) {
    detail::check_quantity_rep(value_, "quantity");
    if (unit_ == Unit::None) throw_error(ErrorCode::InvalidQuantity, "quantity requires a non-None unit");
  }

  static Quantity count(rep v) { return Quantity(v, Unit::Count); }
  static Quantity bytes(rep v) { return Quantity(v, Unit::Bytes); }
  static Quantity bytesPerSecond(rep v) { return Quantity(v, Unit::BytesPerSecond); }
  static Quantity bitsPerSecond(rep v) { return Quantity(v, Unit::BitsPerSecond); }
  static Quantity nodes(rep v) { return Quantity(v, Unit::Nodes); }
  static Quantity percent(rep v) { return Quantity(v, Unit::Percent); }
  static Quantity hertz(rep v) { return Quantity(v, Unit::Hertz); }
  static Quantity zero(Unit u) noexcept { return Quantity(rep{0}, u); }

  constexpr rep value() const noexcept { return value_; }
  constexpr Unit unit() const noexcept { return unit_; }
  constexpr bool isZero() const noexcept { return value_ == 0; }

  Quantity operator+(Quantity other) const {
    if (other.unit_ != unit_) throw_error(ErrorCode::InvalidQuantity, "cannot add quantities of different units");
    return Quantity(detail::checked_add(value_, other.value_, "quantity"), unit_);
  }
  Quantity operator-(Quantity other) const {
    if (other.unit_ != unit_) throw_error(ErrorCode::InvalidQuantity, "cannot subtract quantities of different units");
    if (value_ < other.value_) throw_error(ErrorCode::InvalidQuantity, "quantity subtraction underflow");
    return Quantity(value_ - other.value_, unit_);
  }
  Quantity operator*(rep scalar) const {
    if (scalar < 0) throw_error(ErrorCode::InvalidQuantity, "quantity scaled by negative factor");
    return Quantity(detail::checked_mul(value_, scalar, "quantity"), unit_);
  }
  Quantity scaledByPercent(rep percent) const {
    // Integer percent scaling (floor). Result unit stays.
    if (percent < 0 || percent > 100) throw_error(ErrorCode::InvalidQuantity, "percent out of range");
    return Quantity((value_ * percent) / 100, unit_);
  }

  constexpr bool operator==(Quantity o) const noexcept { return value_ == o.value_ && unit_ == o.unit_; }
  constexpr bool operator!=(Quantity o) const noexcept { return !(*this == o); }
  constexpr bool operator<(Quantity o) const noexcept { return value_ < o.value_; }
  constexpr bool operator<=(Quantity o) const noexcept { return value_ <= o.value_; }
  constexpr bool operator>(Quantity o) const noexcept { return value_ > o.value_; }
  constexpr bool operator>=(Quantity o) const noexcept { return value_ >= o.value_; }

  Quantity min(Quantity o) const noexcept { return value_ < o.value_ ? *this : o; }
  Quantity max(Quantity o) const noexcept { return value_ > o.value_ ? *this : o; }

 private:
  rep value_ = 0;
  Unit unit_ = Unit::Count;
};

// A bounded positive fraction (e.g. overbooking ratio) stored as basis points
// (per-10000) to avoid floating point. 1.0 == 10000 bp.
class Ratio {
 public:
  using rep = std::int64_t;
  constexpr Ratio() noexcept = default;
  explicit constexpr Ratio(rep basisPoints) noexcept : bp_(basisPoints) {}
  static Ratio one() noexcept { return Ratio(10000); }
  static Ratio percent(rep p) {
    if (p < 0 || p > 100000) throw_error(ErrorCode::InvalidQuantity, "ratio percent out of range");
    return Ratio(p * 100);
  }
  constexpr rep basisPoints() const noexcept { return bp_; }
  constexpr bool isUnity() const noexcept { return bp_ == 10000; }
  constexpr bool operator==(Ratio o) const noexcept { return bp_ == o.bp_; }
  constexpr bool operator<(Ratio o) const noexcept { return bp_ < o.bp_; }
  constexpr bool operator>(Ratio o) const noexcept { return bp_ > o.bp_; }
  // Does this ratio allow 'a' units of committed capacity for a physical
  // envelope of 'b' units without overcommitment breach?
  bool permits(Quantity committed, Quantity physical) const {
    if (committed.unit() != physical.unit()) return false;
    // committed <= physical * bp/10000, compared in integer math.
    if (committed.value() > 0 && physical.value() > (std::numeric_limits<std::int64_t>::max() / bp_)) return true; // physical*bp overflows -> definitely breach
    return committed.value() * 10000 <= physical.value() * bp_;
  }

 private:
  rep bp_ = 10000;
};

}  // namespace reservation_fabric
