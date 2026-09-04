#include <reservation_fabric/protocol.hpp>
#include <reservation_fabric/net.hpp>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace reservation_fabric;
using namespace reservation_fabric::protocol;
using namespace reservation_fabric::detail;

int main(int argc, char** argv) {
  if (argc < 8) { std::fprintf(stderr, "usage: rf_worker host port workerId bootId resourceId capacity startNs\n"); return 2; }
  std::string host = argv[1];
  int port = std::atoi(argv[2]);
  WorkerId worker(static_cast<std::uint64_t>(std::atoll(argv[3])));
  WorkerBootId boot(static_cast<std::uint64_t>(std::atoll(argv[4])));
  ResourceId resId(static_cast<std::uint64_t>(std::atoll(argv[5])));
  Quantity cap = Quantity::count(static_cast<std::int64_t>(std::atoll(argv[6])));
  Instant start(static_cast<std::int64_t>(std::atoll(argv[7])));

  if (!netInit()) return 2;
  SOCKET s = connectTo(host, port);
  if (s == INVALID_SOCKET) { std::fprintf(stderr, "worker connect failed\n"); netCleanup(); return 2; }

  { ByteWriter b; b.str("worker"); b.id(worker); b.gen(boot); sendFrame(s, MessageType::Hello, b.data()); Frame f; recvFrame(s, f); }
  { ByteWriter b; b.id(worker); b.gen(boot); sendFrame(s, MessageType::Register, b.data()); Frame f; recvFrame(s, f); }

  ResourcePublication pub;
  pub.worker = worker; pub.workerBoot = boot; pub.id = resId; pub.generation = ResourceGeneration(1);
  pub.resourceClass = ResourceClass::AcceleratorCompute; pub.unit = Unit::Count; pub.totalCapacity = cap;
  pub.pool = ResourcePoolId(1); pub.poolGeneration = ResourcePoolGeneration(1);
  pub.capabilities = CapabilityGeneration(1); pub.topology = TopologyGeneration(1);
  pub.contract = ResourceContractId(1); pub.contractGeneration = ResourceContractGeneration(1);
  pub.availableFrom = start; pub.lifetime = Duration::seconds(365 * 24 * 3600);

  { ByteWriter b; serPublication(b, pub); sendFrame(s, MessageType::PublishResource, b.data()); Frame f; recvFrame(s, f); }
  std::fprintf(stderr, "WORKER %llu PUBLISHED resource %llu capacity=%lld\n",
               (unsigned long long)worker.value(), (unsigned long long)resId.value(), (long long)cap.value());
  std::fflush(stderr);

  for (;;) { Frame f; int rc = recvFrame(s, f); if (rc != 0) break; }
  closesocket(s); netCleanup();
  return 0;
}