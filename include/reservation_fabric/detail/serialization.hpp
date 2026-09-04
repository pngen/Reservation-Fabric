#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include <reservation_fabric/error.hpp>
#include <reservation_fabric/identity.hpp>
#include <reservation_fabric/quantity.hpp>
#include <reservation_fabric/time.hpp>

namespace reservation_fabric {
namespace detail {

// Compact little-endian binary primitives plus CRC32 integrity. Deterministic
// serialization: the same object graph always produces the same byte sequence.

inline std::uint32_t crc32(const std::uint8_t* data, std::size_t len, std::uint32_t seed) noexcept {
  std::uint32_t crc = seed;
  for (std::size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int k = 0; k < 8; ++k) {
      std::uint32_t mask = (crc & 1u) ? 0xFFFFFFFFu : 0u;
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return crc;
}

class ByteWriter {
 public:
  void u8(std::uint8_t v) { buf_.push_back(v); }
  void u16(std::uint16_t v) { buf_.push_back(static_cast<std::uint8_t>(v)); buf_.push_back(static_cast<std::uint8_t>(v >> 8)); }
  void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) buf_.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }
  void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) buf_.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }
  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
  void boolean(bool v) { u8(v ? 1u : 0u); }
  void str(const std::string& s) {
    u32(static_cast<std::uint32_t>(s.size()));
    buf_.insert(buf_.end(), s.begin(), s.end());
  }
  void bytes(const std::vector<std::uint8_t>& v) {
    u32(static_cast<std::uint32_t>(v.size()));
    buf_.insert(buf_.end(), v.begin(), v.end());
  }
  template <class T> void id(const T& t) { u64(t.value()); }
  void instant(Instant t) { i64(t.nanoseconds()); }
  void duration(Duration d) { i64(d.nanoseconds()); }
  void quant(const Quantity& q) { i64(q.value()); u8(static_cast<std::uint8_t>(q.unit())); }
  template <class E> void enu(E e) { u8(static_cast<std::uint8_t>(e)); }
  template <class T> void optId(const std::optional<T>& o) { if (o) { u8(1); id(*o); } else u8(0); }
  template <class G> void gen(const G& g) { u64(g.value()); }
  const std::vector<std::uint8_t>& data() const { return buf_; }
  template <class T> void optVal(const std::optional<T>& o, auto&& writeFn) {
    if (o) { u8(1); writeFn(*o); } else u8(0);
  }
 private:
  std::vector<std::uint8_t> buf_;
};

class ByteReader {
 public:
  explicit ByteReader(const std::vector<std::uint8_t>& data) : data_(data) {}
  explicit ByteReader(const std::uint8_t* data, std::size_t len) : data_(data, data + len) {}

  std::uint8_t u8() { require(1); return data_[pos_++]; }
  std::uint16_t u16() { require(2); std::uint16_t v = 0; for (int i = 0; i < 2; ++i) v |= static_cast<std::uint16_t>(data_[pos_++]) << (8 * i); return v; }
  std::uint32_t u32() { require(4); std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(data_[pos_++]) << (8 * i); return v; }
  std::uint64_t u64() { require(8); std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(data_[pos_++]) << (8 * i); return v; }
  std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
  bool boolean() { return u8() != 0; }
  std::string str() {
    std::uint32_t n = u32();
    if (n > kMaxString) throw_error(ErrorCode::CorruptData, "string length exceeds bound");
    require(n);
    // Avoid &data_[pos_] when n==0 and pos_==size(): vector::operator[] at size()
    // is a Debug-only bounds assert (and UB in any config).
    std::string s;
    if (n > 0) s.assign(reinterpret_cast<const char*>(&data_[pos_]), n);
    pos_ += n;
    return s;
  }
  void bytes(std::vector<std::uint8_t>& out) {
    std::uint32_t n = u32();
    if (n > kMaxString) throw_error(ErrorCode::CorruptData, "byte length exceeds bound");
    require(n);
    out.assign(data_.begin() + pos_, data_.begin() + pos_ + n);
    pos_ += n;
  }
  template <class T> T id() { return T(u64()); }
  Instant instant() { return Instant(i64()); }
  Duration duration() { return Duration(i64()); }
  Quantity quant() {
    std::int64_t v = i64();
    Unit u = static_cast<Unit>(u8());
    return Quantity(v, u);
  }
  template <class E> E enu() { return static_cast<E>(u8()); }
  template <class T> std::optional<T> optId() { if (u8()) return std::optional<T>(T(u64())); return std::nullopt; }
  template <class G> G gen() { return G(u64()); }
  bool empty() const { return pos_ >= data_.size(); }
  std::size_t remaining() const { return data_.size() - pos_; }
  template <class T> std::optional<T> optVal(auto&& readFn) { if (u8()) return readFn(); return std::nullopt; }

 private:
  static constexpr std::uint32_t kMaxString = 1u << 20;
  void require(std::size_t n) const {
    if (pos_ + n > data_.size()) throw_error(ErrorCode::CorruptData, "truncated binary payload");
  }
  const std::vector<std::uint8_t> data_;
  std::size_t pos_ = 0;
};

inline std::uint32_t crcOf(const ByteWriter& w) { return crc32(w.data().data(), w.data().size(), 0xFFFFFFFFu); }

}  // namespace detail
}  // namespace reservation_fabric
