#include "GocatorParameterTree.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QString>

#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace gocator {
namespace {

QString displayName(const QJsonObject& schema, const QString& key)
{
    const auto label = schema.value(QStringLiteral("label")).toString();
    if (!label.isEmpty()) return label;
    const auto title = schema.value(QStringLiteral("title")).toString();
    if (!title.isEmpty()) return title;
    return key;
}

DeviceParameterVisibility visibility(const QJsonObject& schema)
{
    auto value = schema.value(QStringLiteral("visibility")).toString().toLower();
    if (value.isEmpty()) value = schema.value(QStringLiteral("level")).toString().toLower();
    if (value == QStringLiteral("advanced")) return DeviceParameterVisibility::Advanced;
    if (value == QStringLiteral("expert")) return DeviceParameterVisibility::Expert;
    if (value == QStringLiteral("hidden")) return DeviceParameterVisibility::Hidden;
    return DeviceParameterVisibility::Primary;
}

std::string unitOf(const QJsonObject& schema)
{
    auto unit = schema.value(QStringLiteral("unit")).toString();
    if (unit.isEmpty()) unit = schema.value(QStringLiteral("units")).toString();
    return unit.toStdString();
}

std::optional<std::int64_t> integerBound(const QJsonObject& schema, const char* name)
{
    const auto value = schema.value(QLatin1String(name));
    if (!value.isDouble()) return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number)) return std::nullopt;
    if (number < static_cast<double>(std::numeric_limits<std::int64_t>::min())
        || number > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    return static_cast<std::int64_t>(number);
}

std::optional<double> numberBound(const QJsonObject& schema, const char* name)
{
    const auto value = schema.value(QLatin1String(name));
    if (!value.isDouble()) return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number)) return std::nullopt;
    return number;
}

std::optional<std::int64_t> integerValue(const QJsonValue& value)
{
    if (value.isDouble()) return static_cast<std::int64_t>(value.toDouble());
    if (!value.isString()) return std::nullopt;
    bool ok = false;
    const auto number = value.toString().toLongLong(&ok);
    if (!ok) return std::nullopt;
    return number;
}

std::optional<double> floatValue(const QJsonValue& value)
{
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (std::isfinite(number)) return number;
    }
    if (!value.isString()) return std::nullopt;
    bool ok = false;
    const double number = value.toString().toDouble(&ok);
    if (!ok || !std::isfinite(number)) return std::nullopt;
    return number;
}

QString encodedValue(const QJsonValue& value)
{
    const QByteArray bytes = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(bytes.mid(1, bytes.size() - 2));
}

void appendCategory(std::vector<std::string>* path, const QString& label)
{
    if (!path || path->size() >= deviceParameterCategoryDepthLimit) return;
    auto segment = label.toStdString();
    if (segment.empty() || segment.size() > deviceParameterCategorySegmentLimit
        || segment.find('\0') != std::string::npos)
        return;
    path->push_back(std::move(segment));
}

QString escapedToken(const QString& key)
{
    return QString(key).replace(QStringLiteral("~"), QStringLiteral("~0"))
        .replace(QStringLiteral("/"), QStringLiteral("~1"));
}

void addLeaf(std::vector<ResourceParameter>* parameters, const QString& path, const QString& name,
    const QJsonObject& schema, const QJsonValue& rawValue, bool inheritedReadOnly,
    const std::vector<std::string>& categoryPath, const std::string& scope,
    const std::string& connectionId)
{
    if (!parameters) return;

    ResourceParameter parameter;
    auto& descriptor = parameter.descriptor;
    descriptor.target = {connectionId, scope, path.toStdString(), {}};
    descriptor.displayName = displayName(schema, name).toStdString();
    descriptor.description = schema.value(QStringLiteral("description")).toString().toStdString();
    descriptor.unit = unitOf(schema);
    descriptor.available = true;
    descriptor.readable = true;
    descriptor.writable = !(inheritedReadOnly || schema.value(QStringLiteral("readOnly")).toBool());
    descriptor.categoryPath = categoryPath;
    descriptor.visibility = visibility(schema);
    descriptor.representation = DeviceParameterRepresentation::Automatic;

    const auto type = schema.value(QStringLiteral("type")).toString();
    const auto enumValues = schema.value(QStringLiteral("enum")).toArray();
    const auto enumTexts = schema.value(QStringLiteral("enumText")).toArray();

    if (!enumValues.isEmpty()) {
        descriptor.kind = DeviceParameterKind::Enumeration;
        descriptor.representation = DeviceParameterRepresentation::Choice;
        for (qsizetype index = 0; index < enumValues.size(); ++index) {
            const auto raw = enumValues.at(index);
            if (!raw.isString() && !raw.isDouble() && !raw.isBool()) continue;
            auto text = index < enumTexts.size() ? enumTexts.at(index) : QJsonValue{};
            auto entry = text.isString() ? text.toString()
                : text.isDouble() ? QString::number(text.toDouble(), 'g', 17)
                : QString{};
            if (entry.isEmpty()) entry = raw.toVariant().toString();
            bool duplicate = false;
            for (const auto& existing : descriptor.enumEntries)
                if (existing == entry.toStdString()) duplicate = true;
            if (duplicate) continue;
            descriptor.enumEntries.push_back(entry.toStdString());
            parameter.enumRawValues.push_back(raw);
            if (!rawValue.isNull() && rawValue == raw)
                descriptor.value = entry.toStdString();
        }
    } else if (type == QStringLiteral("boolean")) {
        descriptor.kind = DeviceParameterKind::Boolean;
        descriptor.representation = DeviceParameterRepresentation::Toggle;
        if (rawValue.isBool()) descriptor.value = rawValue.toBool();
    } else if (type == QStringLiteral("integer")) {
        descriptor.kind = DeviceParameterKind::Integer;
        descriptor.representation = DeviceParameterRepresentation::Number;
        descriptor.integerMinimum = integerBound(schema, "minimum");
        descriptor.integerMaximum = integerBound(schema, "maximum");
        descriptor.integerIncrement = integerBound(schema, "multipleOf");
        if (auto value = integerValue(rawValue)) descriptor.value = *value;
    } else if (type == QStringLiteral("number") || type == QStringLiteral("float")) {
        descriptor.kind = DeviceParameterKind::Float;
        descriptor.representation = DeviceParameterRepresentation::Number;
        descriptor.floatMinimum = numberBound(schema, "minimum");
        descriptor.floatMaximum = numberBound(schema, "maximum");
        descriptor.floatIncrement = numberBound(schema, "multipleOf");
        if (auto value = floatValue(rawValue)) descriptor.value = *value;
    } else {
        descriptor.kind = DeviceParameterKind::String;
        descriptor.representation = DeviceParameterRepresentation::Text;
        if (rawValue.isString()) descriptor.value = rawValue.toString().toStdString();
        else if (rawValue.isDouble()) descriptor.value = encodedValue(rawValue).toStdString();
    }

    parameter.rawValue = rawValue;
    parameters->push_back(std::move(parameter));
}

