#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <reservation_fabric/detail/serialization.hpp>
#include <reservation_fabric/runtime.hpp>

namespace reservation_fabric {
namespace protocol {

// ---- framed wire protocol ----
constexpr std::uint32_t kFrameMagic = 0x52464246u;      // "RFBF"
constexpr std::uint8_t kFrameVersion = 1;
constexpr std::uint32_t kMaxFramePayload = 1u << 20;     // 1 MiB frame cap
constexpr std::size_t kHeaderSize = 4 + 1 + 1 + 4 + 4;   // magic+ver+type+len+crc

enum class MessageType : std::uint8_t {
  Hello = 1,
  Acknowledge,
  ErrorResponse,
  Register,
  PublishResource,
  PublishResult,
  Assess,
  AssessResult,
  RequestReservation,
  ReservationResult,
  Activate,
  ActivateResult,
  Consume,
  ConsumeResult,
  Release,
  ReleaseResult,
  Query,
  QueryResult,
  Revalidate,
  Shutdown
};

const char* to_string(MessageType t) noexcept;

// Encode the whole frame (header + crc + payload) for one message.
std::vector<std::uint8_t> encodeFrame(MessageType type, const std::vector<std::uint8_t>& payload);

// Decoded frame with a bounded payload.
struct Frame {
  MessageType type = MessageType::ErrorResponse;
  std::vector<std::uint8_t> payload;
};

// Incremental, partial-read tolerant frame decoder. Feed bytes from the socket;
// when a frame is complete it is returned and consumed. Malformed frames (bad
// magic/version, oversized length, checksum mismatch) are rejected.
class FrameDecoder {
 public:
  // Returns 0 when a frame is decoded, -1 when a malformed frame is detected.
  // Returns 1 when more bytes are required.
  int push(const std::uint8_t* data, std::size_t len, Frame& out);
  std::size_t buffered() const { return buf_.size(); }
 private:
  std::vector<std::uint8_t> buf_;
};

// ---- payload schemas (deterministic, versioned) ----
struct HelloMsg {
  std::string role;      // "worker" or "client"
  WorkerId worker;
  WorkerBootId workerBoot;
};
struct RegisterMsg { WorkerId worker; WorkerBootId workerBoot; };
struct PublishMsg { ResourcePublication pub; };
struct AssessMsg { ReservationRequest req; };
struct RequestMsg { ReservationRequest req; };
struct ActivateMsg { ReservationId id; ReservationGeneration generation; ActivationEvidence evidence; };
struct ConsumeMsg { ReservationId id; ReservationGeneration generation; Quantity amount; ConsumeEvidence evidence; };
struct ReleaseMsg { ReservationId id; ReservationGeneration generation; ReleaseEvidence evidence; };
struct ActivateResultMsg { ActivationResult result; };
struct ConsumeResultMsg { ConsumeResult result; };
struct ReleaseResultMsg { ReleaseResult result; };
struct ReservationResultMsg { ReservationResult result; };
struct AssessResultMsg { FeasibilityReport report; };

// Deterministic payload serialization shared by coordinator, worker and client.
void serReq(detail::ByteWriter& w, const ReservationRequest& req);
ReservationRequest desReq(detail::ByteReader& r);
void serPublication(detail::ByteWriter& w, const ResourcePublication& p);
ResourcePublication desPublication(detail::ByteReader& r);
void serReport(detail::ByteWriter& w, const FeasibilityReport& rep);
FeasibilityReport desReport(detail::ByteReader& r);
void serActivationEvidence(detail::ByteWriter& w, const ActivationEvidence& e);
ActivationEvidence desActivationEvidence(detail::ByteReader& r);
void serReservationResult(detail::ByteWriter& w, const ReservationResult& rr);
ReservationResult desReservationResult(detail::ByteReader& r);
void serActivationResult(detail::ByteWriter& w, const ActivationResult& ar);
ActivationResult desActivationResult(detail::ByteReader& r);
void serConsumeResult(detail::ByteWriter& w, const ConsumeResult& cr);
ConsumeResult desConsumeResult(detail::ByteReader& r);
void serReleaseResult(detail::ByteWriter& w, const ReleaseResult& rr);
ReleaseResult desReleaseResult(detail::ByteReader& r);

}  // namespace protocol
}  // namespace reservation_fabric