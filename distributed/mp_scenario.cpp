#include <reservation_fabric/protocol.hpp>
#include <reservation_fabric/net.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <stdexcept>
#include <windows.h>
#include <thread>
#include <chrono>

using namespace reservation_fabric;
using namespace reservation_fabric::protocol;
using namespace reservation_fabric::detail;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__); ++failures; } } while (0)

static SOCKET client = INVALID_SOCKET;
static void waitms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static bool spawn(const std::string& cmdline, HANDLE& h) {
  STARTUPINFOA si{}; si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  char* buf = _strdup(cmdline.c_str());
  BOOL ok = CreateProcessA(NULL, buf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  free(buf);
  if (!ok) return false;
  h = pi.hProcess; CloseHandle(pi.hThread);
  return true;
}
static void killProc(HANDLE h) { if (h) { TerminateProcess(h, 0); CloseHandle(h); } }

static Frame& exchange(MessageType t, const std::vector<std::uint8_t>& payload, Frame& resp) {
  if (!sendFrame(client, t, payload)) throw_error(ErrorCode::ProtocolError, "sendFrame failed");
  int rc = recvFrame(client, resp);
  if (rc != 0) throw_error(ErrorCode::ProtocolError, "peer closed frame channel");
  if (resp.type == MessageType::ErrorResponse) {
    ByteReader er(resp.payload);
    throw_error(ErrorCode::ProtocolError, "coordinator error: " + er.str());
  }
  return resp;
}

static ReservationRequest makeProtoReq(std::uint64_t reqId, std::int64_t qty, std::int64_t startNs, std::int64_t durNs, ResourceId target = ResourceId(0)) {
  ReservationRequest req;
  req.requestId = ReservationRequestId(reqId);
  req.requestGeneration = ReservationRequestGeneration(1);
  req.resourceClass = ResourceClass::AcceleratorCompute;
  req.unit = Unit::Count;
  req.quantity = Quantity::count(qty);
  req.window = Interval(Instant(startNs), Duration::nanoseconds(durNs));
  req.activationMode = ActivationMode::FixedInterval;
  req.strength = ReservationStrength::Hard;
  req.owner = OwnerId(1); req.ownerGeneration = OwnerGeneration(1);
  req.policyGeneration = PolicyGeneration(1); req.priorityGeneration = PriorityGeneration(1);
  if (target.valid()) req.targetResource = target;
  return req;
}

static ReservationResult sendRequest(const ReservationRequest& req) {
  ByteWriter b; serReq(b, req); Frame f;
  exchange(MessageType::RequestReservation, b.data(), f);
  ByteReader r(f.payload);
  return desReservationResult(r);
}
static ActivationResult sendActivate(ReservationId id, ReservationGeneration gen, WorkerId w, WorkerBootId boot) {
  ByteWriter b; b.id(id); b.gen(gen); ActivationEvidence e; e.worker = w; e.workerBoot = boot; e.activationGeneration = ActivationGeneration(1); e.at = Instant(100); serActivationEvidence(b, e); Frame f;
  exchange(MessageType::Activate, b.data(), f);
  ByteReader r(f.payload);
  return desActivationResult(r);
}
static ConsumeResult sendConsume(ReservationId id, ReservationGeneration gen, std::int64_t qty, WorkerId w, WorkerBootId boot, std::uint64_t cg) {
  ByteWriter b; b.id(id); b.gen(gen); b.i64(qty); b.u8(static_cast<std::uint8_t>(Unit::Count)); b.id(w); b.gen(boot); b.gen(ConsumptionGeneration(cg)); b.instant(Instant(100)); Frame f;
  exchange(MessageType::Consume, b.data(), f);
  ByteReader r(f.payload);
  return desConsumeResult(r);
}
static ReleaseResult sendRelease(ReservationId id, ReservationGeneration gen, WorkerId w, WorkerBootId boot) {
  ByteWriter b; b.id(id); b.gen(gen); b.id(w); b.gen(boot); b.gen(ReleaseGeneration(1)); b.instant(Instant(100)); Frame f;
  exchange(MessageType::Release, b.data(), f);
  ByteReader r(f.payload);
  return desReleaseResult(r);
}
static std::int64_t sendAccounting(ResourceId id, bool& consistent) {
  ByteWriter b; b.str("accounting"); b.id(id); Frame f;
  exchange(MessageType::Query, b.data(), f);
  ByteReader r(f.payload);
  std::int64_t committed = r.i64();
  std::int64_t headroom = r.i64();
  consistent = r.boolean();
  (void)headroom;
  return committed;
}
static std::uint64_t sendCount() {
  ByteWriter b; b.str("count"); Frame f;
  exchange(MessageType::Query, b.data(), f);
  ByteReader r(f.payload);
  return static_cast<std::uint64_t>(std::atoll(r.str().c_str()));
}
static void sendRevalidate(ResourceId id, WorkerId w, WorkerBootId boot, ResourceGeneration gen, std::int64_t cap) {
  ResourcePublication pub;
  pub.worker = w; pub.workerBoot = boot; pub.id = id; pub.generation = gen;
  pub.resourceClass = ResourceClass::AcceleratorCompute; pub.unit = Unit::Count; pub.totalCapacity = Quantity::count(cap);
  pub.pool = ResourcePoolId(1); pub.poolGeneration = ResourcePoolGeneration(1); pub.capabilities = CapabilityGeneration(1); pub.topology = TopologyGeneration(1);
  pub.contract = ResourceContractId(1); pub.contractGeneration = ResourceContractGeneration(1); pub.availableFrom = Instant(0); pub.lifetime = Duration::seconds(365 * 24 * 3600);
  ByteWriter b; serPublication(b, pub); Frame f;
  exchange(MessageType::Revalidate, b.data(), f);
}

static void runScenario(const std::string& coord, const std::string& worker, int port) {
  HANDLE coordH = INVALID_HANDLE_VALUE, waH = INVALID_HANDLE_VALUE, wbH = INVALID_HANDLE_VALUE, wa2H = INVALID_HANDLE_VALUE;

  std::fprintf(stderr, "spawning coordinator...\n");
  CHECK(spawn(coord + " " + std::to_string(port), coordH));
  waitms(800);
  std::fprintf(stderr, "spawning worker A and B...\n");
  CHECK(spawn(worker + " 127.0.0.1 " + std::to_string(port) + " 1 1 1 100 0", waH));
  CHECK(spawn(worker + " 127.0.0.1 " + std::to_string(port) + " 2 1 2 100 0", wbH));
  waitms(800);

  for (int attempt = 0; attempt < 20; ++attempt) {
    client = connectTo("127.0.0.1", port);
    if (client != INVALID_SOCKET) break;
    waitms(150);
  }
  CHECK(client != INVALID_SOCKET);
  { ByteWriter b; b.str("client"); b.id(WorkerId(9)); b.gen(WorkerBootId(1)); Frame f; exchange(MessageType::Hello, b.data(), f); }

  const std::int64_t startNs = 100;
  const std::int64_t durNs = 1000000000LL;

  auto rA = sendRequest(makeProtoReq(1, 60, startNs, durNs, ResourceId(1)));
  CHECK(rA.committed);
  auto rB50 = sendRequest(makeProtoReq(2, 50, startNs, durNs, ResourceId(1)));
  CHECK(!rB50.committed);
  auto rB40 = sendRequest(makeProtoReq(3, 40, startNs, durNs, ResourceId(1)));
  CHECK(rB40.committed);
  bool consistent = false;
  CHECK(sendAccounting(ResourceId(1), consistent) == 100);
  CHECK(consistent);
  auto rAdup = sendRequest(makeProtoReq(1, 60, startNs, durNs, ResourceId(1)));
  CHECK(!rAdup.committed);

  CHECK(sendActivate(rA.id, rA.generation, WorkerId(1), WorkerBootId(1)).activated);
  auto ca = sendConsume(rA.id, rA.generation, 25, WorkerId(1), WorkerBootId(1), 1);
  CHECK(ca.consumed);
  CHECK(sendRelease(rA.id, rA.generation, WorkerId(1), WorkerBootId(1)).released);
  CHECK(sendAccounting(ResourceId(1), consistent) == 40);
  CHECK(consistent);

  auto rC = sendRequest(makeProtoReq(4, 55, startNs, durNs, ResourceId(1)));
  CHECK(rC.committed);
  CHECK(sendAccounting(ResourceId(1), consistent) == 95);
  CHECK(consistent);

  CHECK(sendActivate(rB40.id, rB40.generation, WorkerId(1), WorkerBootId(1)).activated);
  CHECK(sendActivate(rC.id, rC.generation, WorkerId(1), WorkerBootId(1)).activated);
  CHECK(sendCount() == 3);

  std::fprintf(stderr, "killing worker A (real process kill)...\n");
  killProc(waH); waH = INVALID_HANDLE_VALUE;
  waitms(600);

  auto actStale = sendActivate(rB40.id, rB40.generation, WorkerId(1), WorkerBootId(1));
  CHECK(!actStale.activated);
  CHECK(actStale.revalidationRequired || !actStale.errorMessage.empty());

  std::fprintf(stderr, "restarting worker A' with fresh boot...\n");
  CHECK(spawn(worker + " 127.0.0.1 " + std::to_string(port) + " 1 2 1 100 0", wa2H));
  waitms(500);
  {
    ByteWriter b;
    ResourcePublication pub;
    pub.worker = WorkerId(1); pub.workerBoot = WorkerBootId(2); pub.id = ResourceId(1); pub.generation = ResourceGeneration(2);
    pub.resourceClass = ResourceClass::AcceleratorCompute; pub.unit = Unit::Count; pub.totalCapacity = Quantity::count(100);
    pub.pool = ResourcePoolId(1); pub.poolGeneration = ResourcePoolGeneration(1); pub.capabilities = CapabilityGeneration(1); pub.topology = TopologyGeneration(1);
    pub.contract = ResourceContractId(1); pub.contractGeneration = ResourceContractGeneration(1); pub.availableFrom = Instant(0); pub.lifetime = Duration::seconds(365 * 24 * 3600);
    serPublication(b, pub);
    Frame f; exchange(MessageType::PublishResource, b.data(), f);
  }
  sendRevalidate(ResourceId(1), WorkerId(1), WorkerBootId(2), ResourceGeneration(2), 100);

  CHECK(sendActivate(rB40.id, rB40.generation, WorkerId(1), WorkerBootId(2)).activated);
  CHECK(sendActivate(rC.id, rC.generation, WorkerId(1), WorkerBootId(2)).activated);

  killProc(wbH); killProc(wa2H);
  { ByteWriter b; Frame f; exchange(MessageType::Shutdown, b.data(), f); }
  waitms(300);
  killProc(coordH);
}

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: mp_scenario <coordinatorExe> <workerExe> [port]\n"); return 2; }
  int port = (argc >= 4) ? std::atoi(argv[3]) : 37911;
  std::string coord = argv[1];
  std::string worker = argv[2];
  if (!netInit()) { std::fprintf(stderr, "winsock init failed\n"); return 2; }
  try {
    runScenario(coord, worker, port);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "mp_scenario exception: %s\n", e.what());
    ++failures;
  }
  if (client != INVALID_SOCKET) closesocket(client);
  netCleanup();
  std::fprintf(stderr, "mp_scenario failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}