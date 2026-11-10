#include <QtTest/QtTest>
#include "Utility/PlaygroundAdapter/Source/GocatorSourceController.h"

#include <future>
#include <stdexcept>

class GocatorParameterControlTests : public QObject {
    Q_OBJECT
private slots:
    void sdkSystemWorkflow();
    void preservesAcknowledgedEffectWhenReadbackFails();
    void unsupportedAndDisconnectedHaveNoEffects();
    void failedSdkDispatchHasUnknownEffect();
    void busyAdmissionAndTeardownDrain();
};

namespace {
template<class Result, class Request>
Result awaitResult(GocatorSourceController& controller, Request request)
{
    auto promise = std::make_shared<std::promise<Result>>();
    auto future = promise->get_future();
    const auto admission = request([promise](Result result) { promise->set_value(std::move(result)); });
    if (admission.error != DeviceParameterError::None) throw std::runtime_error("Unexpected admission refusal");
    if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        throw std::runtime_error("Missing parameter completion");
    auto result = future.get();
    // Completion runs before the busy flag is released; destruction drains the same worker.
    Q_UNUSED(controller);
    return result;
}
}

void GocatorParameterControlTests::sdkSystemWorkflow()
{
    using Error = DeviceParameterError;
    using Effect = DeviceParameterEffect;
    GocatorSourceController controller(nullptr);
    Gocator::SystemState state{7, Gocator::SystemState::Running::Running, false};
    int writes = 0;
    controller._systemConnected = [] { return true; };
    controller._readSystem = [&] { return state; };
    controller._writeQuickEdit = [&](bool enabled, std::optional<Gocator::SystemState> expected) {
        if (!expected || expected->connectionGeneration != state.connectionGeneration
            || expected->quickEditEnabled != state.quickEditEnabled) throw std::logic_error("changed");
        ++writes;
        state.quickEditEnabled = enabled;
    };
    QVERIFY(qobject_cast<IDeviceParameterControl*>(&controller));
    auto inventory = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    QCOMPARE(inventory.result.error, Error::None);
    QCOMPARE(inventory.parameters.size(), std::size_t{2});
    const auto running = inventory.parameters[0];
    const auto quick = inventory.parameters[1];
    QCOMPARE(running.target.scope, std::string("System"));
    QCOMPARE(std::get<std::string>(*running.value), std::string("running"));
    QVERIFY(!running.writable);
    QCOMPARE(quick.target.name, std::string("quickEditEnabled"));
    QCOMPARE(quick.kind, DeviceParameterKind::Boolean);
    QVERIFY(!std::get<bool>(*quick.value));

    const auto write = [&](const DeviceParameterDescriptor& parameter, DeviceParameterValue value,
                           std::optional<std::uint64_t> revision) {
        auto result = awaitResult<DeviceParameterResult>(controller, [&](auto done) {
            return controller.requestWriteParameter(parameter.target, value, revision, done);
        });
        controller._parameterWork.waitForFinished();
        return result;
    };
    QCOMPARE(write(running, true, {}).error, Error::ReadOnly);
    QCOMPARE(write(quick, std::string("true"), {}).error, Error::InvalidArgument);
    QCOMPARE(write(quick, true, quick.revision + 1).error, Error::Conflict);
    QCOMPARE(writes, 0);
    auto changed = write(quick, true, quick.revision);
    QCOMPARE(changed.error, Error::None);
    QCOMPARE(changed.effect, Effect::Applied);
    QVERIFY(changed.descriptor);
    QVERIFY(std::get<bool>(*changed.descriptor->value));
    QVERIFY(changed.descriptor->revision > quick.revision);
    QCOMPARE(writes, 1);
    QCOMPARE(write(quick, false, quick.revision).error, Error::Conflict);
    ++state.connectionGeneration;
    QCOMPARE(write(*changed.descriptor, false, {}).error, Error::Conflict);
    QCOMPARE(writes, 1);
}

void GocatorParameterControlTests::preservesAcknowledgedEffectWhenReadbackFails()
{
    GocatorSourceController controller(nullptr);
    Gocator::SystemState state{1, Gocator::SystemState::Running::Ready, false};
    bool failRead = false;
    controller._systemConnected = [] { return true; };
    controller._readSystem = [&] {
        if (failRead) throw std::runtime_error("SDK readback failed");
        return state;
    };
    controller._writeQuickEdit = [&](bool enabled, auto) { state.quickEditEnabled = enabled; failRead = true; };
    auto inventory = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    const auto quick = inventory.parameters[1];
    auto result = awaitResult<DeviceParameterResult>(controller, [&](auto done) {
        return controller.requestWriteParameter(quick.target, true, quick.revision, done);
    });
    controller._parameterWork.waitForFinished();
    QCOMPARE(result.error, DeviceParameterError::BackendFailure);
    QCOMPARE(result.effect, DeviceParameterEffect::Applied);
    QVERIFY(!result.descriptor);
    QVERIFY(state.quickEditEnabled);
}

void GocatorParameterControlTests::unsupportedAndDisconnectedHaveNoEffects()
{
    Gocator device;
    GocatorSourceController controller(&device);
    int completions = 0;
    auto done = [&](auto) { ++completions; };
    QCOMPARE(controller.requestParameters(done).error, DeviceParameterError::Disconnected);
    QCOMPARE(controller.requestReadParameter({"id", "Scanner", "value", {}}, done).error,
        DeviceParameterError::Unsupported);
    QCOMPARE(controller.requestExecuteParameter({"id", "System", "TriggerSoftware", {}}, {}, done).error,
        DeviceParameterError::Unsupported);
    QCOMPARE(completions, 0);
}

void GocatorParameterControlTests::failedSdkDispatchHasUnknownEffect()
{
    GocatorSourceController controller(nullptr);
    controller._systemConnected = [] { return true; };
    controller._readSystem = [] { return Gocator::SystemState{1, Gocator::SystemState::Running::Ready, false}; };
    controller._writeQuickEdit = [](bool, auto) { throw std::runtime_error("SDK reply lost"); };
    auto inventory = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    auto result = awaitResult<DeviceParameterResult>(controller, [&](auto done) {
        return controller.requestWriteParameter(inventory.parameters[1].target, true, {}, done);
    });
    controller._parameterWork.waitForFinished();
    QCOMPARE(result.error, DeviceParameterError::BackendFailure);
    QCOMPARE(result.effect, DeviceParameterEffect::Unknown);
    QVERIFY(!result.descriptor);
}

void GocatorParameterControlTests::busyAdmissionAndTeardownDrain()
{
    std::atomic<int> completions{0};
    auto gate = std::make_shared<std::promise<void>>();
    auto released = gate->get_future().share();
    {
        GocatorSourceController controller(nullptr);
        controller._systemConnected = [] { return true; };
        controller._readSystem = [released] { released.wait(); return Gocator::SystemState{}; };
        QCOMPARE(controller.requestParameters([&](auto) { ++completions; }).error, DeviceParameterError::None);
        QCOMPARE(controller.requestParameters([&](auto) { ++completions; }).error, DeviceParameterError::Busy);
        gate->set_value();
        // Destruction must finish the accepted callback before returning.
    }
    QCOMPARE(completions.load(), 1);
}

QTEST_MAIN(GocatorParameterControlTests)
#include "GocatorParameterControlTests.moc"
