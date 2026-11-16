#pragma once

#include "DeviceParameter.h"

#include <QJsonObject>
#include <QJsonValue>

#include <string>
#include <vector>

namespace gocator {

/**
 * @brief Copies one writable leaf of a GoPxL resource schema together with its observed value.
 * @note The controller owns revisions; this projection never mutates device state and leaves
 *       `descriptor.revision` at zero for the caller to stamp from its observation cache.
 */
struct ResourceParameter {
    DeviceParameterDescriptor descriptor;
    /** @brief The value exactly as GoPxL stores it, used to encode a validated write. */
    QJsonValue rawValue;
    /** @brief Raw enum values parallel to descriptor.enumEntries; empty for non-enum leaves. */
    std::vector<QJsonValue> enumRawValues;
};

/**
 * @brief Flattens one GoPxL resource schema/data pair into contract descriptors.
 * @param schema Resource JSON schema, normally `/scan/.../scanner-0`.
 * @param data Current resource data for the same path.
 * @param scope Published parameter scope (`Scanner` or `Sensor`).
 * @param connectionId Exact current connection generation.
 * @return Leaves in stable key order (QJsonObject iteration order), with display labels and
 *         nested object groups preserved.
 */
std::vector<ResourceParameter> flattenResource(const QJsonObject& schema, const QJsonObject& data,
    const std::string& scope, const std::string& connectionId);

/** @brief Encodes one validated descriptor value as the JSON GoPxL accepts at its path. */
std::string encodeResourceWrite(const ResourceParameter& parameter, const DeviceParameterValue& value);

} // namespace gocator
