# Reservation Fabric

**Reservation Fabric is an open-source, vendor-neutral C++20 runtime for governing advance reservations of compute, accelerator memory, host memory, bandwidth, residency, and network capacity across heterogeneous AI infrastructure.**

**It answers one systems question:**

**What future capacity has been promised, to whom, for what interval and generation, under which constraints, and can that promise still be honored without violating existing commitments?**

Reservation Fabric turns future capacity into explicit, generation-bound, enforceable commitments. It sits above lower-level resource discovery and accounting, and beside scheduling and runtime control systems. It does not arbitrate current scarce resources, model predictive capacity, own workload or execution lifecycle, perform preemption, discover topology, or govern global SLO policy. It owns future commitments: reservation authority, identity and generation, admission against existing commitments, overlap and conflict detection, composite atomicity, activation windows, enforcement eligibility, modification, expiration, release, stale-resource rejection, durable recovery, and explainable feasibility.

## Defining thesis

**Capacity is not reserved because a scheduler intends to use it later. It is reserved only when a generation-bound commitment has been admitted against competing promises, committed atomically, preserved durably, and remains valid under current resource authority.**

## Systems boundaries

- **Resource Broker** governs real-time scarce-resource arbitration. Reservation Fabric consumes its current inventory, resource generation, and confirmed consumption/release.
- **Capacity Fabric** governs predictive capacity modeling. Reservation Fabric may use supplied forecasts but always distinguishes forecast from committed capacity.
- **Workload Fabric** governs durable workload lifecycle. Reservation Fabric observes workload identity/generation and reservation binding through narrow interfaces.
- **Execution Fabric** governs authoritative execution attempts. Reservation Fabric binds activation and consumption to execution authority.
- **Preemption Fabric** governs safe interruption and reclaimability of live work. Reservation Fabric never performs preemption itself.
- **Topology / NUMA / PCIe / network runtimes** govern physical topology and locality evidence. Reservation Fabric consumes those facts through narrow interfaces.
- **Dependency Fabric** performs optional prerequisite validation; Reservation Fabric does not duplicate dependency graph semantics.
- **SLO Fabric** governs broader service-level contracts; Reservation Fabric does not absorb global SLO governance.
- **Reservation Fabric** governs future capacity commitments: atomic admission, lifecycle, activation, accounting, and enforcement eligibility.

The public API is usable without the built-in TCP coordinator. Framed TCP transport (coordinator + workers + clients) is a reference deployment mechanism, not the core abstraction.

## Identity and generation model

All identities and generations are strongly typed and never silently interchanged across authority domains. Distinct types include ReservationId/ReservationGeneration, ReservationRequestId/RequestGeneration, ReservationSetId/SetGeneration, ReservationGroupId/GroupGeneration, ReservationHoldId/HoldGeneration, ResourceId/ResourceGeneration, ResourcePoolId/PoolGeneration, ResourceContractId/ContractGeneration, CapacityGeneration, OwnerId/OwnerGeneration, WorkloadId/WorkloadGeneration, ExecutionId/ExecutionGeneration, TenantId, PlacementId, TopologyGeneration, CapabilityGeneration, PolicyGeneration, PriorityGeneration, CoordinatorEpoch, WorkerId, WorkerBootId, ActivationGeneration, ConsumptionGeneration, ReleaseGeneration, TransferGeneration, RecoveryGeneration, and AuthorityGeneration.

- A stale ResourceGeneration never satisfies a current reservation.
- A stale ReservationGeneration never releases or modifies the current reservation.
- A stale WorkerBootId never publishes activation, consumption, or release evidence.
- A stale CoordinatorEpoch never mutates current commitments.

## Reservation lifecycle

The runtime uses an explicit guarded state machine (Requested → Planned → Held → Committed → PendingActivation → Active → … → Released/Expired/Cancelled/Superseded → Retired, plus transient states such as Committing, Modifying, Renewing, Releasing, Recovering and RevalidationRequired). Illegal transitions are rejected deterministically (for example REQUESTED→ACTIVE, HELD→CONSUMED, RELEASED→ACTIVE, and activation after EXPIRED/CANCELLED all fail). Modifications operate on a fresh generation and supersede the previous one; one current authoritative generation exists per reservation.

## Reservation strength

HARD, SOFT, OPPORTUNISTIC, BEST_EFFORT and UNKNOWN strength are modeled. A HARD reservation reduces future allocatable commitment capacity according to its contract; SOFT may be displaced only according to explicit policy; BEST_EFFORT is never described as guaranteed; UNKNOWN never silently becomes HARD. HARD reservations are never overbooked by default.

## Holds and atomicity

Provisional holds are first-class, bounded, and expire. A hold reserves candidate capacity during atomic planning without creating a durable commitment. Composite all-or-nothing bundles acquire provisional holds on every component, then commit all or none; a failure on any component releases every hold and leaves no partially committed reservation behind. Bundle membership, ordered activation, topology binding and colocation are supported. The accounting returns exactly to the pre-request baseline when a bundle aborts.

## Capacity accounting and overlap

For each resource scope the runtime tracks total capacity, reserved, held, active, consumed, released, expired, cancelled, headroom and peak future commitment. Quantities are strongly typed, integer, non-negative, and use checked arithmetic; they are never raw doubles for bytes, bandwidth or capacity. A deterministic, keyed lazy treap indexes the capacity envelope, so admission, overlap, conflict, release and resize are O(log N) rather than O(N) on the number of committed intervals. Overlap uses explicit half-open interval semantics.

