#include "impl.hpp"
#include <fstream>
#include <cstring>
#include <cstdio>


namespace reservation_fabric {
namespace {

using detail::ByteReader;
using detail::ByteWriter;

constexpr std::uint32_t kMagic = 0x52464142u;   // "RFAB"
constexpr std::uint8_t kVersion = 1;
constexpr std::uint32_t kMaxCollection = 1u << 24;

template <class G> void serGen(ByteWriter& w, const G& g) { w.gen(g); }
template <class G> G desGen(ByteReader& r) { return r.gen<G>(); }

void serOptInstant(ByteWriter& w, const std::optional<Instant>& o) {
  if (o) { w.u8(1); w.instant(*o); } else w.u8(0);
}
std::optional<Instant> desOptInstant(ByteReader& r) {
  if (r.u8() == 1) return std::optional<Instant>(r.instant());
  return std::nullopt;
}
template <class G> void serOptGen(ByteWriter& w, const std::optional<G>& o) {
  if (o) { w.u8(1); w.gen(*o); } else w.u8(0);
}
template <class G> std::optional<G> desOptGen(ByteReader& r) {
  if (r.u8() == 1) return std::optional<G>(r.gen<G>());
  return std::nullopt;
}

bool validLifecycle(std::uint8_t v) { return v >= 1 && v <= static_cast<std::uint8_t>(Lifecycle::Retired); }
bool validStrength(std::uint8_t v) { return v >= 1 && v <= static_cast<std::uint8_t>(ReservationStrength::Unknown); }
bool validHoldState(std::uint8_t v) { return v >= 1 && v <= static_cast<std::uint8_t>(HoldState::Committed); }
bool validSetTx(std::uint8_t v) { return v >= 1 && v <= static_cast<std::uint8_t>(SetTransactionState::Persisted); }
bool validResourceClass(std::uint8_t v) { return v >= 1 && v <= static_cast<std::uint8_t>(ResourceClass::Unknown); }
bool validUnit(std::uint8_t v) { return v >= 1 && v <= static_cast<std::uint8_t>(Unit::Hertz); }

void serResource(ByteWriter& w, const Resource& res, const CapacityEnvelope& env) {
  w.id(res.id); w.gen(res.generation); w.enu(res.resourceClass); w.enu(res.unit);
  w.quant(res.totalCapacity); w.id(res.pool); w.gen(res.poolGeneration);
  w.id(res.owner); w.gen(res.ownerBoot); w.gen(res.capabilities); w.gen(res.topology);
  w.id(res.contract); w.gen(res.contractGeneration); w.boolean(res.available);
  w.instant(env.origin()); w.duration(env.horizon()); w.u64(env.ratio().basisPoints());
}
Resource desResource(ByteReader& r, Resource& res, Instant& origin, Duration& horizon, std::int64_t& ratioBp) {
  res.id = r.id<ResourceId>(); res.generation = r.gen<ResourceGeneration>();
  res.resourceClass = r.enu<ResourceClass>(); res.unit = r.enu<Unit>();
  res.totalCapacity = r.quant(); res.pool = r.id<ResourcePoolId>(); res.poolGeneration = r.gen<ResourcePoolGeneration>();
  res.owner = r.id<WorkerId>(); res.ownerBoot = r.gen<WorkerBootId>(); res.capabilities = r.gen<CapabilityGeneration>();
  res.topology = r.gen<TopologyGeneration>(); res.contract = r.id<ResourceContractId>(); res.contractGeneration = r.gen<ResourceContractGeneration>();
  res.available = r.boolean();
  origin = r.instant(); horizon = r.duration(); ratioBp = static_cast<std::int64_t>(r.u64());
  return res;
}

void serReservation(ByteWriter& w, const Reservation& r) {
  w.id(r.id); w.gen(r.generation); w.id(r.setId); w.id(r.groupId);
  w.enu(r.strength); w.enu(r.resourceClass); w.enu(r.unit);
  w.id(r.resource); serOptGen(w, r.boundResourceGeneration);
  w.quant(r.quantity); w.quant(r.activated); w.quant(r.consumed);
  w.instant(r.window.start()); w.instant(r.window.end()); w.enu(r.activationMode); w.enu(r.lifecycle);
  w.id(r.owner); w.gen(r.ownerGeneration);
  serOptGen(w, r.workload); serOptGen(w, r.workloadGeneration); serOptGen(w, r.execution); serOptGen(w, r.executionGeneration);
  w.u32(r.capability.requiredCapabilityId); w.boolean(r.capability.capacityRequiresCapability);
  w.id(r.locality.preferredPool); w.id(r.locality.placement); w.enu(r.locality.preferredClass); w.i64(r.locality.localityWeight);
  w.u8(r.topology.topologyDomain); w.boolean(r.topology.requireLocalPlacement);
  w.boolean(r.compatibility.requireSameDriverGeneration);
  w.gen(r.policyGeneration); w.gen(r.priorityGeneration); w.i64(r.priorityClass);
  w.boolean(r.preemptible); w.enu(r.exclusivity); w.enu(r.elasticity); w.enu(r.renewal); w.enu(r.transfer);
  w.i64(r.fragmentationTolerance); w.boolean(r.allOrNothing); w.str(r.provenance);
  serOptGen(w, r.activationGeneration); serOptGen(w, r.consumptionGeneration); serOptGen(w, r.releaseGeneration);
  serOptGen(w, r.transferGeneration); serOptGen(w, r.recoveryGeneration);
  w.boolean(r.contributesEnvelope); w.boolean(r.persisted);
  serOptInstant(w, r.activatedAt); serOptInstant(w, r.releasedAt);
}
Reservation desReservation(ByteReader& r) {
  Reservation res;
  res.id = r.id<ReservationId>(); res.generation = r.gen<ReservationGeneration>();
  res.setId = r.id<ReservationSetId>(); res.groupId = r.id<ReservationGroupId>();
  res.strength = r.enu<ReservationStrength>(); res.resourceClass = r.enu<ResourceClass>(); res.unit = r.enu<Unit>();
  res.resource = r.id<ResourceId>(); res.boundResourceGeneration = desOptGen<ResourceGeneration>(r);
  res.quantity = r.quant(); res.activated = r.quant(); res.consumed = r.quant();
  Instant s = r.instant(), e = r.instant(); res.window = Interval(s, e);
  res.activationMode = r.enu<ActivationMode>(); res.lifecycle = r.enu<Lifecycle>();
  res.owner = r.id<OwnerId>(); res.ownerGeneration = r.gen<OwnerGeneration>();
  res.workload = desOptGen<WorkloadId>(r); res.workloadGeneration = desOptGen<WorkloadGeneration>(r);
  res.execution = desOptGen<ExecutionId>(r); res.executionGeneration = desOptGen<ExecutionGeneration>(r);
  res.capability.requiredCapabilityId = r.u32(); res.capability.capacityRequiresCapability = r.boolean();
  res.locality.preferredPool = r.id<ResourcePoolId>(); res.locality.placement = r.id<PlacementId>();
  res.locality.preferredClass = r.enu<ResourceClass>(); res.locality.localityWeight = static_cast<std::int32_t>(r.i64());
  res.topology.topologyDomain = r.u8(); res.topology.requireLocalPlacement = r.boolean();
  res.compatibility.requireSameDriverGeneration = r.boolean();
  res.policyGeneration = r.gen<PolicyGeneration>(); res.priorityGeneration = r.gen<PriorityGeneration>();
  res.priorityClass = static_cast<std::int32_t>(r.i64());
  res.preemptible = r.boolean(); res.exclusivity = r.enu<ExclusivityMode>(); res.elasticity = r.enu<Elasticity>();
  res.renewal = r.enu<RenewalPermission>(); res.transfer = r.enu<TransferPermission>();
  res.fragmentationTolerance = static_cast<std::int32_t>(r.i64()); res.allOrNothing = r.boolean(); res.provenance = r.str();
  res.activationGeneration = desOptGen<ActivationGeneration>(r); res.consumptionGeneration = desOptGen<ConsumptionGeneration>(r);
  res.releaseGeneration = desOptGen<ReleaseGeneration>(r); res.transferGeneration = desOptGen<TransferGeneration>(r);
  res.recoveryGeneration = desOptGen<RecoveryGeneration>(r);
  res.contributesEnvelope = r.boolean(); res.persisted = r.boolean();
  res.activatedAt = desOptInstant(r); res.releasedAt = desOptInstant(r);
  return res;
}

void serHold(ByteWriter& w, const Hold& h) {
  w.id(h.spec.holdId); w.gen(h.spec.holdGeneration); w.id(h.spec.owner); w.gen(h.spec.ownerGeneration);
  w.enu(h.spec.resourceClass); w.enu(h.spec.unit); w.quant(h.spec.quantity);
  w.instant(h.spec.window.start()); w.instant(h.spec.window.end()); w.gen(h.spec.policyGeneration);
  w.boolean(h.spec.commitAuthority); w.str(h.spec.provenance);
  w.enu(h.state); w.id(h.resource); w.boolean(h.appliedToEnvelope); w.instant(h.expiry); w.boolean(h.persisted);
}
Hold desHold(ByteReader& r) {
  Hold h;
  h.spec.holdId = r.id<ReservationHoldId>(); h.spec.holdGeneration = r.gen<ReservationHoldGeneration>();
  h.spec.owner = r.id<OwnerId>(); h.spec.ownerGeneration = r.gen<OwnerGeneration>();
  h.spec.resourceClass = r.enu<ResourceClass>(); h.spec.unit = r.enu<Unit>(); h.spec.quantity = r.quant();
  Instant s = r.instant(), e = r.instant(); h.spec.window = Interval(s, e);
  h.spec.policyGeneration = r.gen<PolicyGeneration>(); h.spec.commitAuthority = r.boolean(); h.spec.provenance = r.str();
  h.state = r.enu<HoldState>(); h.resource = r.id<ResourceId>(); h.appliedToEnvelope = r.boolean();
  h.expiry = r.instant(); h.persisted = r.boolean();
  return h;
}

void serSet(ByteWriter& w, const ReservationSet& s) {
  w.id(s.id); w.gen(s.generation); w.id(s.groupId); w.id(s.owner); w.gen(s.ownerGeneration);
  w.boolean(s.allOrNothing); w.enu(s.tx); w.boolean(s.committed); w.boolean(s.rolledBack); w.boolean(s.persisted);
  w.u32(static_cast<std::uint32_t>(s.members.size()));
  for (const auto& m : s.members) w.id(m);
}
ReservationSet desSet(ByteReader& r) {
  ReservationSet s;
  s.id = r.id<ReservationSetId>(); s.generation = r.gen<ReservationSetGeneration>(); s.groupId = r.id<ReservationGroupId>();
  s.owner = r.id<OwnerId>(); s.ownerGeneration = r.gen<OwnerGeneration>();
  s.allOrNothing = r.boolean(); s.tx = r.enu<SetTransactionState>(); s.committed = r.boolean(); s.rolledBack = r.boolean(); s.persisted = r.boolean();
  std::uint32_t n = r.u32(); if (n > kMaxCollection) throw_error(ErrorCode::CorruptData, "set member count exceeds bound");
  for (std::uint32_t i = 0; i < n; ++i) s.members.push_back(r.id<ReservationId>());
  return s;
}

// Re-apply every contribution to a freshly-created empty envelope and mark
// dynamic evidence conservative after restart.
void reapplyAfterLoad(ReservationFabric::Impl& d) {
  for (auto& [rid, r] : d.reservations) {
    auto eit = d.envelopes.find(r.resource);
    if (eit == d.envelopes.end()) continue;
    CapacityEnvelope& e = eit->second;
    if (lifecycle_holds_capacity(r.lifecycle)) {
      if (detail::isHard(r.strength)) e.addCommittedHard(r.window, r.quantity.value());
      else e.addCommittedSoft(r.window, r.quantity.value());
      r.contributesEnvelope = true;
    } else {
      r.contributesEnvelope = false;
    }
    // Never restore Active instantly after restart: dynamic evidence must be
    // revalidated by a fresh worker publication.
    if (r.lifecycle == Lifecycle::Active || r.lifecycle == Lifecycle::PartiallyConsumed || r.lifecycle == Lifecycle::Consumed) {
      r.lifecycle = Lifecycle::RevalidationRequired;
    }
  }
  for (auto& [hid, h] : d.holds) {
    auto eit = d.envelopes.find(h.resource);
    if (eit != d.envelopes.end() && h.appliedToEnvelope && h.state == HoldState::Acquired) {
      eit->second.addHeld(h.spec.window, h.spec.quantity.value());
    }
  }
}

}  // namespace

void ReservationFabric::save(const std::string& path) const {
  std::unique_lock<std::shared_mutex> l(impl_->lock);
  ByteWriter b;
  b.u64(impl_->epoch.value()); b.u64(impl_->authority.value());
  b.u64(impl_->nextReservationId); b.u64(impl_->nextHoldId); b.u64(impl_->nextSetId);

  b.u32(static_cast<std::uint32_t>(impl_->resources.size()));
  for (const auto& [id, res] : impl_->resources) {
    CapacityEnvelope* e = impl_->envelopeLocked(id);
    serResource(b, res, e ? *e : CapacityEnvelope(res.unit, res.totalCapacity, Ratio::one()));
  }
  b.u32(static_cast<std::uint32_t>(impl_->workerBoots.size()));
  for (const auto& [wid, boot] : impl_->workerBoots) { b.id(wid); b.gen(boot); }
  b.u32(static_cast<std::uint32_t>(impl_->requestToReservation.size()));
  for (const auto& [rid, res] : impl_->requestToReservation) { b.id(rid); b.id(res); }
  b.u32(static_cast<std::uint32_t>(impl_->reservations.size()));
  for (const auto& [id, r] : impl_->reservations) serReservation(b, r);
  b.u32(static_cast<std::uint32_t>(impl_->currentGen.size()));
  for (const auto& [id, g] : impl_->currentGen) { b.id(id); b.gen(g); }
  b.u32(static_cast<std::uint32_t>(impl_->history.size()));
  for (const auto& [id, hist] : impl_->history) {
    b.id(id);
    b.u32(static_cast<std::uint32_t>(hist.size()));
    for (const auto& r : hist) serReservation(b, r);
  }
  b.u32(static_cast<std::uint32_t>(impl_->holds.size()));
  for (const auto& [hid, h] : impl_->holds) serHold(b, h);
  b.u32(static_cast<std::uint32_t>(impl_->sets.size()));
  for (const auto& [sid, s] : impl_->sets) serSet(b, s);

  std::uint32_t crc = detail::crc32(b.data().data(), b.data().size(), 0xFFFFFFFFu);
  ByteWriter file;
  file.u32(kMagic); file.u8(kVersion); file.u32(static_cast<std::uint32_t>(b.data().size())); file.u32(crc);
  for (auto byte : b.data()) file.u8(byte);

  std::ofstream os(path, std::ios::binary);
  if (!os) throw_error(ErrorCode::PersistenceFailed, "cannot open file for write");
  os.write(reinterpret_cast<const char*>(file.data().data()), static_cast<std::streamsize>(file.data().size()));
  if (!os) throw_error(ErrorCode::PersistenceFailed, "write failed");
}

std::shared_ptr<ReservationFabric> ReservationFabric::load(const std::string& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is) throw_error(ErrorCode::PersistenceFailed, "cannot open file for read");
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
  if (bytes.size() < 13) throw_error(ErrorCode::CorruptData, "persisted file too small");

