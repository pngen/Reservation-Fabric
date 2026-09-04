#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <reservation_fabric/identity.hpp>
#include <reservation_fabric/quantity.hpp>
#include <reservation_fabric/time.hpp>

namespace reservation_fabric {

// Typed resource classes across heterogeneous AI infrastructure. Each class has
// a canonical base Unit so that quantities are measured consistently.
enum class ResourceClass : std::uint8_t {
  AcceleratorCompute = 1,
  AcceleratorMemory,       // VRAM
  PinnedHostMemory,
  PageableHostMemory,
  DeviceLocalMemoryPool,
  ModelResidency,
  AdapterResidency,
  KVTensorStateResidency,
  TransferBandwidth,
  PcieBandwidth,
  InterconnectBandwidth,
  NetworkBandwidth,
  StorageBandwidth,
  StorageCapacity,
  QueueServiceSlot,
  ConcurrentExecution,
  CommunicationPath,
  PlacementConstrainedCapacity,
  TopologyConstrainedCapacity,
  CompositeMultiResourceBundle,
  Unknown
};

inline Unit unit_for(ResourceClass c) noexcept {
  switch (c) {
    case ResourceClass::AcceleratorCompute: return Unit::Count;
    case ResourceClass::AcceleratorMemory: return Unit::Bytes;
    case ResourceClass::PinnedHostMemory: return Unit::Bytes;
    case ResourceClass::PageableHostMemory: return Unit::Bytes;
    case ResourceClass::DeviceLocalMemoryPool: return Unit::Bytes;
    case ResourceClass::ModelResidency: return Unit::Bytes;
    case ResourceClass::AdapterResidency: return Unit::Bytes;
    case ResourceClass::KVTensorStateResidency: return Unit::Bytes;
    case ResourceClass::TransferBandwidth: return Unit::BytesPerSecond;
    case ResourceClass::PcieBandwidth: return Unit::BytesPerSecond;
    case ResourceClass::InterconnectBandwidth: return Unit::BytesPerSecond;
    case ResourceClass::NetworkBandwidth: return Unit::BytesPerSecond;
    case ResourceClass::StorageBandwidth: return Unit::BytesPerSecond;
    case ResourceClass::StorageCapacity: return Unit::Bytes;
    case ResourceClass::QueueServiceSlot: return Unit::Count;
    case ResourceClass::ConcurrentExecution: return Unit::Count;
    case ResourceClass::CommunicationPath: return Unit::Count;
    case ResourceClass::PlacementConstrainedCapacity: return Unit::Bytes;
    case ResourceClass::TopologyConstrainedCapacity: return Unit::Bytes;
    case ResourceClass::CompositeMultiResourceBundle: return Unit::Count;
    case ResourceClass::Unknown: return Unit::Count;
  }
  return Unit::Count;
}

inline const char* to_string(ResourceClass c) noexcept {
  switch (c) {
    case ResourceClass::AcceleratorCompute: return "AcceleratorCompute";
    case ResourceClass::AcceleratorMemory: return "AcceleratorMemory";
    case ResourceClass::PinnedHostMemory: return "PinnedHostMemory";
    case ResourceClass::PageableHostMemory: return "PageableHostMemory";
    case ResourceClass::DeviceLocalMemoryPool: return "DeviceLocalMemoryPool";
    case ResourceClass::ModelResidency: return "ModelResidency";
    case ResourceClass::AdapterResidency: return "AdapterResidency";
    case ResourceClass::KVTensorStateResidency: return "KVTensorStateResidency";
    case ResourceClass::TransferBandwidth: return "TransferBandwidth";
    case ResourceClass::PcieBandwidth: return "PcieBandwidth";
    case ResourceClass::InterconnectBandwidth: return "InterconnectBandwidth";
    case ResourceClass::NetworkBandwidth: return "NetworkBandwidth";
    case ResourceClass::StorageBandwidth: return "StorageBandwidth";
    case ResourceClass::StorageCapacity: return "StorageCapacity";
    case ResourceClass::QueueServiceSlot: return "QueueServiceSlot";
    case ResourceClass::ConcurrentExecution: return "ConcurrentExecution";
    case ResourceClass::CommunicationPath: return "CommunicationPath";
    case ResourceClass::PlacementConstrainedCapacity: return "PlacementConstrainedCapacity";
    case ResourceClass::TopologyConstrainedCapacity: return "TopologyConstrainedCapacity";
    case ResourceClass::CompositeMultiResourceBundle: return "CompositeMultiResourceBundle";
    case ResourceClass::Unknown: return "Unknown";
  }
  return "Unknown";
}

// A concrete reservable resource published by a worker. Capacity is integer and
// measured in the class's base unit. The generation advances whenever the
// underlying dynamic evidence changes, so a stale generation can never satisfy a
// current reservation.
struct Resource {
  ResourceId id;
  ResourceGeneration generation;
  ResourceClass resourceClass = ResourceClass::Unknown;
  Unit unit = Unit::Count;
  Quantity totalCapacity = Quantity::zero(Unit::Count);
  ResourcePoolId pool;
  ResourcePoolGeneration poolGeneration;
  WorkerId owner;
  WorkerBootId ownerBoot;             // evidence epoch for this publication
  CapabilityGeneration capabilities;
  TopologyGeneration topology;
  ResourceContractId contract;
  ResourceContractGeneration contractGeneration;
  bool available = true;

  bool operator==(const Resource& o) const noexcept {
    return id == o.id && generation == o.generation && resourceClass == o.resourceClass &&
           unit == o.unit && totalCapacity == o.totalCapacity && pool == o.pool &&
           owner == o.owner && ownerBoot == o.ownerBoot && capabilities == o.capabilities &&
           topology == o.topology && available == o.available;
  }
};

// A window during which a resource is unavailable for reservation (e.g. an
// announced maintenance window). A reservation overlapping an unavailable window
// is not admissible unless the window is part of the resource's capacity model.
struct UnavailableWindow {
  Interval interval;
  std::string reason;

  bool operator==(const UnavailableWindow& o) const noexcept {
    return interval == o.interval && reason == o.reason;
  }
};

}  // namespace reservation_fabric
