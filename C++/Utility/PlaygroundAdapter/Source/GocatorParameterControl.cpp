#include "GocatorSourceController.h"
#include "GocatorSystemParameters.h"

#include <QtConcurrent>
#include <algorithm>
#include <stdexcept>

namespace {
using Error = DeviceParameterError;
using Effect = DeviceParameterEffect;

/** @brief Creates a pre-effect refusal. */
DeviceParameterResult failure(Error error, std::string message)
{
    return {error, std::move(message), Effect::None, {}};
}

/** @brief Isolates consumer exceptions after delivering the one copied completion. */
template<class Callback, class Value>
void complete(const Callback& callback, Value value)
{
    try { callback(std::move(value)); } catch (...) {}
}

const DeviceParameterDescriptor* findParameter(
    const std::vector<DeviceParameterDescriptor>& parameters, const DeviceParameterTarget& target)
{
    for (const auto& parameter : parameters) {
        if (parameter.target.connectionId == target.connectionId
            && parameter.target.scope == target.scope
            && parameter.target.name == target.name)
            return &parameter;
    }
    return nullptr;
}

const gocator::ResourceParameter* findResourceParameter(
    const std::vector<gocator::ResourceParameter>& parameters, const DeviceParameterTarget& target)
{
    for (const auto& parameter : parameters) {
        if (parameter.descriptor.target.scope == target.scope
            && parameter.descriptor.target.name == target.name)
            return &parameter;
    }
    return nullptr;
}
}

DeviceParameterResult GocatorSourceController::scheduleParameterWork(std::function<void()> work)
{
    if (!_systemConnected || !_systemConnected()) return failure(Error::Disconnected, "Gocator is disconnected.");
    if (_parameterBusy.exchange(true)) return failure(Error::Busy, "A parameter operation is already active.");
    try {
        _parameterWork = QtConcurrent::run([this, work = std::move(work)] {
            struct Release { std::atomic<bool>& busy; ~Release() { busy.store(false); } } release{_parameterBusy};
            work();
        });
    } catch (...) {
        _parameterBusy.store(false);
        return failure(Error::BackendFailure, "Could not schedule parameter work.");
    }
    return {};
}

DeviceParameterResult GocatorSourceController::requestParameters(InventoryCompletion completion)
{
    if (!completion) return failure(Error::InvalidArgument, "A completion is required.");
    return scheduleParameterWork([this, completion = std::move(completion)] {
        DeviceParameterInventory result;
        try { result.parameters = observeAll().parameters; }
        catch (const std::logic_error& error) { result.result = failure(Error::Disconnected, error.what()); }
        catch (const std::exception& error) { result.result = failure(Error::BackendFailure, error.what()); }
        catch (...) { result.result = failure(Error::BackendFailure, "Unexpected SDK failure."); }
        complete(completion, std::move(result));
    });
}

DeviceParameterResult GocatorSourceController::requestReadParameter(DeviceParameterTarget target, ParameterCompletion completion)
{
    return parameterWork(std::move(target), {}, {}, std::move(completion));
}

DeviceParameterResult GocatorSourceController::requestWriteParameter(DeviceParameterTarget target,
    DeviceParameterValue value, std::optional<std::uint64_t> revision, ParameterCompletion completion)
{
    return parameterWork(std::move(target), std::move(value), revision, std::move(completion));
}

DeviceParameterResult GocatorSourceController::requestExecuteParameter(DeviceParameterTarget,
    std::optional<std::uint64_t>, ParameterCompletion completion)
{
    if (!completion) return failure(Error::InvalidArgument, "A completion is required.");
    return failure(Error::Unsupported, "The supported GoSystem controls expose no executable command or software trigger.");
}

