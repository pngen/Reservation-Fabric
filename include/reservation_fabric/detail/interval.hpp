#pragma once
#include <cstdint>
#include <limits>
#include <vector>
#include <algorithm>
#include <reservation_fabric/error.hpp>
#include <reservation_fabric/time.hpp>

namespace reservation_fabric {
namespace detail {

// Deterministic keyed lazy treap over a time line. Each node is a half-open cell
// [start,end) carrying a uniform value state (committed-hard, committed-soft,
// held, capacity). Range operations split/merge the treap so per-operation work
// is O(log(#cells)) instead of O(#cells), even under dense overlapping schedules.
// Priority is a deterministic hash of the cell start, so behavior is reproducible
// and serialization never depends on a random seed.

inline std::uint32_t det_prio(std::uint64_t x) noexcept {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  x = x ^ (x >> 31);
  std::uint32_t r = static_cast<std::uint32_t>(x) | 1u;
  return r;
}

inline bool mul_overflow(std::int64_t a, std::int64_t b, std::int64_t& out) noexcept {
  if (a == 0 || b == 0) { out = 0; return false; }
  if (a > 0 && b > 0 && a > std::numeric_limits<std::int64_t>::max() / b) return true;
  if (a < 0 && b < 0 && a < std::numeric_limits<std::int64_t>::max() / b) return true;
  if (a < 0 && b > 0 && a < std::numeric_limits<std::int64_t>::min() / b) return true;
  if (a > 0 && b < 0 && b < std::numeric_limits<std::int64_t>::min() / a) return true;
  out = a * b; return false;
}

class IntervalAggregator {
 public:
  explicit IntervalAggregator(std::int64_t ratioBasisPoints = 10000) : ratioBp_(ratioBasisPoints) {}

  bool initialize(Instant origin, Duration horizon, std::int64_t capacity) {
    pool_.clear(); root_ = -1;
    if (horizon.isZero()) return false;
    std::int64_t h = horizon.nanoseconds();
    std::int64_t o = origin.nanoseconds();
    int32_t n = createCell(o, o + h, 0, 0, 0, capacity);
    root_ = n; pull(n);
    return true;
  }

  bool empty() const { return root_ == -1; }
  void clear() { pool_.clear(); root_ = -1; }
  std::size_t cellCount() const { return pool_.size(); }

  void addCommittedHard(Instant l, Instant r, std::int64_t delta) { if (delta) rangeAdd(l.nanoseconds(), r.nanoseconds(), delta, 0, 0); }
  void addCommittedSoft(Instant l, Instant r, std::int64_t delta) { if (delta) rangeAdd(l.nanoseconds(), r.nanoseconds(), 0, delta, 0); }
  void addHeld(Instant l, Instant r, std::int64_t delta) { if (delta) rangeAdd(l.nanoseconds(), r.nanoseconds(), 0, 0, delta); }
  void setCapacity(Instant l, Instant r, std::int64_t value) { rangeSet(l.nanoseconds(), r.nanoseconds(), value); }

  std::int64_t minHard(Instant l, Instant r) { return minSlack(l.nanoseconds(), r.nanoseconds(), true); }
  std::int64_t minSoft(Instant l, Instant r) { return minSlack(l.nanoseconds(), r.nanoseconds(), false); }

  // Max over [l,r) of (committed-hard + held) — useful for accounting checks.
  std::int64_t maxCombinedHard(Instant l, Instant r) { return maxContrib(l.nanoseconds(), r.nanoseconds(), true); }
  std::int64_t maxCombinedSoft(Instant l, Instant r) { return maxContrib(l.nanoseconds(), r.nanoseconds(), false); }

  std::int64_t ratioBasisPoints() const { return ratioBp_; }

 private:
  struct Node {
    std::int64_t start = 0, end = 0;
    std::int64_t committedHard = 0, committedSoft = 0, held = 0, capacity = 0;
    std::int64_t minHard = 0, minSoft = 0, maxCbH = 0, maxCbS = 0;
    std::int64_t addH = 0, addS = 0, addHeld = 0;
    bool hasSet = false; std::int64_t setCap = 0;
    std::uint32_t prio = 0;
    int32_t left = -1, right = -1;
  };