## Feasibility and explanation

Every admission decision returns a structured FeasibilityReport with a typed outcome (Admissible, AdmissibleWithModification, PartiallyAdmissible, RejectCapacity, RejectConflict, RejectStaleResource, RejectStalePolicy, RejectTopology, RejectCapability, RejectLocality, RejectInvalidInterval, RejectInvalidQuantity, RejectDuplicate, RejectStaleAuthority, RevalidationRequired, Unknown). It includes the requested resource/quantity/interval, capacity envelope, conflicting reservations, bottleneck resource, available headroom, stale sources, and human-readable reasons. UNKNOWN never becomes Admissible.

## Activation, consumption, release

A committed reservation becomes ACTIVE only when its generation is current, the resource generation is current, the owner/workload authority is current, the activation window is valid, a fresh worker boot id publishes the evidence, and policy permits it. Consumption is distinct from commitment: reserved, activated and consumed quantities are tracked separately, over-consumption is rejected, and duplicate consumption is idempotent. Release returns unconsumed capacity exactly; a released reservation no longer reduces capacity; duplicate release never creates capacity. Expiration and cancellation are explicit and remove future commitment without deleting history.

## Modification, renewal, transfer

Resize, renew and transfer create fresh authoritative generations and supersede the prior one. Increasing a quantity performs fresh admission; decreasing releases exactly the delta. A renewal never silently extends an expired reservation without fresh authority. A transfer is generation-fenced, requires the reservation to permit transfer, and requires destination compatibility and current authority; the old owner becomes stale after a committed transfer. If transfer is not permitted it is rejected rather than faked.

## Resource invalidation, revalidation, recovery

If a resource becomes unavailable or its generation advances, affected reservations become explicit (RevalidationRequired) and are never silently remapped to another resource. Durable commitments are retained; dynamic physical evidence is conservatively discarded on restart and must be revalidated by a fresh worker publication. Persistence uses a versioned binary format with CRC integrity, bounded lengths, and checksum/size/cross-reference validation that rejects truncation, corruption, trailing garbage, bad magic, invalid enums, generation regression, negative capacity and impossible state. Recovery re-derives deterministic commitment state and never restores Active status without current physical evidence.

## Multiprocess proof

A real multiprocess proof is provided over real loopback TCP with a checksummed framed protocol. It runs a coordinator plus two worker processes and a client controller, proves exact accounting for overlapping commitments, rejects an over-subscription, rejects a duplicate request, activates, consumes, releases, and re-admits a fresh compatible reservation. It then kills a worker as a real OS process, shows the coordinator fences it and marks its reservations RevalidationRequired, restarts the worker with a fresh boot id, revalidates, and reactivates under fresh authority. See `distributed/mp_scenario`.

## CUDA proof

When a real CUDA device is available, `cuda/rf_cuda_proof` runs a single physical NVIDIA RTX 5090 (sm_120) proof that reservation authority gates real accelerator memory use:
- A: a committed VRAM reservation gates allocation; a real CUDA kernel runs and is verified by CPU parity; device memory returns to baseline.
- B: an overlapping reservation that would exceed the governed test pool is rejected before any CUDA allocation.
- C: a stale reservation generation is rejected before allocation; the current generation succeeds.
- D: worker death fences the resource; the durable reservation is retained and flagged RevalidationRequired; a fresh boot revalidates; reactivation and real CUDA work succeed.
- E: the coordinator restarts from persisted state, conservatively demotes active status, revalidates the device generation, reactivates, runs real CUDA work and confirms CPU parity.

The CUDA proof uses a SYNTHETIC governed capacity pool (a test-controlled subset of physical free VRAM) and never claims multi-GPU, MIG, NVLink, RDMA, GPUDirect, a distributed GPU fleet, or hardware capacity guarantees that were not tested. Evidence is labeled REAL, SYNTHETIC, DERIVED, or UNSUPPORTED.

## Testing

The suite includes deterministic unit tests (state machine, accounting, interval/overlap, composite atomicity, stale authority, persistence/recovery), seeded property tests cross-checked against a slow reference implementation, genuine concurrency tests, adversarial hardening tests, and the real multiprocess proof. Tests run without any timeouts. The runtime uses a single shared mutex with a documented non-reentrant acquire strategy; a manual lock-reentrancy audit confirmed no shared-lock reacquisition, no read→write upgrade without dropping the guard, no callback-induced re-entry under an ownership lock, and a single consistent lock order across reservation, accounting, hold, protocol and persistence state.

## Build

Requires a C++20 toolchain and CMake 3.20+. On Windows/MSVC the library builds cleanly under /W4 /WX; Release and Debug both pass. CUDA (optional) enables the sm_120 proof. AddressSanitizer is supported for the CPU core where the MSVC sanitizer runtimes are installed for the target architecture.

## Layout

`include/reservation_fabric` public headers; `src` runtime/library; `tests` test suite and proofs; `benchmarks`; `examples`; `tools` CLI; `cuda` CUDA proof; `distributed` reference TCP coordinator/worker/scenario; `cmake` packaging.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.