#include <reservation_fabric/protocol.hpp>
#include <reservation_fabric/net.hpp>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#endif
#include <cstring>
#include <algorithm>

namespace reservation_fabric {
namespace protocol {

using detail::ByteReader;
using detail::ByteWriter;

const char* to_string(MessageType t) noexcept {
  switch (t) {
    case MessageType::Hello: return "Hello";
    case MessageType::Acknowledge: return "Acknowledge";
    case MessageType::ErrorResponse: return "ErrorResponse";
    case MessageType::Register: return "Register";
    case MessageType::PublishResource: return "PublishResource";
    case MessageType::PublishResult: return "PublishResult";
    case MessageType::Assess: return "Assess";
    case MessageType::AssessResult: return "AssessResult";
    case MessageType::RequestReservation: return "RequestReservation";
    case MessageType::ReservationResult: return "ReservationResult";
    case MessageType::Activate: return "Activate";
    case MessageType::ActivateResult: return "ActivateResult";
    case MessageType::Consume: return "Consume";
    case MessageType::ConsumeResult: return "ConsumeResult";
    case MessageType::Release: return "Release";
    case MessageType::ReleaseResult: return "ReleaseResult";
    case MessageType::Query: return "Query";
    case MessageType::QueryResult: return "QueryResult";
    case MessageType::Shutdown: return "Shutdown";
  }
  return "Unknown";
}

std::vector<std::uint8_t> encodeFrame(MessageType type, const std::vector<std::uint8_t>& payload) {
  if (payload.size() > kMaxFramePayload) throw_error(ErrorCode::ProtocolError, "frame payload exceeds bound");
  ByteWriter frame;
  frame.u32(kFrameMagic);
  frame.u8(kFrameVersion);
  frame.u8(static_cast<std::uint8_t>(type));
  frame.u32(static_cast<std::uint32_t>(payload.size()));
  // CRC over type ++ payload.
  std::vector<std::uint8_t> covered;
  covered.reserve(payload.size() + 1);
  covered.push_back(static_cast<std::uint8_t>(type));
  covered.insert(covered.end(), payload.begin(), payload.end());
  std::uint32_t crc = detail::crc32(covered.data(), covered.size(), 0xFFFFFFFFu);
  frame.u32(crc);
  for (auto b : payload) frame.u8(b);
  return frame.data();
}

int FrameDecoder::push(const std::uint8_t* data, std::size_t len, Frame& out) {
  buf_.insert(buf_.end(), data, data + len);
  for (;;) {
    if (buf_.size() < kHeaderSize) return 1;   // need more bytes
    ByteReader hdr(buf_);
    std::uint32_t magic = hdr.u32();
    std::uint8_t version = hdr.u8();
    std::uint8_t type = hdr.u8();
    std::uint32_t payloadLen = hdr.u32();
    std::uint32_t crc = hdr.u32();
    if (magic != kFrameMagic) return -1;
    if (version != kFrameVersion) return -1;
    if (payloadLen > kMaxFramePayload) return -1;
    if (buf_.size() < kHeaderSize + payloadLen) return 1;
    const std::uint8_t* payload = buf_.data() + kHeaderSize;
    std::vector<std::uint8_t> covered;
    covered.reserve(payloadLen + 1);
    covered.push_back(type);
    covered.insert(covered.end(), payload, payload + payloadLen);
    if (detail::crc32(covered.data(), covered.size(), 0xFFFFFFFFu) != crc) return -1;
    out.type = static_cast<MessageType>(type);
    out.payload.assign(payload, payload + payloadLen);
    buf_.erase(buf_.begin(), buf_.begin() + kHeaderSize + payloadLen);
    return 0;
  }
}

// ---- serialization of protocol payloads ----

void serInterval(ByteWriter& w, const Interval& i) { w.instant(i.start()); w.instant(i.end()); }
Interval desInterval(ByteReader& r) { Instant s = r.instant(), e = r.instant(); return Interval(s, e); }

template <class G> void serOptGen(ByteWriter& w, const std::optional<G>& o) { if (o) { w.u8(1); w.gen(*o); } else w.u8(0); }
template <class G> std::optional<G> desOptGen(ByteReader& r) { if (r.u8()) return std::optional<G>(r.gen<G>()); return std::nullopt; }

void serQuantity(ByteWriter& w, const Quantity& q) { w.quant(q); }
Quantity desQuantity(ByteReader& r) { return r.quant(); }

void serReq(ByteWriter& w, const ReservationRequest& req) {
  w.id(req.requestId); w.gen(req.requestGeneration);
  w.enu(req.resourceClass); w.enu(req.unit); w.quant(req.quantity); w.boolean(req.partialAllowed);
  if (req.minAcceptable) { w.u8(1); w.quant(*req.minAcceptable); } else w.u8(0);
  serInterval(w, req.window); w.enu(req.activationMode);
  if (req.durationAfterActivation) { w.u8(1); w.duration(*req.durationAfterActivation); } else w.u8(0);
  w.enu(req.strength); w.id(req.owner); w.gen(req.ownerGeneration);
  if (req.workload) { w.u8(1); w.id(*req.workload); } else w.u8(0);
  if (req.workloadGeneration) { w.u8(1); w.gen(*req.workloadGeneration); } else w.u8(0);
  if (req.execution) { w.u8(1); w.id(*req.execution); } else w.u8(0);
  if (req.executionGeneration) { w.u8(1); w.gen(*req.executionGeneration); } else w.u8(0);
  w.u32(req.capability.requiredCapabilityId); w.boolean(req.capability.capacityRequiresCapability);
  w.id(req.locality.preferredPool); w.id(req.locality.placement); w.enu(req.locality.preferredClass); w.i64(req.locality.localityWeight);
  w.u8(req.topology.topologyDomain); w.boolean(req.topology.requireLocalPlacement);
  w.boolean(req.compatibility.requireSameDriverGeneration);
  serOptGen(w, req.minResourceGeneration); w.gen(req.policyGeneration); w.gen(req.priorityGeneration);
  w.i64(req.priorityClass); w.boolean(req.preemptible); w.enu(req.exclusivity); w.enu(req.elasticity); w.enu(req.renewal); w.enu(req.transfer);
  w.i64(req.fragmentationTolerance); w.boolean(req.allOrNothing); w.id(req.poolConstraint);
  if (req.group) { w.u8(1); w.id(*req.group); } else w.u8(0);
  if (req.targetResource) { w.u8(1); w.id(*req.targetResource); } else w.u8(0);
  w.str(req.provenance);
}
ReservationRequest desReq(ByteReader& r) {
  ReservationRequest req;
  req.requestId = r.id<ReservationRequestId>(); req.requestGeneration = r.gen<ReservationRequestGeneration>();
  req.resourceClass = r.enu<ResourceClass>(); req.unit = r.enu<Unit>(); req.quantity = r.quant(); req.partialAllowed = r.boolean();
  if (r.u8()) req.minAcceptable = r.quant();
  req.window = desInterval(r); req.activationMode = r.enu<ActivationMode>();
  if (r.u8()) req.durationAfterActivation = r.duration();
  req.strength = r.enu<ReservationStrength>(); req.owner = r.id<OwnerId>(); req.ownerGeneration = r.gen<OwnerGeneration>();
  if (r.u8()) req.workload = r.id<WorkloadId>();
  if (r.u8()) req.workloadGeneration = r.gen<WorkloadGeneration>();
  if (r.u8()) req.execution = r.id<ExecutionId>();
  if (r.u8()) req.executionGeneration = r.gen<ExecutionGeneration>();
  req.capability.requiredCapabilityId = r.u32(); req.capability.capacityRequiresCapability = r.boolean();
  req.locality.preferredPool = r.id<ResourcePoolId>(); req.locality.placement = r.id<PlacementId>(); req.locality.preferredClass = r.enu<ResourceClass>(); req.locality.localityWeight = static_cast<std::int32_t>(r.i64());
  req.topology.topologyDomain = r.u8(); req.topology.requireLocalPlacement = r.boolean();
  req.compatibility.requireSameDriverGeneration = r.boolean();
  req.minResourceGeneration = desOptGen<ResourceGeneration>(r); req.policyGeneration = r.gen<PolicyGeneration>(); req.priorityGeneration = r.gen<PriorityGeneration>();
  req.priorityClass = static_cast<std::int32_t>(r.i64()); req.preemptible = r.boolean(); req.exclusivity = r.enu<ExclusivityMode>(); req.elasticity = r.enu<Elasticity>(); req.renewal = r.enu<RenewalPermission>(); req.transfer = r.enu<TransferPermission>();
  req.fragmentationTolerance = static_cast<std::int32_t>(r.i64()); req.allOrNothing = r.boolean(); req.poolConstraint = r.id<ResourcePoolId>();
  if (r.u8()) req.group = r.id<ReservationGroupId>();
  if (r.u8()) req.targetResource = r.id<ResourceId>();
  req.provenance = r.str();
  return req;
}

void serPublication(ByteWriter& w, const ResourcePublication& p) {
  w.id(p.worker); w.gen(p.workerBoot); w.id(p.id); w.gen(p.generation);
  w.enu(p.resourceClass); w.enu(p.unit); w.quant(p.totalCapacity);
  w.id(p.pool); w.gen(p.poolGeneration); w.gen(p.capabilities); w.gen(p.topology);
  w.id(p.contract); w.gen(p.contractGeneration);
  w.instant(p.availableFrom); w.duration(p.lifetime); w.u64(p.overbookRatio.basisPoints());
  w.u32(static_cast<std::uint32_t>(p.unavailable.size()));
  for (const auto& un : p.unavailable) { serInterval(w, un.interval); w.str(un.reason); }
}
ResourcePublication desPublication(ByteReader& r) {
  ResourcePublication p;
  p.worker = r.id<WorkerId>(); p.workerBoot = r.gen<WorkerBootId>(); p.id = r.id<ResourceId>(); p.generation = r.gen<ResourceGeneration>();
  p.resourceClass = r.enu<ResourceClass>(); p.unit = r.enu<Unit>(); p.totalCapacity = r.quant();
  p.pool = r.id<ResourcePoolId>(); p.poolGeneration = r.gen<ResourcePoolGeneration>(); p.capabilities = r.gen<CapabilityGeneration>(); p.topology = r.gen<TopologyGeneration>();
  p.contract = r.id<ResourceContractId>(); p.contractGeneration = r.gen<ResourceContractGeneration>();
  p.availableFrom = r.instant(); p.lifetime = r.duration(); p.overbookRatio = Ratio(static_cast<std::int64_t>(r.u64()));
  std::uint32_t n = r.u32();
  for (std::uint32_t i = 0; i < n; ++i) { UnavailableWindow un; un.interval = desInterval(r); un.reason = r.str(); p.unavailable.push_back(std::move(un)); }
  return p;
}

void serReport(ByteWriter& w, const FeasibilityReport& rep) {
  w.enu(rep.outcome); w.enu(rep.resourceClass); w.enu(rep.unit);
  w.quant(rep.requestedQuantity); w.quant(rep.admissibleQuantity);
  w.u8(rep.requestedInterval.isEmpty() ? 0 : 1);
  if (!rep.requestedInterval.isEmpty()) serInterval(w, rep.requestedInterval);
  w.id(rep.resource);
  w.gen(rep.currentResourceGeneration); w.gen(rep.staleResourceGeneration);
  w.gen(rep.policyGeneration); w.gen(rep.stalePolicyGeneration);
  w.gen(rep.currentEpoch); w.gen(rep.staleEpoch);
  w.u32(static_cast<std::uint32_t>(rep.conflictingReservations.size()));
  for (const auto& id : rep.conflictingReservations) w.id(id);
  w.u32(static_cast<std::uint32_t>(rep.bottleneckResources.size()));
  for (const auto& id : rep.bottleneckResources) w.id(id);
  w.quant(rep.availableAtBottleneck);
  w.u8(rep.conflictingInterval.isEmpty() ? 0 : 1);
  if (!rep.conflictingInterval.isEmpty()) serInterval(w, rep.conflictingInterval);
  w.boolean(rep.blockedBySoftOnly); w.boolean(rep.overbookEligible); w.boolean(rep.revalidationRequired);
  w.u32(static_cast<std::uint32_t>(rep.reasons.size()));
  for (const auto& s : rep.reasons) w.str(s);
}
FeasibilityReport desReport(ByteReader& r) {
  FeasibilityReport rep;
  rep.outcome = r.enu<Feasibility>(); rep.resourceClass = r.enu<ResourceClass>(); rep.unit = r.enu<Unit>();
  rep.requestedQuantity = r.quant(); rep.admissibleQuantity = r.quant();
  if (r.u8()) rep.requestedInterval = desInterval(r); else rep.requestedInterval = Interval();
  rep.resource = r.id<ResourceId>();
  rep.currentResourceGeneration = r.gen<ResourceGeneration>(); rep.staleResourceGeneration = r.gen<ResourceGeneration>();
  rep.policyGeneration = r.gen<PolicyGeneration>(); rep.stalePolicyGeneration = r.gen<PolicyGeneration>();
  rep.currentEpoch = r.gen<CoordinatorEpoch>(); rep.staleEpoch = r.gen<CoordinatorEpoch>();
  std::uint32_t a = r.u32(); for (std::uint32_t i = 0; i < a; ++i) rep.conflictingReservations.push_back(r.id<ReservationId>());
  std::uint32_t b = r.u32(); for (std::uint32_t i = 0; i < b; ++i) rep.bottleneckResources.push_back(r.id<ResourceId>());
  rep.availableAtBottleneck = r.quant();
  if (r.u8()) rep.conflictingInterval = desInterval(r);
  else rep.conflictingInterval = Interval();
  rep.blockedBySoftOnly = r.boolean(); rep.overbookEligible = r.boolean(); rep.revalidationRequired = r.boolean();
  std::uint32_t c = r.u32(); for (std::uint32_t i = 0; i < c; ++i) rep.reasons.push_back(r.str());
  return rep;
}

void serActivationEvidence(ByteWriter& w, const ActivationEvidence& e) { w.id(e.worker); w.gen(e.workerBoot); w.gen(e.activationGeneration); w.instant(e.at); w.u64(e.token); }
ActivationEvidence desActivationEvidence(ByteReader& r) { ActivationEvidence e; e.worker = r.id<WorkerId>(); e.workerBoot = r.gen<WorkerBootId>(); e.activationGeneration = r.gen<ActivationGeneration>(); e.at = r.instant(); e.token = r.u64(); return e; }

void serReservationResult(ByteWriter& w, const ReservationResult& rr) {
  w.boolean(rr.committed); w.boolean(rr.idempotent); w.id(rr.id); w.gen(rr.generation); w.str(rr.errorMessage); serReport(w, rr.feasibility);
}
ReservationResult desReservationResult(ByteReader& r) {
  ReservationResult rr; rr.committed = r.boolean(); rr.idempotent = r.boolean(); rr.id = r.id<ReservationId>(); rr.generation = r.gen<ReservationGeneration>(); rr.errorMessage = r.str(); rr.feasibility = desReport(r); return rr;
}
void serActivationResult(ByteWriter& w, const ActivationResult& ar) { w.boolean(ar.activated); w.boolean(ar.revalidationRequired); w.id(ar.id); w.gen(ar.generation); w.str(ar.errorMessage); }
ActivationResult desActivationResult(ByteReader& r) { ActivationResult ar; ar.activated = r.boolean(); ar.revalidationRequired = r.boolean(); ar.id = r.id<ReservationId>(); ar.generation = r.gen<ReservationGeneration>(); ar.errorMessage = r.str(); return ar; }
void serConsumeResult(ByteWriter& w, const ConsumeResult& cr) { w.boolean(cr.consumed); w.boolean(cr.idempotent); w.id(cr.id); w.gen(cr.generation); w.quant(cr.consumedQuantity); w.quant(cr.remaining); w.str(cr.errorMessage); }
ConsumeResult desConsumeResult(ByteReader& r) { ConsumeResult cr; cr.consumed = r.boolean(); cr.idempotent = r.boolean(); cr.id = r.id<ReservationId>(); cr.generation = r.gen<ReservationGeneration>(); cr.consumedQuantity = r.quant(); cr.remaining = r.quant(); cr.errorMessage = r.str(); return cr; }
void serReleaseResult(ByteWriter& w, const ReleaseResult& rr) { w.boolean(rr.released); w.boolean(rr.idempotent); w.id(rr.id); w.gen(rr.generation); w.str(rr.errorMessage); }
ReleaseResult desReleaseResult(ByteReader& r) { ReleaseResult rr; rr.released = r.boolean(); rr.idempotent = r.boolean(); rr.id = r.id<ReservationId>(); rr.generation = r.gen<ReservationGeneration>(); rr.errorMessage = r.str(); return rr; }

#ifdef _WIN32
bool netInit() { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }
void netCleanup() { WSACleanup(); }
SOCKET connectTo(const std::string& host, int port) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return INVALID_SOCKET;
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(static_cast<u_short>(port));
  inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
  if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { closesocket(s); return INVALID_SOCKET; }
  return s;
}
bool listenOn(int port, SOCKET& listener) {
  listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == INVALID_SOCKET) return false;
  BOOL reuse = TRUE; setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons(static_cast<u_short>(port));
  if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { closesocket(listener); return false; }
  if (listen(listener, SOMAXCONN) == SOCKET_ERROR) { closesocket(listener); return false; }
  return true;
}
SOCKET acceptConn(SOCKET listener, bool& ok) {
  sockaddr_in client{}; int len = sizeof(client);
  SOCKET s = accept(listener, reinterpret_cast<sockaddr*>(&client), &len);
  ok = s != INVALID_SOCKET; return s;
}
bool recvAll(SOCKET s, std::uint8_t* buf, std::size_t len) {
  std::size_t got = 0;
  while (got < len) {
    int n = recv(s, reinterpret_cast<char*>(buf + got), static_cast<int>(len - got), 0);
    if (n <= 0) return false;
    got += static_cast<std::size_t>(n);
  }
  return true;
}
bool sendAll(SOCKET s, const std::uint8_t* buf, std::size_t len) {
  std::size_t sent = 0;
  while (sent < len) {
    int n = send(s, reinterpret_cast<const char*>(buf + sent), static_cast<int>(len - sent), 0);
    if (n <= 0) return false;
    sent += static_cast<std::size_t>(n);
  }
  return true;
}
int recvFrame(SOCKET s, Frame& frame) {
  std::uint8_t hdr[kHeaderSize];
  if (!recvAll(s, hdr, kHeaderSize)) return 1;   // clean close
  ByteReader h(hdr, kHeaderSize);
  std::uint32_t magic = h.u32(); std::uint8_t version = h.u8(); std::uint8_t type = h.u8();
  std::uint32_t len = h.u32(); std::uint32_t crc = h.u32();
  if (magic != kFrameMagic || version != kFrameVersion || len > kMaxFramePayload) return -1;
  std::vector<std::uint8_t> payload(len);
  if (len && !recvAll(s, payload.data(), len)) return 1;
  std::vector<std::uint8_t> covered; covered.reserve(len + 1); covered.push_back(type); covered.insert(covered.end(), payload.begin(), payload.end());
  if (detail::crc32(covered.data(), covered.size(), 0xFFFFFFFFu) != crc) return -1;
  frame.type = static_cast<MessageType>(type); frame.payload = std::move(payload);
  return 0;
}
bool sendFrame(SOCKET s, MessageType type, const std::vector<std::uint8_t>& payload) {
  auto bytes = encodeFrame(type, payload);
  return sendAll(s, bytes.data(), bytes.size());
}
#endif

}  // namespace protocol
}  // namespace reservation_fabric