  std::int64_t effCapacity(std::int64_t cap) const {
    if (ratioBp_ == 10000) return cap;
    std::int64_t product = 0;
    if (mul_overflow(cap, ratioBp_, product)) return std::numeric_limits<std::int64_t>::max();
    return product / 10000;
  }

  std::int64_t selfMinHard(const Node& n) const { return n.capacity - n.committedHard - n.held; }
  std::int64_t selfMinSoft(const Node& n) const { return effCapacity(n.capacity) - n.committedHard - n.committedSoft - n.held; }

  void applyAddH(int32_t n, std::int64_t d) {
    if (n == -1 || d == 0) return;
    Node& x = pool_[n];
    x.committedHard += d; x.minHard -= d; x.minSoft -= d; x.maxCbH += d; x.maxCbS += d; x.addH += d;
  }
  void applyAddS(int32_t n, std::int64_t d) {
    if (n == -1 || d == 0) return;
    Node& x = pool_[n];
    x.committedSoft += d; x.minSoft -= d; x.maxCbS += d; x.addS += d;
  }
  void applyAddHeld(int32_t n, std::int64_t d) {
    if (n == -1 || d == 0) return;
    Node& x = pool_[n];
    x.held += d; x.minHard -= d; x.minSoft -= d; x.maxCbH += d; x.maxCbS += d; x.addHeld += d;
  }
  void applySetCap(int32_t n, std::int64_t value) {
    if (n == -1) return;
    Node& x = pool_[n];
    x.capacity = value;
    x.minHard = value - x.maxCbH;
    x.minSoft = effCapacity(value) - x.maxCbS;
    x.hasSet = true; x.setCap = value;
  }

  void push(int32_t n) {
    if (n == -1) return;
    Node& x = pool_[n];
    if (x.hasSet) { if (x.left != -1) applySetCap(x.left, x.setCap); if (x.right != -1) applySetCap(x.right, x.setCap); x.hasSet = false; }
    if (x.addH != 0) { if (x.left != -1) applyAddH(x.left, x.addH); if (x.right != -1) applyAddH(x.right, x.addH); x.addH = 0; }
    if (x.addS != 0) { if (x.left != -1) applyAddS(x.left, x.addS); if (x.right != -1) applyAddS(x.right, x.addS); x.addS = 0; }
    if (x.addHeld != 0) { if (x.left != -1) applyAddHeld(x.left, x.addHeld); if (x.right != -1) applyAddHeld(x.right, x.addHeld); x.addHeld = 0; }
  }

  void pull(int32_t n) {
    Node& x = pool_[n];
    std::int64_t mh = selfMinHard(x), ms = selfMinSoft(x);
    std::int64_t mxh = x.committedHard + x.held;
    std::int64_t mxs = x.committedHard + x.committedSoft + x.held;
    if (x.left != -1) {
      const Node& l = pool_[x.left];
      mh = std::min(mh, l.minHard); ms = std::min(ms, l.minSoft);
      mxh = std::max(mxh, l.maxCbH); mxs = std::max(mxs, l.maxCbS);
    }
    if (x.right != -1) {
      const Node& r = pool_[x.right];
      mh = std::min(mh, r.minHard); ms = std::min(ms, r.minSoft);
      mxh = std::max(mxh, r.maxCbH); mxs = std::max(mxs, r.maxCbS);
    }
    x.minHard = mh; x.minSoft = ms; x.maxCbH = mxh; x.maxCbS = mxs;
  }

  int32_t createCell(std::int64_t start, std::int64_t end, std::int64_t cH, std::int64_t cS, std::int64_t hd, std::int64_t cap) {
    Node n;
    n.start = start; n.end = end;
    n.committedHard = cH; n.committedSoft = cS; n.held = hd; n.capacity = cap;
    n.prio = det_prio(static_cast<std::uint64_t>(start));
    pool_.push_back(n);
    return static_cast<int32_t>(pool_.size() - 1);
  }

  int32_t mergeRec(int32_t a, int32_t b) {
    if (a == -1) return b;
    if (b == -1) return a;
    if (pool_[a].prio > pool_[b].prio) {
      push(a);
      pool_[a].right = mergeRec(pool_[a].right, b);
      pull(a);
      return a;
    } else {
      push(b);
      pool_[b].left = mergeRec(a, pool_[b].left);
      pull(b);
      return b;
    }
  }

