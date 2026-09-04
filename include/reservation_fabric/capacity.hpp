#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <reservation_fabric/detail/interval.hpp>
#include <reservation_fabric/error.hpp>
#include <reservation_fabric/identity.hpp>
#include <reservation_fabric/quantity.hpp>
#include <reservation_fabric/time.hpp>
#include <reservation_fabric/resource.hpp>

namespace reservation_fabric {

// The per-resource capacity envelope. This is a query-acceleration structure
// that answers "how much headroom remains over this window" in O(log N). The
// authoritative set of commitments lives in the reservation registry; this index
// mirrors it for fast admission and overlap queries.
class CapacityEnvelope {
 public:
  CapacityEnvelope() = default;
  CapacityEnvelope(Unit unit, Quantity capacity, Ratio ratio)
      : unit_(unit), capacity_(capacity), ratio_(ratio) {}

  bool init(Instant origin, Duration horizon, Quantity capacity, Ratio ratio) {
    if (capacity.unit() != unit_ && unit_ != Unit::None) return false;
    unit_ = capacity.unit();
    capacity_ = capacity;
    ratio_ = ratio;
    origin_ = origin;
    horizon_ = horizon;
    agg_ = detail::IntervalAggregator(ratio.basisPoints());
    if (!agg_.initialize(origin, horizon, capacity.value())) return false;
    valid_ = true;
    return true;
  }
  Instant origin() const { return origin_; }
  Duration horizon() const { return horizon_; }

  bool valid() const { return valid_; }
  Unit unit() const { return unit_; }
  Quantity capacity() const { return capacity_; }
  Ratio ratio() const { return ratio_; }

  // Envelope contribution helpers. deltaQty is in the resource's base unit.
  void addCommittedHard(const Interval& i, std::int64_t qty) noexcept {
    if (!valid_) return;
    agg_.addCommittedHard(i.start(), i.end(), qty);
  }
  void addCommittedSoft(const Interval& i, std::int64_t qty) noexcept {
    if (!valid_) return;
    agg_.addCommittedSoft(i.start(), i.end(), qty);
  }
  void addHeld(const Interval& i, std::int64_t qty) noexcept {
    if (!valid_) return;
    agg_.addHeld(i.start(), i.end(), qty);
  }
  void removeCommittedHard(const Interval& i, std::int64_t qty) noexcept { addCommittedHard(i, -qty); }
  void removeCommittedSoft(const Interval& i, std::int64_t qty) noexcept { addCommittedSoft(i, -qty); }
  void removeHeld(const Interval& i, std::int64_t qty) noexcept { addHeld(i, -qty); }
  void setUnavailable(const Interval& i, std::int64_t capacityDuring) noexcept {
    if (!valid_) return;
    agg_.setCapacity(i.start(), i.end(), capacityDuring);
  }

  // Minimum headroom for a HARD reservation over the window. Negative means the
  // window is already over-committed (should not happen for hard).
  std::int64_t minHardHeadroom(const Interval& i) const {
    if (!valid_) return std::numeric_limits<std::int64_t>::max();
    return const_cast<detail::IntervalAggregator&>(agg_).minHard(i.start(), i.end());
  }
  std::int64_t minSoftHeadroom(const Interval& i) const {
    if (!valid_) return std::numeric_limits<std::int64_t>::max();
    return const_cast<detail::IntervalAggregator&>(agg_).minSoft(i.start(), i.end());
  }
  // Peak (committed-hard + held) over the window, for accounting checks.
  std::int64_t peakCombinedHard(const Interval& i) const {
    if (!valid_) return 0;
    return const_cast<detail::IntervalAggregator&>(agg_).maxCombinedHard(i.start(), i.end());
  }
  std::int64_t peakCombinedSoft(const Interval& i) const {
    if (!valid_) return 0;
    return const_cast<detail::IntervalAggregator&>(agg_).maxCombinedSoft(i.start(), i.end());
  }

 private:
  Unit unit_ = Unit::Count;
  Quantity capacity_ = Quantity::zero(Unit::Count);
  Ratio ratio_ = Ratio::one();
  Instant origin_;
  Duration horizon_;
  detail::IntervalAggregator agg_;
  bool valid_ = false;
};

// Snapshot categories for a resource scope. All integers are in the resource's
// base unit. Categories are computed from the authoritative registry.
struct CapacityAccounting {
  ResourceId resource;
  ResourceGeneration generation;
  ResourceClass resourceClass = ResourceClass::Unknown;
  Unit unit = Unit::Count;
  Quantity total = Quantity::zero(Unit::Count);
  Quantity committed = Quantity::zero(Unit::Count);      // HARD committed
  Quantity softCommitted = Quantity::zero(Unit::Count);
  Quantity held = Quantity::zero(Unit::Count);
  Quantity active = Quantity::zero(Unit::Count);
  Quantity consumed = Quantity::zero(Unit::Count);
  Quantity released = Quantity::zero(Unit::Count);
  Quantity expired = Quantity::zero(Unit::Count);
  Quantity cancelled = Quantity::zero(Unit::Count);
  Quantity unavailable = Quantity::zero(Unit::Count);
  Quantity unknown = Quantity::zero(Unit::Count);
  Quantity headroom = Quantity::zero(Unit::Count);
  Quantity peakCommittedFuture = Quantity::zero(Unit::Count);  // peak over horizon
  bool consistent = false;
};

}  // namespace reservation_fabric
