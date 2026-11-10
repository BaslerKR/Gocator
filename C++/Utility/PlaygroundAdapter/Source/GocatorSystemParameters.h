#pragma once

#include "DeviceParameter.h"
#include "Gocator.h"

namespace gocator {

/** @brief Projects official GoSystem observations into the neutral parameter contract. */
inline std::vector<DeviceParameterDescriptor> systemParameters(
    const Gocator::SystemState& state, const std::string& identity, std::uint64_t revision)
{
    const auto connection = identity + ":" + std::to_string(state.connectionGeneration);
    DeviceParameterDescriptor running;
    running.target = {connection, "System", "runningState", {}};
    running.revision = revision;
    running.displayName = "Running state";
    running.kind = DeviceParameterKind::Enumeration;
    running.available = running.readable = true;
    running.enumEntries = {"ready", "running", "conflict"};
    running.value = std::string(state.running == Gocator::SystemState::Running::Ready ? "ready"
        : state.running == Gocator::SystemState::Running::Running ? "running" : "conflict");

    DeviceParameterDescriptor quick;
    quick.target = {connection, "System", "quickEditEnabled", {}};
    quick.revision = revision;
    quick.displayName = "Quick edit";
    quick.description = "Suppresses recalculation of existing scan data while editing configuration.";
    quick.kind = DeviceParameterKind::Boolean;
    quick.available = quick.readable = quick.writable = true;
    quick.value = state.quickEditEnabled;
    return {std::move(running), std::move(quick)};
}

} // namespace gocator