  ByteReader r(bytes);
  std::uint32_t magic = r.u32();
  if (magic != kMagic) throw_error(ErrorCode::CorruptData, "bad magic");
  std::uint8_t version = r.u8();
  if (version != kVersion) throw_error(ErrorCode::CorruptData, "unsupported version");
  std::uint32_t len = r.u32();
  std::uint32_t crc = r.u32();
  if (len != r.remaining() || r.remaining() < 3) {
    if (len > r.remaining()) throw_error(ErrorCode::CorruptData, "truncated payload");
    throw_error(ErrorCode::CorruptData, "trailing garbage");
  }
  // Header is magic(4) + version(1) + len(4) + crc(4) = 13 bytes.
  std::vector<std::uint8_t> payload(bytes.begin() + 13, bytes.end());
  if (detail::crc32(payload.data(), payload.size(), 0xFFFFFFFFu) != crc) throw_error(ErrorCode::CorruptData, "checksum mismatch");

  auto rf = std::make_shared<ReservationFabric>();
  ByteReader p(payload);
  rf->impl_->epoch = CoordinatorEpoch(p.u64());
  rf->impl_->authority = AuthorityGeneration(p.u64());
  rf->impl_->nextReservationId = p.u64();
  rf->impl_->nextHoldId = p.u64();
  rf->impl_->nextSetId = p.u64();

