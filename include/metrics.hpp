#pragma once

#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/registry.h>

#include <memory>

namespace judge {
namespace metrics {
std::shared_ptr<prometheus::Registry> global_registry();
}
}  // namespace judge