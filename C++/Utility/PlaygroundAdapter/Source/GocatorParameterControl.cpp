#include "GocatorSourceController.h"
#include "GocatorSystemParameters.h"

#include <QtConcurrent>
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
}

DeviceParameterResult GocatorSourceController::scheduleParameterWork(std::function<void()> work)
{
    if (!_systemConnected()) return failure(Error::Disconnected, "Gocator is disconnected.");
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

std::vector<DeviceParameterDescriptor> GocatorSourceController::observeSystem(const Gocator::SystemState& state)
{
    if (!_lastSystemState || _lastSystemState->connectionGeneration != state.connectionGeneration
        || _lastSystemState->running != state.running || _lastSystemState->quickEditEnabled != state.quickEditEnabled)
        ++_parameterRevision;
    _lastSystemState = state;
    return gocator::systemParameters(state, _parameterIdentity, _parameterRevision);
}

DeviceParameterResult GocatorSourceController::requestParameters(InventoryCompletion completion)
{
    if (!completion) return failure(Error::InvalidArgument, "A completion is required.");
    return scheduleParameterWork([this, completion = std::move(completion)] {
        DeviceParameterInventory result;
        try { result.parameters = observeSystem(_readSystem()); }
        catch (const std::logic_error& error) { result.result = failure(Error::Disconnected, error.what()); }
        catch (const std::exception& error) { result.result = failure(Error::BackendFailure, error.what()); }
        catch (...) { result.result = failure(Error::BackendFailure, "Unexpected SDK failure."); }
        complete(completion, std::move(result));
    });
}

DeviceParameterResult GocatorSourceController::requestReadParameter(DeviceParameterTarget target, Completion completion)
{
    return parameterWork(std::move(target), {}, {}, std::move(completion));
}

DeviceParameterResult GocatorSourceController::requestWriteParameter(DeviceParameterTarget target,
    DeviceParameterValue value, std::optional<std::uint64_t> revision, Completion completion)
{
    return parameterWork(std::move(target), std::move(value), revision, std::move(completion));
}

DeviceParameterResult GocatorSourceController::requestExecuteParameter(DeviceParameterTarget,
    std::optional<std::uint64_t>, Completion completion)
{
    if (!completion) return failure(Error::InvalidArgument, "A completion is required.");
    return failure(Error::Unsupported, "The supported GoSystem controls expose no executable command or software trigger.");
}

DeviceParameterResult GocatorSourceController::parameterWork(DeviceParameterTarget target,
    std::optional<DeviceParameterValue> value, std::optional<std::uint64_t> revision, Completion completion)
{
    if (!completion) return failure(Error::InvalidArgument, "A completion is required.");
    for (const auto* field : {&target.connectionId, &target.scope, &target.name})
        if (field->empty() || field->find('\0') != std::string::npos)
            return failure(Error::InvalidArgument, "Exact target fields are required.");
    if (target.scope != "System")
        return failure(Error::Unsupported, "Only official GoSystem parameters are supported by this capability.");
    if (!target.selectors.empty()) return failure(Error::Conflict, "System controls have no selector context.");
    if (target.name != "runningState" && target.name != "quickEditEnabled")
        return failure(Error::NotFound, "Unknown System parameter.");
    return scheduleParameterWork([this, target = std::move(target), value = std::move(value), revision,
                                  completion = std::move(completion)] {
        DeviceParameterResult result;
        bool dispatched = false;
        try {
            const auto observed = _readSystem();
            auto parameters = observeSystem(observed);
            const auto index = target.name == "runningState" ? 0U : 1U;
            result.descriptor = parameters[index];
            if (target.connectionId != result.descriptor->target.connectionId)
                result = failure(Error::Conflict, "The connection generation changed.");
            else if (value) {
                const auto validation = validateDeviceParameterWrite(*result.descriptor, target, value, revision);
                if (validation != Error::None) result = failure(validation, "Fresh System observation refused the write.");
                else {
                    dispatched = true;
                    _writeQuickEdit(std::get<bool>(*value), observed);
                    result.effect = Effect::Applied;
                    // A failed readback preserves the acknowledged effect and removes stale observation.
                    result.descriptor.reset();
                    result.descriptor = observeSystem(_readSystem())[index];
                }
            }
        } catch (const Gocator::SystemControlError& error) {
            result = failure(Error::BackendFailure, error.what());
            result.effect = error.mayHaveApplied ? Effect::Unknown : Effect::None;
        } catch (const std::logic_error& error) {
            // State conflicts precede effects; a disconnect after acknowledged mutation retains Applied.
            const auto effect = result.effect;
            result = failure(_systemConnected() ? Error::Conflict : Error::Disconnected, error.what());
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