  std::uint32_t nRes = p.u32();
  if (nRes > kMaxCollection) throw_error(ErrorCode::CorruptData, "resource count exceeds bound");
  for (std::uint32_t i = 0; i < nRes; ++i) {
    Resource res; Instant origin; Duration horizon; std::int64_t ratioBp = 10000;
    desResource(p, res, origin, horizon, ratioBp);
    CapacityEnvelope env(res.unit, res.totalCapacity, Ratio(ratioBp));
    if (!env.init(origin, horizon, res.totalCapacity, Ratio(ratioBp))) throw_error(ErrorCode::CorruptData, "envelope init failed");
    rf->impl_->resources[res.id] = res;
    rf->impl_->envelopes[res.id] = std::move(env);
  }
  std::uint32_t nBoots = p.u32();
  if (nBoots > kMaxCollection) throw_error(ErrorCode::CorruptData, "boot count exceeds bound");
  for (std::uint32_t i = 0; i < nBoots; ++i) { auto wid = p.id<WorkerId>(); auto boot = p.gen<WorkerBootId>(); rf->impl_->workerBoots[wid] = boot; }
  std::uint32_t nReq = p.u32();
  if (nReq > kMaxCollection) throw_error(ErrorCode::CorruptData, "request map exceeds bound");
  for (std::uint32_t i = 0; i < nReq; ++i) { auto rid = p.id<ReservationRequestId>(); auto rid2 = p.id<ReservationId>(); rf->impl_->requestToReservation[rid] = rid2; }
  std::uint32_t nResv = p.u32();
  if (nResv > kMaxCollection) throw_error(ErrorCode::CorruptData, "reservation count exceeds bound");
  for (std::uint32_t i = 0; i < nResv; ++i) { Reservation rv = desReservation(p); rf->impl_->reservations[rv.id] = rv; }
  std::uint32_t nCur = p.u32();
  if (nCur > kMaxCollection) throw_error(ErrorCode::CorruptData, "currentGen count exceeds bound");
  for (std::uint32_t i = 0; i < nCur; ++i) { auto id = p.id<ReservationId>(); auto g = p.gen<ReservationGeneration>(); rf->impl_->currentGen[id] = g; }
  std::uint32_t nHist = p.u32();
  if (nHist > kMaxCollection) throw_error(ErrorCode::CorruptData, "history count exceeds bound");
  for (std::uint32_t i = 0; i < nHist; ++i) {
    auto id = p.id<ReservationId>();
    std::uint32_t cnt = p.u32();
    if (cnt > kMaxCollection) throw_error(ErrorCode::CorruptData, "history size exceeds bound");
    for (std::uint32_t j = 0; j < cnt; ++j) rf->impl_->history[id].push_back(desReservation(p));
  }
  std::uint32_t nHolds = p.u32();
  if (nHolds > kMaxCollection) throw_error(ErrorCode::CorruptData, "hold count exceeds bound");
  for (std::uint32_t i = 0; i < nHolds; ++i) { Hold h = desHold(p); rf->impl_->holds[h.spec.holdId] = h; }
  std::uint32_t nSets = p.u32();
  if (nSets > kMaxCollection) throw_error(ErrorCode::CorruptData, "set count exceeds bound");
  for (std::uint32_t i = 0; i < nSets; ++i) { ReservationSet s = desSet(p); rf->impl_->sets[s.id] = s; }

  if (!p.empty()) throw_error(ErrorCode::CorruptData, "unparsed trailing data");

  reapplyAfterLoad(*rf->impl_);
  return rf;
}

}  // namespace reservation_fabric