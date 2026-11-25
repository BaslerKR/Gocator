#include <QtTest/QtTest>
#include "Utility/PlaygroundAdapter/Source/GocatorSourceController.h"
#include "Utility/PlaygroundAdapter/Source/GocatorParameterNotifications.h"
#include "Utility/PlaygroundAdapter/GocatorDataSetGraphicsFrameAdapter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <future>
#include <mutex>
#include <stdexcept>
#include <vector>

class GocatorParameterControlTests : public QObject {
    Q_OBJECT
private slots:
    void mapsAndSamplesRangeRejections();
    void sdkSystemWorkflow();
    void resourceTreeStructure();
    void resourceWriteUsesObservationsAndReadback();
    void refusedResourceWriteKeepsSdkReason();
    void preservesAcknowledgedEffectWhenReadbackFails();
    void disconnectDuringReadbackKeepsDisconnectedAndEffect();
    void unsupportedAndDisconnectedHaveNoEffects();
    void failedSdkDispatchHasUnknownEffect();
    void writeWhileRunningFollowsSensorResult();
    void busyAdmissionAndTeardownDrain();
    void subscriptionSnapshotAndDelta();
    void otherObservationsPublishExternalChanges();
    void subscriptionReobservesWritability();
    void resourceReadFailureIsReported();
    void removedLeafRevisionStaysMonotonic();
    void changeQueueOverflowBecomesResyncSnapshot();
    void deliveryKeepsSequenceOrderAcrossThreads();
    void releaseStopsDelivery();
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

const DeviceParameterDescriptor* find(const std::vector<DeviceParameterDescriptor>& parameters,
    const std::string& scope, const std::string& name)
{
    for (const auto& parameter : parameters)
        if (parameter.target.scope == scope && parameter.target.name == name) return &parameter;
    return nullptr;
}
} // namespace

/** @brief Copies a GoPxL resource pair and records every explicit write. */
struct ResourceFake {
    ResourceFake()
    {
        const QJsonObject scanFields{
            {"scanMode", QJsonObject{{"type", "integer"}, {"title", "Scan mode"},
                {"enum", QJsonArray{2, 3}}, {"enumText", QJsonArray{"Profile", "Surface"}}}},
            {"scanLengthMm", QJsonObject{{"type", "number"}, {"title", "Scan length"},
                {"unit", "mm"}, {"minimum", 1}, {"maximum", 1000}}},
            {"intensityEnabled", QJsonObject{{"type", "boolean"}, {"title", "Intensity"}}}};
        const QJsonObject scannerFields{
            {"scanModeSettings", QJsonObject{{"type", "object"}, {"title", "Scan mode settings"},
                {"properties", scanFields}}},
            {"name", QJsonObject{{"type", "string"}, {"title", "Name"}, {"readOnly", true}}},
            {"mode", QJsonObject{{"type", "integer"}, {"title", "Mode"}, {"readOnly", true}}}};
        scannerSchema = QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"parameters", QJsonObject{{"type", "object"}, {"properties", scannerFields}}}}}};
        scannerData = QJsonObject{{"parameters", QJsonObject{
            {"scanModeSettings", QJsonObject{{"scanMode", 2}, {"scanLengthMm", 20.0},
                {"intensityEnabled", true}}},
            {"name", "scanner-0"}, {"mode", 7}}}};

        const QJsonObject exposureFields{
            {"exposureMode", QJsonObject{{"type", "integer"}, {"title", "Exposure mode"}}},
            {"singleExposure", QJsonObject{{"type", "integer"}, {"title", "Exposure"},
                {"unit", "us"}, {"minimum", 10}, {"maximum", 100000}}}};
        sensorSchema = QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"parameters", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
                {"exposureSettings", QJsonObject{{"type", "object"}, {"title", "Exposure"},
                    {"properties", exposureFields}}}}}}}}}};
        sensorData = QJsonObject{{"parameters", QJsonObject{
            {"exposureSettings", QJsonObject{{"exposureMode", 0}, {"singleExposure", 1000}}}}}};
    }

    std::mutex mutex;
    QJsonObject scannerSchema;
    QJsonObject scannerData;
    QJsonObject sensorSchema;
    QJsonObject sensorData;
    std::vector<std::string> writes;
    bool failReads = false;
    bool failReadsAfterWrite = false;
    Gocator::ParameterWriteResult nextWrite{true, false, {}};
    std::function<void(const std::string&, const std::string&)> afterWrite;

    GocatorSourceController::ResourceSnapshot read(Gocator::ParameterTarget target)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (failReads) throw std::runtime_error("fake resource read failure");
        const auto schema = target == Gocator::ParameterTarget::Scanner ? scannerSchema : sensorSchema;
        const auto data = target == Gocator::ParameterTarget::Scanner ? scannerData : sensorData;
        return {QJsonDocument(schema).toJson(QJsonDocument::Compact).toStdString(),
            QJsonDocument(data).toJson(QJsonDocument::Compact).toStdString()};
    }
};

