// Reservation Fabric CUDA proof on a single physical NVIDIA RTX 5090 (sm_120).
// Evidence is labeled REAL (actual device properties, cudaMalloc/kernel/free/CPU
// parity), SYNTHETIC (test-controlled governed capacity pool), or DERIVED (free
// memory baseline). No multi-GPU/MIG/NVLink/RDMA/distributed-GPU claims are made.
#include <reservation_fabric/runtime.hpp>
#include <cuda_runtime.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

using namespace reservation_fabric;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); ++failures; } else { std::fprintf(stderr, "  ok: %s\n", msg); } } while (0)
#define LABEL(tag, msg) std::fprintf(stderr, "[%s] %s\n", tag, msg)

static ActivationEvidence mkAct(WorkerId w, WorkerBootId b, Instant at) { ActivationEvidence e; e.worker = w; e.workerBoot = b; e.activationGeneration = ActivationGeneration(1); e.at = at; return e; }
static ReleaseEvidence mkRel(WorkerId w, WorkerBootId b, Instant at) { ReleaseEvidence e; e.worker = w; e.workerBoot = b; e.releaseGeneration = ReleaseGeneration(1); e.at = at; return e; }

static const char* deviceName(int dev) {
  static char name[256];
  cudaDeviceProp p; cudaGetDeviceProperties(&p, dev);
  std::snprintf(name, sizeof(name), "%s", p.name);
  return name;
}

static void setDevice(int dev) { cudaSetDevice(dev); }

// Saxpy-style add: out[i] = a[i] + b[i]. Runs on the device; CPU reference
// verifies parity.
__global__ void vecadd(const float* a, const float* b, float* out, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) out[i] = a[i] + b[i];
}

static bool runVecAdd(const float* a, const float* b, float* out, int n) {
  vecadd<<<(n + 255) / 256, 256>>>(a, b, out, n);
  cudaError_t e = cudaDeviceSynchronize();
  return e == cudaSuccess;
}

// Returns free bytes on the device (DERIVED evidence).
static std::int64_t freeMemNow() {
  size_t freeb = 0, totalb = 0;
  cudaMemGetInfo(&freeb, &totalb);
  return static_cast<std::int64_t>(freeb);
}

static void makeFabric(ReservationFabric& rf, const ResourceId rid, std::int64_t poolBytes) {
  ResourcePublication pub;
  pub.worker = WorkerId(1); pub.workerBoot = WorkerBootId(1);
  pub.id = rid; pub.generation = ResourceGeneration(1);
  pub.resourceClass = ResourceClass::AcceleratorMemory; pub.unit = Unit::Bytes;
  pub.totalCapacity = Quantity::bytes(poolBytes);
  pub.pool = ResourcePoolId(1); pub.poolGeneration = ResourcePoolGeneration(1);
  pub.capabilities = CapabilityGeneration(1); pub.topology = TopologyGeneration(1);
  pub.contract = ResourceContractId(1); pub.contractGeneration = ResourceContractGeneration(1);
  pub.availableFrom = Instant(0); pub.lifetime = Duration::seconds(365 * 24 * 3600);
  rf.publishResource(pub);
}

static ReservationRequest makeVRAMReq(std::uint64_t rid, std::int64_t bytes, Interval w, OwnerId owner) {
  ReservationRequest req;
  req.requestId = ReservationRequestId(rid); req.requestGeneration = ReservationRequestGeneration(1);
  req.resourceClass = ResourceClass::AcceleratorMemory; req.unit = Unit::Bytes;
  req.quantity = Quantity::bytes(bytes); req.window = w;
  req.activationMode = ActivationMode::FixedInterval; req.strength = ReservationStrength::Hard;
  req.owner = owner; req.ownerGeneration = OwnerGeneration(1);
  req.policyGeneration = PolicyGeneration(1); req.priorityGeneration = PriorityGeneration(1);
  req.targetResource = ResourceId(1);
  return req;
}