void flattenProperties(std::vector<ResourceParameter>* parameters, const QJsonObject& properties,
    const QJsonObject& values, const QString& basePath, bool inheritedReadOnly,
    const std::vector<std::string>& categoryPath, const std::string& scope,
    const std::string& connectionId)
{
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        const auto schema = it.value().toObject();
        const auto value = values.value(it.key());
        const auto path = basePath + QStringLiteral("/") + escapedToken(it.key());
        const bool readOnly = inheritedReadOnly || schema.value(QStringLiteral("readOnly")).toBool();
        const auto subProperties = schema.value(QStringLiteral("properties")).toObject();
        if (schema.value(QStringLiteral("type")).toString() == QStringLiteral("object")
            && !subProperties.isEmpty()) {
            std::vector<std::string> childPath = categoryPath;
            appendCategory(&childPath, displayName(schema, it.key()));
            flattenProperties(parameters, subProperties, value.toObject(), path, readOnly,
                childPath, scope, connectionId);
            continue;
        }
        if (schema.value(QStringLiteral("type")).toString() == QStringLiteral("object")) continue;
        addLeaf(parameters, path, it.key(), schema, value, readOnly, categoryPath, scope,
            connectionId);
    }
}

} // namespace

std::vector<ResourceParameter> flattenResource(const QJsonObject& schema, const QJsonObject& data,
    const std::string& scope, const std::string& connectionId)
{
    std::vector<ResourceParameter> parameters;
    const auto properties = schema.value(QStringLiteral("properties")).toObject();
    const bool resourceReadOnly = schema.value(QStringLiteral("readOnly")).toBool();
    const auto parameterSchema = properties.value(QStringLiteral("parameters")).toObject();
    if (!parameterSchema.isEmpty()) {
        const bool inheritedReadOnly = resourceReadOnly
            || parameterSchema.value(QStringLiteral("readOnly")).toBool();
        flattenProperties(&parameters,
            parameterSchema.value(QStringLiteral("properties")).toObject(),
            data.value(QStringLiteral("parameters")).toObject(), QStringLiteral("/parameters"),
            inheritedReadOnly, {}, scope, connectionId);
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            if (it.key() == QStringLiteral("parameters")) continue;
            flattenProperties(&parameters, QJsonObject{{it.key(), it.value().toObject()}}, data,
                QString{}, resourceReadOnly, {}, scope, connectionId);
        }
        return parameters;
    }

    // Firmware variants without a parameters envelope expose writable values at the root.
    flattenProperties(&parameters, properties, data, {}, resourceReadOnly, {}, scope, connectionId);
    return parameters;
}

std::string encodeResourceWrite(const ResourceParameter& parameter, const DeviceParameterValue& value)
{
    switch (parameter.descriptor.kind) {
    case DeviceParameterKind::Enumeration: {
        const auto entry = std::get_if<std::string>(&value);
        if (!entry) return {};
        for (std::size_t index = 0; index < parameter.descriptor.enumEntries.size(); ++index) {
            if (parameter.descriptor.enumEntries[index] != *entry) continue;
            if (index >= parameter.enumRawValues.size()) return {};
            return encodedValue(parameter.enumRawValues[index]).toStdString();
        }
        return {};
    }
    case DeviceParameterKind::Integer: {
        const auto number = std::get_if<std::int64_t>(&value);
        return number ? std::to_string(*number) : std::string{};
    }
    case DeviceParameterKind::Float: {
        const auto number = std::get_if<double>(&value);
        if (!number || !std::isfinite(*number)) return {};
        return encodedValue(QJsonValue(*number)).toStdString();
    }
    case DeviceParameterKind::Boolean: {
        const auto flag = std::get_if<bool>(&value);
        return flag ? (*flag ? "true" : "false") : std::string{};
    }
    case DeviceParameterKind::String: {
        const auto text = std::get_if<std::string>(&value);
        if (!text) return {};
        return encodedValue(QJsonValue(QString::fromStdString(*text))).toStdString();
    }
    case DeviceParameterKind::Command:
        return {};
    }
    return {};
}

} // namespace gocator
