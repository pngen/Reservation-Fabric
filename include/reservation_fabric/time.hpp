#pragma once
#include <cstdint>
#include <chrono>
#include <limits>
#include <reservation_fabric/error.hpp>

namespace reservation_fabric {
namespace detail {

inline void check_abs(std::int64_t v, const char* what) {
  if (v < 0) throw_error(ErrorCode::InvalidQuantity, std::string(what) + " must be non-negative");
}

}  // namespace detail

// A non-negative elapsed duration in integer nanoseconds. Checked arithmetic.
// Strong type so it cannot be confused with an absolute time.
class Duration {
 public:
  using rep = std::int64_t;
  constexpr Duration() noexcept = default;
  constexpr explicit Duration(rep nanoseconds) noexcept : ns_(nanoseconds) {}

  static constexpr Duration zero() noexcept { return Duration(rep{0}); }
  static constexpr Duration nanoseconds(rep n) noexcept { return Duration(n); }
  static Duration seconds(rep s) {
    if (s > std::numeric_limits<rep>::max() / 1000000000LL) throw_error(ErrorCode::QuantityOverflow, "Duration::seconds overflow");
    return Duration(s * 1000000000LL);
  }

  constexpr rep nanoseconds() const noexcept { return ns_; }
  constexpr bool isZero() const noexcept { return ns_ == 0; }
  constexpr bool isPositive() const noexcept { return ns_ > 0; }

  Duration operator+(Duration other) const {
    if (ns_ > std::numeric_limits<rep>::max() - other.ns_) throw_error(ErrorCode::QuantityOverflow, "Duration addition overflow");
    return Duration(ns_ + other.ns_);
  }
  Duration operator-(Duration other) const {
    if (ns_ < other.ns_) throw_error(ErrorCode::InvalidQuantity, "Duration subtraction underflow");
    return Duration(ns_ - other.ns_);
  }
  Duration operator*(rep scalar) const {
    if (scalar < 0) throw_error(ErrorCode::InvalidQuantity, "Duration multiply by negative scalar");
    if (scalar != 0 && ns_ > std::numeric_limits<rep>::max() / scalar) throw_error(ErrorCode::QuantityOverflow, "Duration multiplication overflow");
    return Duration(ns_ * scalar);
  }
  Duration operator/(rep scalar) const {
    if (scalar <= 0) throw_error(ErrorCode::InvalidQuantity, "Duration division by non-positive scalar");
    return Duration(ns_ / scalar);
  }

  constexpr bool operator==(Duration o) const noexcept { return ns_ == o.ns_; }
  constexpr bool operator!=(Duration o) const noexcept { return ns_ != o.ns_; }
  constexpr bool operator<(Duration o) const noexcept { return ns_ < o.ns_; }
  constexpr bool operator<=(Duration o) const noexcept { return ns_ <= o.ns_; }
  constexpr bool operator>(Duration o) const noexcept { return ns_ > o.ns_; }
  constexpr bool operator>=(Duration o) const noexcept { return ns_ >= o.ns_; }
  Duration min(Duration o) const noexcept { return ns_ < o.ns_ ? *this : o; }
  Duration max(Duration o) const noexcept { return ns_ > o.ns_ ? *this : o; }

 private:
  rep ns_ = 0;
};

// An absolute point in time (integer nanoseconds). Used only for contract-level
// reservation semantics (activation windows, fixed intervals). Runtime elapsed
// logic uses the monotonic clock instead.
class Instant {
 public:
  using rep = std::int64_t;
  constexpr Instant() noexcept = default;
  constexpr explicit Instant(rep nanoseconds) noexcept : ns_(nanoseconds) {}

  static constexpr Instant epoch() noexcept { return Instant(rep{0}); }

  constexpr rep nanoseconds() const noexcept { return ns_; }