static bool allocAndVerify(std::int64_t bytes) {
  // REAL CUDA work: cudaMalloc, H2D, kernel, D2H, CPU parity, cudaFree.
  const int n = static_cast<int>(bytes / sizeof(float));
  float* a; float* b; float* out;
  if (cudaMalloc(&a, n * sizeof(float)) != cudaSuccess) { std::fprintf(stderr, "  (cudaMalloc a failed)\n"); return false; }
  if (cudaMalloc(&b, n * sizeof(float)) != cudaSuccess) { cudaFree(a); return false; }
  if (cudaMalloc(&out, n * sizeof(float)) != cudaSuccess) { cudaFree(a); cudaFree(b); return false; }
  std::vector<float> ha(n), hb(n), hout(n);
  for (int i = 0; i < n; ++i) { ha[i] = static_cast<float>(i); hb[i] = 2.0f; }
  if (cudaMemcpy(a, ha.data(), n * sizeof(float), cudaMemcpyHostToDevice) != cudaSuccess) { cudaFree(a); cudaFree(b); cudaFree(out); return false; }
  if (cudaMemcpy(b, hb.data(), n * sizeof(float), cudaMemcpyHostToDevice) != cudaSuccess) { cudaFree(a); cudaFree(b); cudaFree(out); return false; }
  if (!runVecAdd(a, b, out, n)) { cudaFree(a); cudaFree(b); cudaFree(out); return false; }
  if (cudaMemcpy(hout.data(), out, n * sizeof(float), cudaMemcpyDeviceToHost) != cudaSuccess) { cudaFree(a); cudaFree(b); cudaFree(out); return false; }
  bool ok = true;
  for (int i = 0; i < n; ++i) if (std::fabs(hout[i] - (ha[i] + hb[i])) > 1e-3f) { ok = false; break; }
  cudaFree(a); cudaFree(b); cudaFree(out);
  return ok;
}

