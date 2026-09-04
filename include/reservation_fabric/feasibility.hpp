#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <reservation_fabric/identity.hpp>
#include <reservation_fabric/quantity.hpp>
#include <reservation_fabric/time.hpp>
#include <reservation_fabric/request.hpp>
#include <reservation_fabric/resource.hpp>

namespace reservation_fabric {

// Deterministic structured feasibility outcome. UNKNOWN must never be reported
// as ADMISSIBLE.
enum class Feasibility : std::uint8_t {
  Admissible = 1,
  AdmissibleWithModification,
  PartiallyAdmissible,
  RejectCapacity,
  RejectConflict,
  RejectStaleResource,
  RejectStalePolicy,
  RejectTopology,
  RejectCapability,
  RejectLocality,
  RejectInvalidInterval,
  RejectInvalidQuantity,
  RejectDuplicate,
  RejectStaleAuthority,
  RevalidationRequired,
  Unknown
};

inline const char* to_string(Feasibility f) noexcept {
  switch (f) {
    case Feasibility::Admissible: return "Admissible";
    case Feasibility::AdmissibleWithModification: return "AdmissibleWithModification";
    case Feasibility::PartiallyAdmissible: return "PartiallyAdmissible";
    case Feasibility::RejectCapacity: return "RejectCapacity";
    case Feasibility::RejectConflict: return "RejectConflict";
    case Feasibility::RejectStaleResource: return "RejectStaleResource";
    case Feasibility::RejectStalePolicy: return "RejectStalePolicy";
    case Feasibility::RejectTopology: return "RejectTopology";
    case Feasibility::RejectCapability: return "RejectCapability";
    case Feasibility::RejectLocality: return "RejectLocality";
    case Feasibility::RejectInvalidInterval: return "RejectInvalidInterval";
    case Feasibility::RejectInvalidQuantity: return "RejectInvalidQuantity";
    case Feasibility::RejectDuplicate: return "RejectDuplicate";
    case Feasibility::RejectStaleAuthority: return "RejectStaleAuthority";
    case Feasibility::RevalidationRequired: return "RevalidationRequired";
    case Feasibility::Unknown: return "Unknown";
  }
  return "Unknown";
}

// A typed, deterministic explanation of an admission decision. Human-readable
// reasons are supplementary; the typed fields are the primary API.
struct FeasibilityReport {
  Feasibility outcome = Feasibility::Unknown;
  ResourceClass resourceClass = ResourceClass::Unknown;
  Unit unit = Unit::Count;
  Quantity requestedQuantity = Quantity::zero(Unit::Count);
  Quantity admissibleQuantity = Quantity::zero(Unit::Count);  // meaningful for partial/modified
  Interval requestedInterval;
  ResourceId resource;                       // 0 if unresolved
  ResourceGeneration currentResourceGeneration;
  ResourceGeneration staleResourceGeneration;
  PolicyGeneration policyGeneration;
  PolicyGeneration stalePolicyGeneration;
  CoordinatorEpoch currentEpoch;
  CoordinatorEpoch staleEpoch;
  std::vector<ReservationId> conflictingReservations;
  std::vector<ResourceId> bottleneckResources;
  Quantity availableAtBottleneck = Quantity::zero(Unit::Count);
  Interval conflictingInterval;
  bool blockedBySoftOnly = false;           // only a soft/opportunistic reservation blocks
  bool overbookEligible = false;
  bool revalidationRequired = false;
  std::vector<std::string> reasons;          // human-readable supplementary text

  bool admissible() const noexcept {
    return outcome == Feasibility::Admissible || outcome == Feasibility::AdmissibleWithModification ||
           outcome == Feasibility::PartiallyAdmissible;
  }
  static FeasibilityReport admitted() { FeasibilityReport r; r.outcome = Feasibility::Admissible; return r; }
  static FeasibilityReport rejected(Feasibility f, std::string reason) {
    FeasibilityReport r; r.outcome = f; if (!reason.empty()) r.reasons.push_back(std::move(reason)); return r;
  }
};

}  // namespace reservation_fabric