struct GocatorParameterTestAccess {
    static bool beginRangeRun(GocatorSourceController& controller, bool single)
    {
        controller._lifecycle.grab = [](std::size_t) { return true; };
        controller._singleInFlight.store(single, std::memory_order_relaxed);
        return controller.beginGrab(single ? 1U : 0U);
    }

    static void rejectRange(GocatorSourceController& controller, const GocatorRangeRejection& evidence,
                            std::size_t sequence)
    {
        controller.reportRangeRejection(evidence, sequence);
    }

    static void bind(GocatorSourceController& controller, ResourceFake& fake)
    {
        controller._systemConnected = [] { return true; };
        controller._readSystem = [] {
            return Gocator::SystemState{3, Gocator::SystemState::Running::Ready, false};
        };
        controller._readResource = [&fake](Gocator::ParameterTarget target) { return fake.read(target); };
        controller._writeParameter = [&fake](Gocator::ParameterTarget, const std::string& path,
            const std::string& value) {
            std::lock_guard<std::mutex> lock(fake.mutex);
            fake.writes.push_back(path + "=" + value);
            const auto result = fake.nextWrite;
            if (result.applied && fake.afterWrite) fake.afterWrite(path, value);
            if (fake.failReadsAfterWrite) fake.failReads = true;
            return result;
        };
        controller._lifecycleAvailable = true;
        controller._lifecycle.snapshot = [] {
            return Gocator::ConnectionSnapshot{true, 3, "10.0.0.5", "10.0.0.5"};
        };
    }
};

