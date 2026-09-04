#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace reservation_fabric {

// Stable, typed error classification. Error messages are supplementary; callers
// and tools should rely on the code for deterministic dispatch.
enum class ErrorCode : std::uint8_t {
  None = 0,
  InvalidArgument,
  InvalidQuantity,
  QuantityOverflow,
  DuplicateIdentity,
  StaleAuthority,
  StaleGeneration,
  InvalidTransition,
  InvalidState,
  Conflict,
  CapacityExceeded,
  NotAdmissible,
  CorruptData,
  PersistenceFailed,
  ProtocolError,
  NotSupported,
  NoCapacity,
  TopologyMismatch,
  CapabilityMismatch,
  LocalityMismatch,
  InvalidInterval,
  NotFound,
  AlreadyExists,
  OverbookNotPermitted,
  AuthorityMismatch,
  OutOfBounds,
  Internal
};

inline const char* to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::None: return "None";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::InvalidQuantity: return "InvalidQuantity";
    case ErrorCode::QuantityOverflow: return "QuantityOverflow";
    case ErrorCode::DuplicateIdentity: return "DuplicateIdentity";
    case ErrorCode::StaleAuthority: return "StaleAuthority";
    case ErrorCode::StaleGeneration: return "StaleGeneration";
    case ErrorCode::InvalidTransition: return "InvalidTransition";
    case ErrorCode::InvalidState: return "InvalidState";
    case ErrorCode::Conflict: return "Conflict";
    case ErrorCode::CapacityExceeded: return "CapacityExceeded";
    case ErrorCode::NotAdmissible: return "NotAdmissible";
    case ErrorCode::CorruptData: return "CorruptData";
    case ErrorCode::PersistenceFailed: return "PersistenceFailed";
    case ErrorCode::ProtocolError: return "ProtocolError";
    case ErrorCode::NotSupported: return "NotSupported";
    case ErrorCode::NoCapacity: return "NoCapacity";
    case ErrorCode::TopologyMismatch: return "TopologyMismatch";
    case ErrorCode::CapabilityMismatch: return "CapabilityMismatch";
    case ErrorCode::LocalityMismatch: return "LocalityMismatch";
    case ErrorCode::InvalidInterval: return "InvalidInterval";
    case ErrorCode::NotFound: return "NotFound";
    case ErrorCode::AlreadyExists: return "AlreadyExists";
    case ErrorCode::OverbookNotPermitted: return "OverbookNotPermitted";
    case ErrorCode::AuthorityMismatch: return "AuthorityMismatch";
    case ErrorCode::OutOfBounds: return "OutOfBounds";
    case ErrorCode::Internal: return "Internal";
  }
  return "Unknown";
}

class FabricError : public std::runtime_error {
 public:
  FabricError(ErrorCode code, std::string message)
      : std::runtime_error(std::move(message)), code_(code) {}
  ErrorCode code() const noexcept { return code_; }
 private:
  ErrorCode code_;
};

inline void throw_error(ErrorCode code, std::string message) {
  throw FabricError(code, std::move(message));
}

}  // namespace reservation_fabric