  void split(int32_t n, std::int64_t key, int32_t& l, int32_t& r) {
    if (n == -1) { l = r = -1; return; }
    push(n);
    if (pool_[n].start < key) {
      int32_t nr;
      split(pool_[n].right, key, nr, r);
      pool_[n].right = nr;
      l = n; pull(l);
    } else {
      int32_t nl;
      split(pool_[n].left, key, l, nl);
      pool_[n].left = nl;
      r = n; pull(r);
    }
  }

  int32_t detachMax(int32_t n, int32_t& outMax) {
    if (n == -1) { outMax = -1; return -1; }
    push(n);
    if (pool_[n].right == -1) {
      outMax = n;
      int32_t sub = pool_[n].left;
      pool_[n].left = pool_[n].right = -1;
      return sub;
    }
    int32_t nr = detachMax(pool_[n].right, outMax);
    pool_[n].right = nr;
    pull(n);
    return n;
  }

  int32_t rightmostNode(int32_t n) const {
    while (n != -1 && pool_[n].right != -1) n = pool_[n].right;
    return n;
  }

  void ensureBoundary(std::int64_t t) {
    if (root_ == -1) return;
    int32_t a = -1, b = -1;
    split(root_, t, a, b);
    int32_t rm = rightmostNode(a);
    if (rm == -1) { root_ = mergeRec(a, b); return; }
    const Node& cell = pool_[rm];
    if (cell.end == t || cell.end < t) { root_ = mergeRec(a, b); return; }
    std::int64_t cs = cell.start, ce = cell.end;
    std::int64_t cH = cell.committedHard, cS = cell.committedSoft, hd = cell.held, cap = cell.capacity;
    int32_t keep = -1;
    a = detachMax(a, keep);
    (void)keep;
    int32_t c1 = createCell(cs, t, cH, cS, hd, cap);
    int32_t c2 = createCell(t, ce, cH, cS, hd, cap);
    a = mergeRec(a, c1);
    a = mergeRec(a, c2);
    root_ = mergeRec(a, b);
  }

  void rangeAdd(std::int64_t l, std::int64_t r, std::int64_t dH, std::int64_t dS, std::int64_t dHeld) {
    if (root_ == -1 || r <= l) return;
    ensureBoundary(l); ensureBoundary(r);
    int32_t a = -1, b = -1, c = -1, mid = -1;
    split(root_, r, a, b);
    split(a, l, c, mid);
    if (mid != -1) {
      applyAddH(mid, dH); applyAddS(mid, dS); applyAddHeld(mid, dHeld);
    }
    root_ = mergeRec(mergeRec(c, mid), b);
  }

  void rangeSet(std::int64_t l, std::int64_t r, std::int64_t value) {
    if (root_ == -1 || r <= l) return;
    ensureBoundary(l); ensureBoundary(r);
    int32_t a = -1, b = -1, c = -1, mid = -1;
    split(root_, r, a, b);
    split(a, l, c, mid);
    if (mid != -1) applySetCap(mid, value);
    root_ = mergeRec(mergeRec(c, mid), b);
  }

  std::int64_t minSlack(std::int64_t l, std::int64_t r, bool hard) {
    if (root_ == -1 || r <= l) return std::numeric_limits<std::int64_t>::max();
    ensureBoundary(l); ensureBoundary(r);
    int32_t a = -1, b = -1, c = -1, mid = -1;
    split(root_, r, a, b);
    split(a, l, c, mid);
    std::int64_t res = (mid == -1) ? std::numeric_limits<std::int64_t>::max()
                                   : (hard ? pool_[mid].minHard : pool_[mid].minSoft);
    root_ = mergeRec(mergeRec(c, mid), b);
    return res;
  }

  std::int64_t maxContrib(std::int64_t l, std::int64_t r, bool hard) {
    if (root_ == -1 || r <= l) return 0;
    ensureBoundary(l); ensureBoundary(r);
    int32_t a = -1, b = -1, c = -1, mid = -1;
    split(root_, r, a, b);
    split(a, l, c, mid);
    std::int64_t res = (mid == -1) ? 0 : (hard ? pool_[mid].maxCbH : pool_[mid].maxCbS);
    root_ = mergeRec(mergeRec(c, mid), b);
    return res;
  }

  std::vector<Node> pool_;
  int32_t root_ = -1;
  std::int64_t ratioBp_ = 10000;
};

}  // namespace detail
}  // namespace reservation_fabric
