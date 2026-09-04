#include <reservation_fabric/runtime.hpp>
#include <iostream>
using namespace reservation_fabric;
int main() {
  ReservationFabric rf;
  ResourcePublication pub;
  pub.worker = WorkerId(1); pub.workerBoot = WorkerBootId(1);
  pub.id = ResourceId(1); pub.generation = ResourceGeneration(1);
  pub.resourceClass = ResourceClass::AcceleratorCompute; pub.unit = Unit::Count;
  pub.totalCapacity = Quantity::count(100);
  pub.availableFrom = Instant(0);
  pub.lifetime = Duration::seconds(365 * 24 * 3600);
  std::cout << "resource register: " << to_string(rf.publishResource(pub)) << "\n";
  return 0;
}
