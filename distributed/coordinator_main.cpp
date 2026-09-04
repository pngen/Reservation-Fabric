#include <reservation_fabric/runtime.hpp>
#include <reservation_fabric/protocol.hpp>
#include <reservation_fabric/net.hpp>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>

using namespace reservation_fabric;
using namespace reservation_fabric::protocol;
using namespace reservation_fabric::detail;

namespace {
struct CoordinatorState {
  ReservationFabric runtime;
  std::string savePath;
  std::atomic<bool> shutdown{false};
};
CoordinatorState* g_state = nullptr;

void sendErr(SOCKET s, const std::string& msg) {
  ByteWriter b; b.str(msg);
  sendFrame(s, MessageType::ErrorResponse, b.data());
}

void handleMessage(SOCKET s, const Frame& frame) {
  ByteReader r(frame.payload);
  switch (frame.type) {
    case MessageType::Hello: {
      std::string role = r.str();
      WorkerId w = r.id<WorkerId>();
      WorkerBootId boot = r.gen<WorkerBootId>();
      ByteWriter b; b.str(role);
      sendFrame(s, MessageType::Acknowledge, b.data());
      (void)w; (void)boot;
      break;
    }
    case MessageType::Register: {
      WorkerId w = r.id<WorkerId>();
      WorkerBootId boot = r.gen<WorkerBootId>();
      ByteWriter b; b.id(w);
      sendFrame(s, MessageType::Acknowledge, b.data());
      (void)boot;
      break;
    }
    case MessageType::PublishResource: {
      ResourcePublication pub = desPublication(r);
      std::string err;
      auto res = g_state->runtime.publishResource(pub, &err);
      ByteWriter b;
      b.enu(res == ResourceRegisterResult::Accepted ? ResourceRegisterResult::Accepted : res);
      if (!err.empty()) b.str(err); else b.str("");
      sendFrame(s, MessageType::PublishResult, b.data());
      break;
    }
    case MessageType::Assess: {
      ReservationRequest req = desReq(r);
      FeasibilityReport rep = g_state->runtime.assess(req);
      ByteWriter b; serReport(b, rep);
      sendFrame(s, MessageType::AssessResult, b.data());
      break;
    }
    case MessageType::RequestReservation: {
      ReservationRequest req = desReq(r);
      ReservationResult rr = g_state->runtime.commit(req);
      ByteWriter b; serReservationResult(b, rr);
      sendFrame(s, MessageType::ReservationResult, b.data());
      break;
    }
    case MessageType::Activate: {
      ReservationId id = r.id<ReservationId>();
      ReservationGeneration gen = r.gen<ReservationGeneration>();
      ActivationEvidence ev = desActivationEvidence(r);
      ActivationResult ar = g_state->runtime.activate(id, gen, ev);
      ByteWriter b; serActivationResult(b, ar);
      sendFrame(s, MessageType::ActivateResult, b.data());
      break;
    }
    case MessageType::Consume: {
      ReservationId id = r.id<ReservationId>();
      ReservationGeneration gen = r.gen<ReservationGeneration>();
      Quantity amount = r.quant();
      ConsumeEvidence ev;
      ev.worker = r.id<WorkerId>(); ev.workerBoot = r.gen<WorkerBootId>(); ev.consumptionGeneration = r.gen<ConsumptionGeneration>(); ev.at = r.instant();
      ConsumeResult cr = g_state->runtime.consume(id, gen, amount, ev);
      ByteWriter b; serConsumeResult(b, cr);
      sendFrame(s, MessageType::ConsumeResult, b.data());
      break;
    }
    case MessageType::Release: {
      ReservationId id = r.id<ReservationId>();
      ReservationGeneration gen = r.gen<ReservationGeneration>();
      ReleaseEvidence ev;
      ev.worker = r.id<WorkerId>(); ev.workerBoot = r.gen<WorkerBootId>(); ev.releaseGeneration = r.gen<ReleaseGeneration>(); ev.at = r.instant();
      ReleaseResult rr = g_state->runtime.release(id, gen, ev);
      ByteWriter b; serReleaseResult(b, rr);
      sendFrame(s, MessageType::ReleaseResult, b.data());
      break;
    }
    case MessageType::Query: {
      std::string kind = r.str();
      ByteWriter b;
      if (kind == "inventory") b.str(g_state->runtime.dumpInventory());
      else if (kind == "count") b.str(std::to_string(g_state->runtime.currentReservationCount()));
      else if (kind == "epoch") b.str(std::to_string(g_state->runtime.coordinatorEpoch().value()));
      else if (kind == "accounting") {
        ResourceId id = r.id<ResourceId>();
        auto acc = g_state->runtime.accounting(id);
        b.i64(acc.committed.value()); b.i64(acc.headroom.value()); b.boolean(acc.consistent);
      } else b.str("");
      sendFrame(s, MessageType::QueryResult, b.data());
      break;
    }
    case MessageType::Revalidate: {
      ResourcePublication fresh = desPublication(r);
      std::size_t n = g_state->runtime.revalidateResource(fresh.id, fresh);
      ByteWriter b; b.u64(n);
      sendFrame(s, MessageType::Acknowledge, b.data());
      break;
    }
    case MessageType::Shutdown: {
      g_state->shutdown = true;
      ByteWriter b; b.u8(1);
      sendFrame(s, MessageType::Acknowledge, b.data());
      break;
    }
    default:
      sendErr(s, "unhandled message type");
      break;
  }
}

void serveConnection(SOCKET s) {
  WorkerId connWorker;
  for (;;) {
    Frame frame;
    int rc = recvFrame(s, frame);
    if (rc == 1) break;         // clean close / error
    if (rc == -1) break;         // malformed frame
    try {
      ByteReader r(frame.payload);
      if (frame.type == MessageType::Register) { connWorker = r.id<WorkerId>(); }
      if (frame.type == MessageType::Hello) { std::string role = r.str(); if (role == "worker") { WorkerId w = r.id<WorkerId>(); connWorker = w; } else { (void)r.id<WorkerId>(); } }
      handleMessage(s, frame);
    } catch (const FabricError& e) { sendErr(s, e.what()); }
  }
  // A worker that dropped (process killed) must have its resources fenced so that
  // reservations bound to its dynamic evidence become RevalidationRequired.
  if (connWorker.valid()) {
    for (auto id : g_state->runtime.resourceIds()) {
      const Resource* res = g_state->runtime.resource(id);
      if (res && res->owner == connWorker) {
        InvalidationNotice notice; notice.resource = id; notice.condition = ResourceCondition::Invalidated; notice.reason = "worker disconnected";
        g_state->runtime.invalidateResource(notice);
      }
    }
  }
  closesocket(s);
}

}  // anonymous namespace

int main(int argc, char** argv) {
  if (argc < 2) { std::cerr << "usage: rf_coordinator <port>\n"; return 2; }
  int port = std::atoi(argv[1]);
  if (!netInit()) { std::cerr << "winsock init failed\n"; return 2; }
  CoordinatorState state;
  g_state = &state;
  SOCKET listener;
  if (!listenOn(port, listener)) { std::cerr << "bind failed\n"; netCleanup(); return 2; }
  std::fprintf(stderr, "coordinator listening on %d (epoch %llu)\n", port, (unsigned long long)state.runtime.coordinatorEpoch().value());
  std::fflush(stderr);

  std::vector<std::thread> threads;
  while (!state.shutdown) {
    bool ok = false;
    SOCKET s = acceptConn(listener, ok);
    if (!ok) break;
    threads.emplace_back(serveConnection, s);
    // Bound concurrent connections.
    if (threads.size() > 16) {
      for (auto& t : threads) if (t.joinable()) t.join();
      threads.clear();
    }
  }
  for (auto& t : threads) if (t.joinable()) t.join();
  closesocket(listener);
  netCleanup();
  return 0;
}