void bindResourceFake(GocatorSourceController& controller, ResourceFake& fake)
{
    GocatorParameterTestAccess::bind(controller, fake);
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

void GocatorParameterControlTests::resourceTreeStructure()
{
    using Error = DeviceParameterError;
    ResourceFake fake;
    GocatorSourceController controller(nullptr);
    bindResourceFake(controller, fake);

    auto inventory = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    QCOMPARE(inventory.result.error, Error::None);
    QCOMPARE(inventory.parameters.size(), std::size_t{9});
    QCOMPARE(inventory.parameters[0].target.scope, std::string("System"));
    QCOMPARE(inventory.parameters[1].target.scope, std::string("System"));

    const auto* scanMode = find(inventory.parameters, "Scanner", "/parameters/scanModeSettings/scanMode");
    const auto* scanLength = find(inventory.parameters, "Scanner", "/parameters/scanModeSettings/scanLengthMm");
    const auto* name = find(inventory.parameters, "Scanner", "/parameters/name");
    const auto* exposure = find(inventory.parameters, "Sensor", "/parameters/exposureSettings/singleExposure");
    QVERIFY(scanMode);
    QVERIFY(scanLength);
    QVERIFY(name);
    QVERIFY(exposure);
    QCOMPARE(scanMode->kind, DeviceParameterKind::Enumeration);
    QCOMPARE(scanMode->representation, DeviceParameterRepresentation::Choice);
    QVERIFY(scanMode->enumEntries == (std::vector<std::string>{"Profile", "Surface"}));
    QCOMPARE(std::get<std::string>(*scanMode->value), std::string("Profile"));
    QVERIFY(scanMode->categoryPath == std::vector<std::string>({"Scan mode settings"}));
    QVERIFY(scanMode->writable);
    QCOMPARE(scanLength->kind, DeviceParameterKind::Float);
    QCOMPARE(scanLength->representation, DeviceParameterRepresentation::Number);
    QCOMPARE(scanLength->unit, std::string("mm"));
    QVERIFY(scanLength->floatMinimum && *scanLength->floatMinimum == 1.0);
    QVERIFY(scanLength->floatMaximum && *scanLength->floatMaximum == 1000.0);
    QCOMPARE(name->kind, DeviceParameterKind::String);
    QVERIFY(!name->writable);
    QCOMPARE(exposure->kind, DeviceParameterKind::Integer);
    QVERIFY(exposure->categoryPath == std::vector<std::string>({"Exposure"}));
    QVERIFY(scanMode->revision > 0);
}

void GocatorParameterControlTests::resourceWriteUsesObservationsAndReadback()
{
    using Error = DeviceParameterError;
    using Effect = DeviceParameterEffect;
    ResourceFake fake;
    GocatorSourceController controller(nullptr);
    bindResourceFake(controller, fake);
    fake.afterWrite = [&fake](const std::string& path, const std::string& value) {
        if (path != "/parameters/scanModeSettings/scanMode") return;
        auto parameters = fake.scannerData["parameters"].toObject();
        auto settings = parameters["scanModeSettings"].toObject();
        settings["scanMode"] = std::stoi(value);
        parameters["scanModeSettings"] = settings;
        fake.scannerData["parameters"] = parameters;
    };

    auto inventory = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    const auto* scanMode = find(inventory.parameters, "Scanner", "/parameters/scanModeSettings/scanMode");
    QVERIFY(scanMode);
    auto result = awaitResult<DeviceParameterResult>(controller, [&](auto done) {
        return controller.requestWriteParameter(scanMode->target, std::string("Surface"),
            scanMode->revision, done);
    });
    controller._parameterWork.waitForFinished();
    QCOMPARE(result.error, Error::None);
    QCOMPARE(result.effect, Effect::Applied);
    QVERIFY(result.descriptor);
    QCOMPARE(std::get<std::string>(*result.descriptor->value), std::string("Surface"));
    QVERIFY(result.descriptor->revision > scanMode->revision);
    QCOMPARE(fake.writes.size(), std::size_t{1});
    QCOMPARE(fake.writes.front(), std::string("/parameters/scanModeSettings/scanMode=3"));
}

void GocatorParameterControlTests::refusedResourceWriteKeepsSdkReason()
{
    using Error = DeviceParameterError;
    using Effect = DeviceParameterEffect;
    ResourceFake fake;
    GocatorSourceController controller(nullptr);
    bindResourceFake(controller, fake);
    fake.nextWrite = {false, false, "GoPxL refused /parameters/scanModeSettings/scanMode while running"};

    auto inventory = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    const auto* scanMode = find(inventory.parameters, "Scanner", "/parameters/scanModeSettings/scanMode");
    QVERIFY(scanMode);
    auto refused = awaitResult<DeviceParameterResult>(controller, [&](auto done) {
        return controller.requestWriteParameter(scanMode->target, std::string("Surface"), {}, done);
    });
    controller._parameterWork.waitForFinished();
    QCOMPARE(refused.error, Error::BackendFailure);
    QCOMPARE(refused.effect, Effect::None);
    QVERIFY(QString::fromStdString(refused.message).contains(
        QStringLiteral("GoPxL refused /parameters/scanModeSettings/scanMode while running")));
    QVERIFY(refused.descriptor);

    fake.nextWrite = {false, true, "GoPxL write reply lost"};
    auto unknown = awaitResult<DeviceParameterResult>(controller, [&](auto done) {
        return controller.requestWriteParameter(scanMode->target, std::string("Surface"), {}, done);
    });
    controller._parameterWork.waitForFinished();
    QCOMPARE(unknown.error, Error::BackendFailure);
    QCOMPARE(unknown.effect, Effect::Unknown);
    QVERIFY(QString::fromStdString(unknown.message).contains(QStringLiteral("GoPxL write reply lost")));

    // A readback failure must not replace the SDK refusal reason.
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        fake.failReads = false;
        fake.failReadsAfterWrite = true;
        fake.nextWrite = {false, false, "GoPxL refused the scan mode"};
    }
    auto refusedWithFailedReadback = awaitResult<DeviceParameterResult>(controller, [&](auto done) {
        return controller.requestWriteParameter(scanMode->target, std::string("Surface"), {}, done);
    });
    controller._parameterWork.waitForFinished();
    QCOMPARE(refusedWithFailedReadback.error, Error::BackendFailure);
    QCOMPARE(refusedWithFailedReadback.effect, Effect::None);
    QVERIFY(QString::fromStdString(refusedWithFailedReadback.message).contains(
        QStringLiteral("GoPxL refused the scan mode")));
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

void GocatorParameterControlTests::disconnectDuringReadbackKeepsDisconnectedAndEffect()
{
    GocatorSourceController controller(nullptr);
    Gocator::SystemState state{1, Gocator::SystemState::Running::Ready, false};
    bool connected = true;
    controller._systemConnected = [&] { return connected; };
    controller._readSystem = [&] {
        if (!connected) throw std::logic_error("Gocator is disconnected.");
        return state;
    };
    controller._writeQuickEdit = [&](bool enabled, auto) {
        state.quickEditEnabled = enabled;
        connected = false;
    };
    auto inventory = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    auto result = awaitResult<DeviceParameterResult>(controller, [&](auto done) {
        return controller.requestWriteParameter(inventory.parameters[1].target, true, {}, done);
    });
    controller._parameterWork.waitForFinished();
    QCOMPARE(result.error, DeviceParameterError::Disconnected);
    QCOMPARE(result.effect, DeviceParameterEffect::Applied);
    QVERIFY(!result.descriptor);
}

void GocatorParameterControlTests::unsupportedAndDisconnectedHaveNoEffects()
{
    Gocator device;
    GocatorSourceController controller(&device);
    int completions = 0;
    auto done = [&](auto) { ++completions; };
    QCOMPARE(controller.requestParameters(done).error, DeviceParameterError::Disconnected);
    QCOMPARE(controller.requestReadParameter({"id", "Interface", "value", {}}, done).error,
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

void GocatorParameterControlTests::writeWhileRunningFollowsSensorResult()
{
    using Error = DeviceParameterError;
    using Effect = DeviceParameterEffect;
    GocatorSourceController controller(nullptr);
    Gocator::SystemState state{5, Gocator::SystemState::Running::Running, false};
    int writes = 0;
    controller._systemConnected = [] { return true; };
    controller._readSystem = [&] { return state; };
    controller._writeQuickEdit = [&](bool enabled, auto) {
        ++writes;
        state.quickEditEnabled = enabled;
    };

    // The controller has no blanket "grabbing" refusal: a running sensor that accepts the write
    // reports Applied. The fake SDK is the only authority here.
    auto inventory = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    const auto quick = inventory.parameters[1];
    auto accepted = awaitResult<DeviceParameterResult>(controller, [&](auto done) {
        return controller.requestWriteParameter(quick.target, true, quick.revision, done);
    });
    controller._parameterWork.waitForFinished();
    QCOMPARE(accepted.error, Error::None);
    QCOMPARE(accepted.effect, Effect::Applied);
    QCOMPARE(writes, 1);

    // When GoSystem/GoPxL refuses the same write while running, its reason reaches the caller
    // unchanged and no effect is claimed.
    controller._writeQuickEdit = [&](bool, auto) {
        ++writes;
        throw Gocator::SystemControlError("GoPxL refused the write while running", false);
    };
    QVERIFY(accepted.descriptor);
    auto refused = awaitResult<DeviceParameterResult>(controller, [&](auto done) {
        return controller.requestWriteParameter(accepted.descriptor->target, false,
            accepted.descriptor->revision, done);
    });
    controller._parameterWork.waitForFinished();
    QCOMPARE(refused.error, Error::BackendFailure);
    QCOMPARE(refused.effect, Effect::None);
    QVERIFY(QString::fromStdString(refused.message).contains(
        QStringLiteral("GoPxL refused the write while running")));
    QCOMPARE(writes, 2);

    // An uncertain SDK completion keeps the reason and reports Unknown.
    controller._writeQuickEdit = [&](bool, auto) {
        ++writes;
        throw Gocator::SystemControlError("GoPxL write reply lost", true);
    };
    auto unknown = awaitResult<DeviceParameterResult>(controller, [&](auto done) {
        return controller.requestWriteParameter(accepted.descriptor->target, false,
            accepted.descriptor->revision, done);
    });
    controller._parameterWork.waitForFinished();
    QCOMPARE(unknown.error, Error::BackendFailure);
    QCOMPARE(unknown.effect, Effect::Unknown);
    QVERIFY(QString::fromStdString(unknown.message).contains(QStringLiteral("reply lost")));
    QCOMPARE(writes, 3);
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

void GocatorParameterControlTests::subscriptionSnapshotAndDelta()
{
    ResourceFake fake;
    GocatorSourceController controller(nullptr);
    bindResourceFake(controller, fake);
    auto inventory = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    const auto connection = inventory.parameters.front().target.connectionId;

    std::mutex mutex;
    std::vector<DeviceParameterChange> changes;
    const auto delivery = [&](DeviceParameterChange change) {
        std::lock_guard<std::mutex> lock(mutex);
        changes.push_back(std::move(change));
    };
    QCOMPARE(controller.requestParameterChanges(connection, "watch", delivery).error,
        DeviceParameterError::None);
    QVERIFY(QTest::qWaitFor([&] {
        std::lock_guard<std::mutex> lock(mutex);
        return !changes.empty();
    }, 3000));
    {
        std::lock_guard<std::mutex> lock(mutex);
        QCOMPARE(changes.front().kind, DeviceParameterChangeKind::Snapshot);
        QCOMPARE(changes.front().sequence, std::uint64_t{1});
        QCOMPARE(changes.front().descriptors.size(), std::size_t{9});
        changes.clear();
    }

    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        auto parameters = fake.scannerData["parameters"].toObject();
        auto settings = parameters["scanModeSettings"].toObject();
        settings["scanMode"] = 3;
        parameters["scanModeSettings"] = settings;
        fake.scannerData["parameters"] = parameters;
    }
    controller.wakeObservationThread();
    QVERIFY(QTest::qWaitFor([&] {
        std::lock_guard<std::mutex> lock(mutex);
        return !changes.empty();
    }, 3000));
    {
        std::lock_guard<std::mutex> lock(mutex);
        QCOMPARE(changes.front().kind, DeviceParameterChangeKind::Delta);
        QCOMPARE(changes.front().sequence, std::uint64_t{2});
        QCOMPARE(changes.front().descriptors.size(), std::size_t{1});
        QCOMPARE(changes.front().descriptors.front().target.name,
            std::string("/parameters/scanModeSettings/scanMode"));
        QCOMPARE(std::get<std::string>(*changes.front().descriptors.front().value), std::string("Surface"));
        changes.clear();
    }

    bool released = false;
    QCOMPARE(controller.releaseParameterChanges(connection, "watch", [&](auto) { released = true; }).error,
        DeviceParameterError::None);
    QVERIFY(released);
}

void GocatorParameterControlTests::otherObservationsPublishExternalChanges()
{
    ResourceFake fake;
    GocatorSourceController controller(nullptr);
    bindResourceFake(controller, fake);
    auto inventory = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    const auto connection = inventory.parameters.front().target.connectionId;

    std::mutex mutex;
    std::vector<DeviceParameterChange> changes;
    QCOMPARE(controller.requestParameterChanges(connection, "watch", [&](DeviceParameterChange change) {
        std::lock_guard<std::mutex> lock(mutex);
        changes.push_back(std::move(change));
    }).error, DeviceParameterError::None);
    QVERIFY(QTest::qWaitFor([&] {
        std::lock_guard<std::mutex> lock(mutex);
        return !changes.empty();
    }, 3000));
    {
        std::lock_guard<std::mutex> lock(mutex);
        changes.clear();
    }

    // An external GoPxL change is read by an inventory observation that does not itself deliver.
    // The changed leaf must still reach the subscription, not be consumed by the cache update.
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        auto parameters = fake.scannerData["parameters"].toObject();
        auto settings = parameters["scanModeSettings"].toObject();
        settings["scanMode"] = 3;
        parameters["scanModeSettings"] = settings;
        fake.scannerData["parameters"] = parameters;
    }
    auto after = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    QCOMPARE(after.result.error, DeviceParameterError::None);

    QVERIFY(QTest::qWaitFor([&] {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& change : changes)
            for (const auto& descriptor : change.descriptors)
                if (descriptor.target.name == "/parameters/scanModeSettings/scanMode"
                    && descriptor.value && std::get<std::string>(*descriptor.value) == "Surface")
                    return true;
        return false;
    }, 3000));
    controller.releaseParameterChanges(connection, "watch", [](auto) {});
}

void GocatorParameterControlTests::subscriptionReobservesWritability()
{
    ResourceFake fake;
    GocatorSourceController controller(nullptr);
    bindResourceFake(controller, fake);
    auto inventory = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    const auto connection = inventory.parameters.front().target.connectionId;
    const auto revision = find(inventory.parameters, "Scanner", "/parameters/name")->revision;
    QVERIFY(!find(inventory.parameters, "Scanner", "/parameters/name")->writable);

    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        auto properties = fake.scannerSchema["properties"].toObject();
        auto parameters = properties["parameters"].toObject();
        auto fields = parameters["properties"].toObject();
        fields.insert("name", QJsonObject{{"type", "string"}, {"title", "Name"}});
        parameters["properties"] = fields;
        properties["parameters"] = parameters;
        fake.scannerSchema["properties"] = properties;
    }

    std::mutex mutex;
    std::vector<DeviceParameterChange> changes;
    QCOMPARE(controller.requestParameterChanges(connection, "watch", [&](DeviceParameterChange change) {
        std::lock_guard<std::mutex> lock(mutex);
        changes.push_back(std::move(change));
    }).error, DeviceParameterError::None);
    QVERIFY(QTest::qWaitFor([&] {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& change : changes)
            for (const auto& descriptor : change.descriptors)
                if (descriptor.target.name == "/parameters/name"
                    && descriptor.writable && descriptor.revision > revision)
                    return true;
        return false;
    }, 3000));
    controller.releaseParameterChanges(connection, "watch", [](auto) {});
}