DeviceParameterResult GocatorSourceController::parameterWork(DeviceParameterTarget target,
    std::optional<DeviceParameterValue> value, std::optional<std::uint64_t> revision, ParameterCompletion completion)
{
    if (!completion) return failure(Error::InvalidArgument, "A completion is required.");
    for (const auto* field : {&target.connectionId, &target.scope, &target.name})
        if (field->empty() || field->find('\0') != std::string::npos)
            return failure(Error::InvalidArgument, "Exact target fields are required.");
    if (target.scope != "System" && target.scope != "Scanner" && target.scope != "Sensor")
        return failure(Error::Unsupported, "Unsupported Gocator parameter scope.");
    if (!target.selectors.empty()) return failure(Error::Conflict, "Gocator parameters have no selector context.");
    if (target.scope == "System" && target.name != "runningState" && target.name != "quickEditEnabled")
        return failure(Error::NotFound, "Unknown System parameter.");
    if (target.scope != "System" && target.name.front() != '/')
        return failure(Error::InvalidArgument, "Resource parameters use a JSON pointer path.");

    return scheduleParameterWork([this, target = std::move(target), value = std::move(value), revision,
                                  completion = std::move(completion)] {
        DeviceParameterResult result;
        bool dispatched = false;
        try {
            auto observation = observeAll();
            const auto* current = findParameter(observation.parameters, target);
            if (!current) {
                // A stale connection id is a conflict, not an unknown feature.
                for (const auto& parameter : observation.parameters) {
                    if (parameter.target.scope == target.scope && parameter.target.name == target.name
                        && parameter.target.connectionId != target.connectionId) {
                        result = failure(Error::Conflict, "The connection generation changed.");
                        complete(completion, std::move(result));
                        return;
                    }
                }
                result = failure(Error::NotFound, "Unknown Gocator parameter for this connection.");
                complete(completion, std::move(result));
                return;
            }
            result.descriptor = *current;
            if (!value) {
                complete(completion, std::move(result));
                return;
            }

            const auto validation = validateDeviceParameterWrite(*current, target, value, revision);
            if (validation != Error::None) {
                result = failure(validation, "Fresh Gocator observation refused the write.");
                complete(completion, std::move(result));
                return;
            }

            dispatched = true;
            if (target.scope == "System") {
                if (!observation.systemState) throw std::logic_error("The System observation is missing.");
                _writeQuickEdit(std::get<bool>(*value), observation.systemState);
            } else {
                const auto* resource = findResourceParameter(observation.resourceParameters, target);
                if (!resource) throw std::logic_error("The resource parameter disappeared before the write.");
                const auto encoded = gocator::encodeResourceWrite(*resource, *value);
                if (encoded.empty()) throw std::invalid_argument("The value cannot be encoded for GoPxL.");
                const auto write = _writeParameter
                    ? _writeParameter(target.scope == "Scanner" ? Gocator::ParameterTarget::Scanner
                                                               : Gocator::ParameterTarget::Sensor,
                          target.name, encoded)
                    : Gocator::ParameterWriteResult{false, true, "The Gocator write route is unavailable."};
                if (!write.applied) {
                    result = failure(Error::BackendFailure, write.error.empty()
                        ? "GoPxL refused the parameter write." : write.error);
                    result.effect = write.mayHaveApplied ? Effect::Unknown : Effect::None;
                }
            }
            if (result.error == Error::None) result.effect = Effect::Applied;

            // Readback is the source of truth even when the write was refused or uncertain.
            result.descriptor.reset();
            try {
                auto after = observeAll();
                if (const auto* readback = findParameter(after.parameters, target))
                    result.descriptor = *readback;
            } catch (const std::logic_error& error) {
                // A disconnect after an acknowledged mutation keeps Applied; a prior SDK refusal
                // still owns the reason.
                const auto effect = result.effect;
                const auto priorError = result.error;
                const auto priorMessage = result.message;
                result = failure(_systemConnected && _systemConnected()
                    ? Error::Conflict : Error::Disconnected, error.what());
                result.effect = effect;
                result.descriptor.reset();
                if (priorError != Error::None) {
                    result.error = priorError;
                    result.message = priorMessage;
                }
            } catch (const std::exception& error) {
                // Preserve the acknowledged effect and a prior SDK refusal reason.
                const auto effect = result.effect;
                if (result.error == Error::None) {
                    result.error = Error::BackendFailure;
                    result.message = error.what();
                }
                result.effect = effect;
                result.descriptor.reset();
            }
        } catch (const Gocator::SystemControlError& error) {
            result = failure(Error::BackendFailure, error.what());
            result.effect = error.mayHaveApplied ? Effect::Unknown : Effect::None;
        } catch (const std::invalid_argument& error) {
            result = failure(Error::InvalidArgument, error.what());
        } catch (const std::logic_error& error) {
            // State conflicts precede effects; a disconnect after acknowledged mutation retains Applied.
            const auto effect = result.effect;
            result = failure(_systemConnected && _systemConnected() ? Error::Conflict : Error::Disconnected,
                error.what());
            result.effect = effect;
        } catch (const std::exception& error) {
            result.error = Error::BackendFailure;
            result.message = error.what();
            if (dispatched && result.effect != Effect::Applied) result.effect = Effect::Unknown;
            result.descriptor.reset();
        } catch (...) {
            result.error = Error::BackendFailure;
            result.message = "Unexpected SDK failure.";
            if (dispatched && result.effect != Effect::Applied) result.effect = Effect::Unknown;
            result.descriptor.reset();
        }
        complete(completion, std::move(result));
    });
}
