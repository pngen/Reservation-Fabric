#pragma once
#include <reservation_fabric/runtime.hpp>
#include <iostream>
using namespace reservation_fabric;

inline ResourcePublication makeResource(ResourceId id, ResourceClass cls, Unit unit, Quantity cap,
                                        WorkerId w = WorkerId(1), ResourceGeneration gen = ResourceGeneration(1),
                                        WorkerBootId boot = WorkerBootId(1)) {
  ResourcePublication pub;
  pub.worker = w; pub.workerBoot = boot; pub.id = id; pub.generation = gen;
  pub.resourceClass = cls; pub.unit = unit; pub.totalCapacity = cap;
  pub.pool = ResourcePoolId(1); pub.poolGeneration = ResourcePoolGeneration(1);
  pub.capabilities = CapabilityGeneration(1); pub.topology = TopologyGeneration(1);
  pub.contract = ResourceContractId(1); pub.contractGeneration = ResourceContractGeneration(1);
  pub.availableFrom = Instant(0); pub.lifetime = Duration::seconds(365 * 24 * 3600);
  return pub;
}

inline ReservationRequest makeReq(ReservationRequestId rid, ResourceClass cls, Unit unit, Quantity qty,
                                  Interval window, OwnerId ow = OwnerId(1),
                                  ReservationStrength strength = ReservationStrength::Hard) {
  ReservationRequest req;
  req.requestId = rid; req.requestGeneration = ReservationRequestGeneration(1);
  req.resourceClass = cls; req.unit = unit; req.quantity = qty; req.window = window;
  req.activationMode = ActivationMode::FixedInterval; req.strength = strength;
  req.owner = ow; req.ownerGeneration = OwnerGeneration(1);
  req.policyGeneration = PolicyGeneration(1); req.priorityGeneration = PriorityGeneration(1);
  return req;
}

inline ActivationEvidence makeEvidence(WorkerId w = WorkerId(1), WorkerBootId boot = WorkerBootId(1), Instant at = Instant(100), ActivationGeneration g = ActivationGeneration(1)) {
  ActivationEvidence e; e.worker = w; e.workerBoot = boot; e.activationGeneration = g; e.at = at; return e;
}
inline ConsumeEvidence makeConsumeEvidence(WorkerId w = WorkerId(1), WorkerBootId boot = WorkerBootId(1), ConsumptionGeneration g = ConsumptionGeneration(1)) {
  ConsumeEvidence e; e.worker = w; e.workerBoot = boot; e.consumptionGeneration = g; return e;
}
inline ReleaseEvidence makeReleaseEvidence(WorkerId w = WorkerId(1), WorkerBootId boot = WorkerBootId(1), Instant at = Instant(100), ReleaseGeneration g = ReleaseGeneration(1)) {
  ReleaseEvidence e; e.worker = w; e.workerBoot = boot; e.releaseGeneration = g; e.at = at; return e;
}