void GocatorParameterControlTests::resourceReadFailureIsReported()
{
    ResourceFake fake;
    GocatorSourceController controller(nullptr);
    bindResourceFake(controller, fake);
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        fake.failReads = true;
    }
    auto inventory = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    QCOMPARE(inventory.result.error, DeviceParameterError::BackendFailure);
    QVERIFY(QString::fromStdString(inventory.result.message).contains(
        QStringLiteral("fake resource read failure")));
}

void GocatorParameterControlTests::removedLeafRevisionStaysMonotonic()
{
    ResourceFake fake;
    GocatorSourceController controller(nullptr);
    bindResourceFake(controller, fake);
    auto before = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    const auto* original = find(before.parameters, "Scanner", "/parameters/name");
    QVERIFY(original);
    const auto revision = original->revision;

    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        auto properties = fake.scannerSchema["properties"].toObject();
        auto parameters = properties["parameters"].toObject();
        auto fields = parameters["properties"].toObject();
        fields.remove("name");
        parameters["properties"] = fields;
        properties["parameters"] = parameters;
        fake.scannerSchema["properties"] = properties;
    }
    auto removed = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    QVERIFY(!find(removed.parameters, "Scanner", "/parameters/name"));

    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        auto properties = fake.scannerSchema["properties"].toObject();
        auto parameters = properties["parameters"].toObject();
        auto fields = parameters["properties"].toObject();
        fields.insert("name", QJsonObject{{"type", "string"}, {"title", "Name"}, {"readOnly", true}});
        parameters["properties"] = fields;
        properties["parameters"] = parameters;
        fake.scannerSchema["properties"] = properties;
    }
    auto restored = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    const auto* again = find(restored.parameters, "Scanner", "/parameters/name");
    QVERIFY(again);
    QVERIFY(again->revision > revision);
}