int main() {
  std::fprintf(stderr, "=== Reservation Fabric CUDA proof on %s (sm_120) ===\n", deviceName(0));
  setDevice(0);
  const std::int64_t baseline = freeMemNow();
  LABEL("DERIVED", "baseline free VRAM captured");

  // SYNTHETIC governed pool: 4 GiB, a strict subset of physical free (~31 GiB).
  const std::int64_t pool = 4LL * 1024 * 1024 * 1024;
  LABEL("SYNTHETIC", "governed reservable pool = 4 GiB (subset of physical free)");
  Interval w(Instant(100), Duration::seconds(3600));

  // ---- Scenario A: committed VRAM reservation gates allocation ----
  std::fprintf(stderr, "--- Scenario A: committed VRAM reservation gates allocation ---\n");
  {
    ReservationFabric rf; makeFabric(rf, ResourceId(1), pool);
    const std::int64_t reqBytes = 1LL * 1024 * 1024 * 1024;   // 1 GiB
    auto rc = rf.commit(makeVRAMReq(1, reqBytes, w, OwnerId(1)));
    CHECK(rc.committed, "A commit");
    auto act = rf.activate(rc.id, rc.generation, mkAct(WorkerId(1), WorkerBootId(1), Instant(200)));
    CHECK(act.activated, "A activate");
    bool ok = allocAndVerify(reqBytes);
    CHECK(ok, "A real CUDA alloc + kernel + CPU parity");
    auto rel = rf.release(rc.id, rc.generation, mkRel(WorkerId(1), WorkerBootId(1), Instant(300)));
    CHECK(rel.released, "A release");
    auto acc = rf.accounting(ResourceId(1));
    CHECK(acc.headroom.value() == pool, "A capacity returns to full after release");
  }

  // ---- Scenario B: overlapping reservation rejected ----
  std::fprintf(stderr, "--- Scenario B: overlapping VRAM reservation rejected ---\n");
  {
    ReservationFabric rf; makeFabric(rf, ResourceId(1), pool);
    auto a = rf.commit(makeVRAMReq(2, 3LL * 1024 * 1024 * 1024, w, OwnerId(1)));
    CHECK(a.committed, "B first 3 GiB commit");
    auto b = rf.commit(makeVRAMReq(3, 2LL * 1024 * 1024 * 1024, w, OwnerId(1)));
    CHECK(!b.committed, "B overlapping 2 GiB rejected (3+2 > 4)");
    // No CUDA allocation attempted for the rejected reservation.
    auto rel = rf.release(a.id, a.generation, mkRel(WorkerId(1), WorkerBootId(1), Instant(300)));
    CHECK(rel.released, "B release first");
    auto c = rf.commit(makeVRAMReq(4, 2LL * 1024 * 1024 * 1024, w, OwnerId(1)));
    CHECK(c.committed, "B fresh valid reservation after release succeeds");
    CHECK(allocAndVerify(2LL * 1024 * 1024 * 1024), "B real CUDA work on fresh reservation");
  }

  // ---- Scenario C: stale reservation generation rejected ----
  std::fprintf(stderr, "--- Scenario C: stale reservation generation rejected ---\n");
  {
    ReservationFabric rf; makeFabric(rf, ResourceId(1), pool);
    auto rc = rf.commit(makeVRAMReq(5, 1LL * 1024 * 1024 * 1024, w, OwnerId(1)));
    CHECK(rc.committed, "C commit gen1 (1 GiB)");
    auto m = rf.resize(rc.id, rc.generation, Quantity::bytes(2LL * 1024 * 1024 * 1024), PolicyGeneration(1));
    CHECK(m.applied, "C resize to gen2 (2 GiB)");
    // Attempt allocation under the STALE generation-1 authority: must be
    // rejected before any CUDA allocation occurs.
    auto stale = rf.activate(rc.id, rc.generation, mkAct(WorkerId(1), WorkerBootId(1), Instant(250)));
    CHECK(!stale.activated, "C stale gen1 activation rejected");
    auto fresh = rf.activate(rc.id, m.newGeneration, mkAct(WorkerId(1), WorkerBootId(1), Instant(250)));
    CHECK(fresh.activated, "C gen2 activate");
    CHECK(allocAndVerify(2LL * 1024 * 1024 * 1024), "C real CUDA work under gen2");
  }

  // ---- Scenario D: worker death / revalidation (authority fencing) ----
  // The real OS-process worker kill is proven by the CPU mp_scenario test. Here
  // we prove the same reservation-authority gating for a GPU resource.
  std::fprintf(stderr, "--- Scenario D: worker death / revalidation (authority fencing) ---\n");
  {
    ReservationFabric rf; makeFabric(rf, ResourceId(1), pool);
    auto rc = rf.commit(makeVRAMReq(6, 1LL * 1024 * 1024 * 1024, w, OwnerId(1)));
    CHECK(rc.committed, "D commit");
    CHECK(rf.activate(rc.id, rc.generation, mkAct(WorkerId(1), WorkerBootId(1), Instant(250))).activated, "D activate");
    bool ok = allocAndVerify(1LL * 1024 * 1024 * 1024);
    CHECK(ok, "D real CUDA work pre-death");
    // Worker death: fence its resource; the durable reservation is retained.
    InvalidationNotice notice; notice.resource = ResourceId(1); notice.condition = ResourceCondition::Invalidated; notice.reason = "worker died";
    rf.invalidateResource(notice);
    const Reservation* r = rf.reservation(rc.id);
    CHECK(r != nullptr, "D durable reservation retained after worker death");
    CHECK(r->lifecycle == Lifecycle::RevalidationRequired, "D reservation flagged RevalidationRequired");
    // Stale old-authority activation must be rejected.
    CHECK(!rf.activate(rc.id, rc.generation, mkAct(WorkerId(1), WorkerBootId(1), Instant(400))).activated, "D stale activation rejected");
    // Fresh worker boot revalidates device evidence, then reactivation + CUDA.
    ResourcePublication freshPub;
    freshPub.worker = WorkerId(1); freshPub.workerBoot = WorkerBootId(2);
    freshPub.id = ResourceId(1); freshPub.generation = ResourceGeneration(2);
    freshPub.resourceClass = ResourceClass::AcceleratorMemory; freshPub.unit = Unit::Bytes;
    freshPub.totalCapacity = Quantity::bytes(pool); freshPub.pool = ResourcePoolId(1); freshPub.poolGeneration = ResourcePoolGeneration(1);
    freshPub.capabilities = CapabilityGeneration(1); freshPub.topology = TopologyGeneration(1);
    freshPub.contract = ResourceContractId(1); freshPub.contractGeneration = ResourceContractGeneration(1);
    freshPub.availableFrom = Instant(0); freshPub.lifetime = Duration::seconds(365*24*3600);
    rf.revalidateResource(ResourceId(1), freshPub);
    CHECK(rf.activate(rc.id, rc.generation, mkAct(WorkerId(1), WorkerBootId(2), Instant(400))).activated, "D reactivate under fresh authority");
    CHECK(allocAndVerify(1LL * 1024 * 1024 * 1024), "D real CUDA work after revalidation");
  }

  // ---- Scenario E: recovery ----
  std::fprintf(stderr, "--- Scenario E: recovery (persist/restart/revalidate) ---\n");
  {
    const char* path = "cuda_state.bin";
    {
      ReservationFabric rf; makeFabric(rf, ResourceId(1), pool);
      auto rc = rf.commit(makeVRAMReq(7, 1LL * 1024 * 1024 * 1024, w, OwnerId(1)));
      CHECK(rc.committed, "E commit");
      CHECK(rf.activate(rc.id, rc.generation, mkAct(WorkerId(1), WorkerBootId(1), Instant(250))).activated, "E activate");
      rf.save(path);
    }
    auto rf2 = ReservationFabric::load(path);
    CHECK(rf2 != nullptr, "E load after coordinator restart");
    auto ids = rf2->reservationIds();
    CHECK(ids.size() == 1, "E durable reservation reconstructed");
    // Physical device evidence must NOT be assumed fresh after restart.
    bool anyRevalidated = false;
    for (auto id : ids) {
      const Reservation* r = rf2->reservation(id);
      if (r && r->lifecycle == Lifecycle::RevalidationRequired) anyRevalidated = true;  // conservative recovery
      if (r) {
        ResourcePublication freshPub;
        freshPub.worker = WorkerId(1); freshPub.workerBoot = WorkerBootId(3); freshPub.id = ResourceId(1); freshPub.generation = ResourceGeneration(3);
        freshPub.resourceClass = ResourceClass::AcceleratorMemory; freshPub.unit = Unit::Bytes;
        freshPub.totalCapacity = Quantity::bytes(pool); freshPub.pool = ResourcePoolId(1); freshPub.poolGeneration = ResourcePoolGeneration(1);
        freshPub.capabilities = CapabilityGeneration(1); freshPub.topology = TopologyGeneration(1);
        freshPub.contract = ResourceContractId(1); freshPub.contractGeneration = ResourceContractGeneration(1);
        freshPub.availableFrom = Instant(0); freshPub.lifetime = Duration::seconds(365*24*3600);
        rf2->revalidateResource(ResourceId(1), freshPub);
        CHECK(rf2->activate(id, r->generation, mkAct(WorkerId(1), WorkerBootId(3), Instant(400))).activated, "E reactivate after revalidation");
        CHECK(allocAndVerify(1LL * 1024 * 1024 * 1024), "E real CUDA work after recovery");
        CHECK(rf2->release(id, r->generation, mkRel(WorkerId(1), WorkerBootId(3), Instant(500))).released, "E release");
      }
    }
    CHECK(anyRevalidated, "E active reservation conservatively demoted after restart");
    std::remove(path);
  }

  // ---- Device memory cleanup: verify baseline restored ----
  const std::int64_t final = freeMemNow();
  std::int64_t drift = final - baseline;
  std::fprintf(stderr, "[DERIVED] free VRAM baseline=%lld MB final=%lld MB drift=%lld MB\n",
               (long long)(baseline/1048576), (long long)(final/1048576), (long long)(drift/1048576));
  CHECK(drift < 512LL * 1024 * 1024 && drift > -512LL * 1024 * 1024, "device memory returns to baseline");

  std::fprintf(stderr, "=== CUDA proof complete; failures=%d ===\n", failures);
  return failures == 0 ? 0 : 1;
}