  Instant operator+(Duration d) const {
    if (d.nanoseconds() > 0 && ns_ > std::numeric_limits<rep>::max() - d.nanoseconds()) throw_error(ErrorCode::QuantityOverflow, "Instant addition overflow");
    return Instant(ns_ + d.nanoseconds());
  }
  Instant operator-(Duration d) const {
    if (d.nanoseconds() > 0 && ns_ < std::numeric_limits<rep>::min() + d.nanoseconds()) throw_error(ErrorCode::QuantityOverflow, "Instant subtraction overflow");
    return Instant(ns_ - d.nanoseconds());
  }

  // Duration from 'this' to 'other'. Other must not precede this.
  Duration durationUntil(Instant other) const {
    if (other.ns_ < ns_) throw_error(ErrorCode::InvalidInterval, "time flows forward: other precedes this");
    return Duration(other.ns_ - ns_);
  }
  Duration durationFrom(Instant other) const { return other.durationUntil(*this); }

  constexpr bool operator==(Instant o) const noexcept { return ns_ == o.ns_; }
  constexpr bool operator!=(Instant o) const noexcept { return ns_ != o.ns_; }
  constexpr bool operator<(Instant o) const noexcept { return ns_ < o.ns_; }
  constexpr bool operator<=(Instant o) const noexcept { return ns_ <= o.ns_; }
  constexpr bool operator>(Instant o) const noexcept { return ns_ > o.ns_; }
  constexpr bool operator>=(Instant o) const noexcept { return ns_ >= o.ns_; }
  Instant min(Instant o) const noexcept { return ns_ < o.ns_ ? *this : o; }
  Instant max(Instant o) const noexcept { return ns_ > o.ns_ ? *this : o; }

 private:
  rep ns_ = 0;
};

// A monotonic clock for runtime elapsed-duration logic (leases, timeouts,
// hold expiry). Never used as reservation authority.
class MonotonicClock {
 public:
  using rep = std::int64_t;
  static Instant now() {
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now().time_since_epoch()).count();
    return Instant(ns);
  }
};

// Wall/absolute clock for contract timestamps when explicitly requested.
class AbsoluteClock {
 public:
  using rep = std::int64_t;
  static Instant now() {
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    return Instant(ns);
  }
};

// A half-open interval [start, end) in absolute time. end must be strictly
// greater than start (positive length). Boundary semantics are explicit via the
// default half-open convention.
class Interval {
 public:
  constexpr Interval() noexcept = default;
  Interval(Instant start, Instant end)
      : start_(start), end_(end) {
    if (!(end_ > start_)) throw_error(ErrorCode::InvalidInterval, "interval end must be strictly after start");
  }

  Interval(Instant start, Duration length) : start_(start), end_(start + length) {
    if (!(end_ > start_)) throw_error(ErrorCode::InvalidInterval, "interval end must be strictly after start");
  }

  // Zero-length (point) reservations are not supported in 1.0.0.

  constexpr Instant start() const noexcept { return start_; }
  constexpr Instant end() const noexcept { return end_; }
  Duration length() const noexcept { return start_.durationUntil(end_); }
  constexpr bool isEmpty() const noexcept { return end_ <= start_; }

  // Overlap of two half-open intervals; returns length of the intersection.
  Duration overlap(const Interval& other) const noexcept {
    Instant lo = start_.max(other.start_);
    Instant hi = end_.min(other.end_);
    return (hi > lo) ? lo.durationUntil(hi) : Duration::zero();
  }
  bool overlaps(const Interval& other) const noexcept { return end_ > other.start_ && start_ < other.end_; }
  bool contains(Instant t) const noexcept { return t >= start_ && t < end_; }

  constexpr bool operator==(const Interval& o) const noexcept { return start_ == o.start_ && end_ == o.end_; }
  constexpr bool operator!=(const Interval& o) const noexcept { return !(*this == o); }

 private:
  Instant start_;
  Instant end_;
};

}  // namespace reservation_fabric