void GocatorParameterControlTests::changeQueueOverflowBecomesResyncSnapshot()
{
    DeviceParameterDescriptor descriptor;
    descriptor.target = {"connection", "Scanner", "/parameters/value", {}};
    descriptor.revision = 1;
    gocator::ParameterNotificationHub hub([&](const std::string& connection,
        const std::string& subscription) {
        DeviceParameterChange snapshot;
        snapshot.connectionId = connection;
        snapshot.subscriptionId = subscription;
        snapshot.descriptors = {descriptor};
        snapshot.revision = descriptor.revision;
        return snapshot;
    });
    std::mutex mutex;
    std::vector<DeviceParameterChange> changes;
    QCOMPARE(hub.add("connection", "watch", [&](DeviceParameterChange change) {
        std::lock_guard<std::mutex> lock(mutex);
        changes.push_back(std::move(change));
    }).error, DeviceParameterError::None);
    hub.publishSnapshot("connection");
    hub.drain();
    QCOMPARE(changes.size(), std::size_t{1});
    QCOMPARE(changes.front().sequence, std::uint64_t{1});

    for (int index = 0; index < 20; ++index) {
        descriptor.revision = 2 + index;
        hub.publishDelta("connection", {descriptor}, {});
    }
    hub.drain();
    bool resync = false;
    std::uint64_t previous = changes.front().sequence;
    for (std::size_t index = 1; index < changes.size(); ++index) {
        const auto& change = changes[index];
        QVERIFY(change.sequence > previous);
        previous = change.sequence;
        if (change.resync) {
            resync = true;
            QCOMPARE(change.kind, DeviceParameterChangeKind::Snapshot);
            QVERIFY(!change.overflowReason.empty());
        }
    }
    QVERIFY(resync);
    QVERIFY(hub.pending("watch") == 0);
}

void GocatorParameterControlTests::deliveryKeepsSequenceOrderAcrossThreads()
{
    DeviceParameterDescriptor descriptor;
    descriptor.target = {"connection", "Scanner", "/parameters/value", {}};
    descriptor.revision = 1;
    gocator::ParameterNotificationHub hub([&](const std::string& connection,
        const std::string& subscription) {
        DeviceParameterChange snapshot;
        snapshot.connectionId = connection;
        snapshot.subscriptionId = subscription;
        snapshot.descriptors = {descriptor};
        snapshot.revision = descriptor.revision;
        return snapshot;
    });
    std::mutex mutex;
    std::vector<std::uint64_t> delivered;
    auto entered = std::make_shared<std::promise<void>>();
    auto enteredFuture = entered->get_future();
    auto secondPopped = std::make_shared<std::promise<void>>();
    auto secondPoppedFuture = secondPopped->get_future();
    auto release = std::make_shared<std::promise<void>>();
    auto released = release->get_future().share();
    std::atomic<int> pops{0};
    QCOMPARE(hub.add("connection", "watch", [&](DeviceParameterChange change) {
        std::lock_guard<std::mutex> lock(mutex);
        delivered.push_back(change.sequence);
    }).error, DeviceParameterError::None);
    hub.publishSnapshot("connection");
    hub.drain();
    // Pause the first drain after its pop and before its delivery. While it is paused, a second
    // drain must not pop the next sequence: the old implementation released the hub lock before
    // this point and got it wrong.
    hub.setAfterPopHookForTest([&] {
        if (pops.fetch_add(1) == 0) {
            entered->set_value();
            released.wait();
        } else {
            secondPopped->set_value();
        }
    });
    descriptor.revision = 2;
    hub.publishDelta("connection", {descriptor}, {});
    descriptor.revision = 3;
    hub.publishDelta("connection", {descriptor}, {});

    auto first = std::async(std::launch::async, [&] { hub.drain(); });
    QCOMPARE(enteredFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto second = std::async(std::launch::async, [&] { hub.drain(); });
    // The second drain must stay blocked before its pop while the first owns the watch lock.
    QCOMPARE(secondPoppedFuture.wait_for(std::chrono::milliseconds(400)), std::future_status::timeout);
    release->set_value();
    first.wait();
    second.wait();
    std::lock_guard<std::mutex> lock(mutex);
    QCOMPARE(delivered, (std::vector<std::uint64_t>{1, 2, 3}));
}

void GocatorParameterControlTests::releaseStopsDelivery()
{
    ResourceFake fake;
    GocatorSourceController controller(nullptr);
    bindResourceFake(controller, fake);
    auto inventory = awaitResult<DeviceParameterInventory>(controller, [&](auto done) {
        return controller.requestParameters(done);
    });
    controller._parameterWork.waitForFinished();
    const auto connection = inventory.parameters.front().target.connectionId;
    std::atomic<int> deliveries{0};
    QCOMPARE(controller.requestParameterChanges(connection, "watch", [&](DeviceParameterChange) {
        deliveries.fetch_add(1);
    }).error, DeviceParameterError::None);
    QVERIFY(QTest::qWaitFor([&] { return deliveries.load() > 0; }, 3000));
    bool released = false;
    QCOMPARE(controller.releaseParameterChanges(connection, "watch", [&](auto) { released = true; }).error,
        DeviceParameterError::None);
    QVERIFY(released);
    const int afterRelease = deliveries.load();
    controller.wakeObservationThread();
    QVERIFY(QTest::qWaitFor([&] {
        std::lock_guard<std::mutex> lock(controller._observationThreadMutex);
        return !controller._observationRunning;
    }, 3000));
    QCOMPARE(deliveries.load(), afterRelease);
    QCOMPARE(controller.releaseParameterChanges(connection, "watch", [](auto) {}).error,
        DeviceParameterError::NotFound);

    // The observation loop may have returned while its thread object is still joinable. A second
    // subscribe must join that finished thread instead of move-assigning onto it.
    QCOMPARE(controller.requestParameterChanges(connection, "watch-2", [&](DeviceParameterChange) {
        deliveries.fetch_add(1);
    }).error, DeviceParameterError::None);
    QVERIFY(QTest::qWaitFor([&] { return deliveries.load() > afterRelease; }, 3000));
    QCOMPARE(controller.releaseParameterChanges(connection, "watch-2", [](auto) {}).error,
        DeviceParameterError::None);
}

void GocatorParameterControlTests::mapsAndSamplesRangeRejections()
{
    using Access = GocatorParameterTestAccess;
    using Reason = GocatorRangeRejection::Reason;
    static std::vector<DeviceRangeRejectionObservation> observations;
    observations.clear();
    GocatorSourceController controller(nullptr, nullptr,
        +[](const DeviceRangeRejectionObservation& observation) noexcept {
            try { observations.push_back(observation); } catch (...) {}
        });
    QVERIFY(Access::beginRangeRun(controller, false));
    GocatorRangeRejection evidence;
    evidence.reason = Reason::StorageRefused;
    evidence.requestedStorageBytes = 127U;
    for (std::size_t i = 1; i <= 101; ++i) Access::rejectRange(controller, evidence, i);
    QCOMPARE(observations.size(), std::size_t{2});
    QCOMPARE(observations[0].frameSequence, std::uint64_t{1});
    QCOMPARE(observations[1].frameSequence, std::uint64_t{100});
    QCOMPARE(observations.front().requestedStorageBytes, std::uint64_t{127});

    QVERIFY(Access::beginRangeRun(controller, false));
    Access::rejectRange(controller, evidence, 102U);
    QCOMPARE(observations.size(), std::size_t{3});
    QVERIFY(Access::beginRangeRun(controller, true));
    Access::rejectRange(controller, evidence, 103U);
    Access::rejectRange(controller, evidence, 104U);
    QCOMPARE(observations.size(), std::size_t{5});

    const std::pair<Reason, DeviceRangeRejection> mappings[] = {
        {Reason::MissingRange, DeviceRangeRejection::MissingRange},
        {Reason::MissingData, DeviceRangeRejection::MissingData},
        {Reason::InvalidDimensions, DeviceRangeRejection::InvalidDimensions},
        {Reason::UnsupportedFormat, DeviceRangeRejection::UnsupportedFormat},
        {Reason::TruncatedData, DeviceRangeRejection::TruncatedData},
        {Reason::InvalidLayout, DeviceRangeRejection::InvalidLayout},
        {Reason::SizeLimitExceeded, DeviceRangeRejection::SizeLimitExceeded},
        {Reason::StorageRefused, DeviceRangeRejection::StorageRefused},
        {Reason::StorageMisaligned, DeviceRangeRejection::StorageMisaligned},
    };
    for (const auto& [input, expected] : mappings) {
        evidence.reason = input;
        const auto before = observations.size();
        Access::rejectRange(controller, evidence, 105U);
        QCOMPARE(observations.size(), before + 1U);
        QCOMPARE(observations.back().reason, expected);
    }
}

QTEST_MAIN(GocatorParameterControlTests)
#include "GocatorParameterControlTests.moc"
