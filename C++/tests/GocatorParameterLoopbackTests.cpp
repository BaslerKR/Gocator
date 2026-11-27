#include <QtTest/QtTest>

#include "DeviceRuntime/Protocol/DeviceParameterLoopback.h"
#include "DeviceRuntime/Protocol/DeviceRuntimeProtocol.h"
#include "Utility/PlaygroundAdapter/Source/GocatorSourceController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <future>
#include <memory>

/**
 * @brief Runs the Core parameter transport against the Gocator controller.
 *
 * Every request, admission, completion and notification crosses the Playground JSON codec.
 * The fixture is synthetic: no sensor is discovered or opened, and the controller seams copy
 * GoPxL-shaped schema/data objects.
 */
class GocatorParameterLoopbackTests : public QObject {
    Q_OBJECT
private slots:
    void codecCarriesTreeSnapshotDeltaAndResync();
};

namespace {

struct Collected {
    std::mutex mutex;
    std::vector<QJsonObject> completions;
    std::vector<DeviceParameterChange> changes;
};

bool waitFor(const std::function<bool()>& predicate)
{
    return QTest::qWaitFor(predicate, 5000);
}

DeviceParameterResult admissionOf(const QJsonObject& response)
{
    const auto result = response.value(QStringLiteral("result")).toObject()
        .value(QStringLiteral("admission")).toObject();
    DeviceParameterResult admission;
    QString error;
    if (!DeviceRuntimeProtocol::decodeResult(result, &admission, &error))
        throw std::runtime_error("Malformed loopback admission");
    return admission;
}

DeviceConnectionResult awaitConnection(const std::function<DeviceConnectionResult(
    IDeviceConnectionControl::Completion)>& request)
{
    auto promise = std::make_shared<std::promise<DeviceConnectionResult>>();
    auto future = promise->get_future();
    const auto admission = request([promise](DeviceConnectionResult result) {
        promise->set_value(std::move(result));
    });
    if (admission.error != DeviceConnectionError::None) return admission;
    if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        throw std::runtime_error("Missing connection completion");
    return future.get();
}

struct Resources {
    Resources()
    {
        const QJsonObject scanMode{{"type", "integer"}, {"title", "Scan mode"},
            {"enum", QJsonArray{2, 3}}, {"enumText", QJsonArray{"Profile", "Surface"}}};
        const QJsonObject settings{{"type", "object"}, {"title", "Scan mode settings"},
            {"properties", QJsonObject{{"scanMode", scanMode}}}};
        const QJsonObject parameters{{"type", "object"},
            {"properties", QJsonObject{{"scanModeSettings", settings}}}};
        scannerSchema = QJsonObject{{"type", "object"},
            {"properties", QJsonObject{{"parameters", parameters}}}};
        scannerData = QJsonObject{{"parameters", QJsonObject{
            {"scanModeSettings", QJsonObject{{"scanMode", 2}}}}}};
    }

    std::mutex mutex;
    QJsonObject scannerSchema;
    QJsonObject scannerData;
    std::vector<std::string> writes;
    Gocator::ConnectionSnapshot snapshot{true, 4, "10.0.0.9", "10.0.0.9"};
    bool grabbing = false;
    std::size_t grabs = 0;
    std::size_t stops = 0;

    GocatorSourceController::ResourceSnapshot read(Gocator::ParameterTarget target)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (target != Gocator::ParameterTarget::Scanner) return {"{}", "{}"};
        return {QJsonDocument(scannerSchema).toJson(QJsonDocument::Compact).toStdString(),
            QJsonDocument(scannerData).toJson(QJsonDocument::Compact).toStdString()};
    }
};

} // namespace

void GocatorParameterLoopbackTests::codecCarriesTreeSnapshotDeltaAndResync()
{
    Resources resources;
    GocatorSourceController controller(nullptr);
    controller._systemConnected = [] { return true; };
    controller._readSystem = [] {
        return Gocator::SystemState{9, Gocator::SystemState::Running::Ready, false};
    };
    controller._readResource = [&resources](Gocator::ParameterTarget target) {
        return resources.read(target);
    };
    controller._writeParameter = [&resources](Gocator::ParameterTarget, const std::string& path,
        const std::string& value) {
        std::lock_guard<std::mutex> lock(resources.mutex);
        resources.writes.push_back(path + "=" + value);
        auto parameters = resources.scannerData["parameters"].toObject();
        auto settings = parameters["scanModeSettings"].toObject();
        settings["scanMode"] = std::stoi(value);
        parameters["scanModeSettings"] = settings;
        resources.scannerData["parameters"] = parameters;
        return Gocator::ParameterWriteResult{true, false, {}};
    };
    controller._lifecycleAvailable = true;
    controller._lifecycle.snapshot = [&resources] { return resources.snapshot; };
    controller._lifecycle.beginControl = [] { return true; };
    controller._lifecycle.endControl = [] {};
    controller._lifecycle.diagnostics = [&resources] {
        return GocatorRuntimeDiagnostics{resources.grabbing, 0, 0};
    };
    controller._lifecycle.grab = [&resources](std::size_t frames) {
        if (resources.grabbing || !resources.snapshot.opened) return false;
        ++resources.grabs;
        resources.grabbing = true;
        return true;
    };
    controller._lifecycle.stop = [&resources] {
        ++resources.stops;
        resources.grabbing = false;
    };
    controller._observationInterval = std::chrono::seconds(60);

    Collected collected;
    DeviceParameterLoopback loopback(QStringLiteral("gocator-loopback"), controller,
        [&collected](const QString& name, const QJsonObject& data) {
            if (name == QLatin1String("completion")) {
                std::lock_guard<std::mutex> lock(collected.mutex);
                collected.completions.push_back(data);
            } else if (name == QLatin1String("parameters.changed")) {
                DeviceParameterChange change;
                QString error;
                if (!DeviceRuntimeProtocol::decodeChange(
                        data.value(QStringLiteral("change")).toObject(), &change, &error))
                    return;
                std::lock_guard<std::mutex> lock(collected.mutex);
                collected.changes.push_back(std::move(change));
            }
        });

    const auto request = [&](const QString& id, const QString& op, const QJsonObject& args) {
        std::optional<DeviceParameterResult> admission;
        if (!loopback.request(id, op, args, [&admission](const QJsonObject& response) {
                admission = admissionOf(response);
            }))
            throw std::runtime_error("The loopback transport refused the request.");
        if (!admission) throw std::runtime_error("The loopback transport completed no admission.");
        return *admission;
    };

    // parameters.inventory: every tree field crosses encode/decode unchanged.
    QCOMPARE(request(QStringLiteral("inventory"), QStringLiteral("parameters.inventory"), {}).error,
        DeviceParameterError::None);
    QVERIFY(waitFor([&] {
        std::lock_guard<std::mutex> lock(collected.mutex);
        for (const auto& completion : collected.completions)
            if (completion.value(QStringLiteral("op")).toString()
                == QStringLiteral("parameters.inventory")) return true;
        return false;
    }));
    DeviceParameterInventory inventory;
    {
        std::lock_guard<std::mutex> lock(collected.mutex);
        QString error;
        for (const auto& completion : collected.completions) {
            if (completion.value(QStringLiteral("op")).toString()
                != QStringLiteral("parameters.inventory")) continue;
            QVERIFY(DeviceRuntimeProtocol::decodeInventory(
                completion.value(QStringLiteral("inventory")).toObject(), &inventory, &error));
        }
    }
    QCOMPARE(inventory.result.error, DeviceParameterError::None);
    QCOMPARE(inventory.parameters.size(), std::size_t{3});
    const auto& scanMode = inventory.parameters[2];
    QCOMPARE(scanMode.target.scope, std::string("Scanner"));
    QVERIFY(scanMode.categoryPath == std::vector<std::string>({"Scan mode settings"}));
    QCOMPARE(scanMode.representation, DeviceParameterRepresentation::Choice);
    QVERIFY(scanMode.writable);
    const auto connection = inventory.parameters.front().target.connectionId;

    // parameters.watch: the initial snapshot must arrive through parameters.changed.
    QCOMPARE(request(QStringLiteral("watch"), QStringLiteral("parameters.watch"),
        QJsonObject{{QStringLiteral("connectionId"), QString::fromStdString(connection)},
                    {QStringLiteral("subscriptionId"), QStringLiteral("loopback-watch")}}).error,
        DeviceParameterError::None);
    QVERIFY(waitFor([&] {
        std::lock_guard<std::mutex> lock(collected.mutex);
        return !collected.changes.empty();
    }));
    {
        std::lock_guard<std::mutex> lock(collected.mutex);
        QCOMPARE(collected.changes.front().kind, DeviceParameterChangeKind::Snapshot);
        QCOMPARE(collected.changes.front().sequence, std::uint64_t{1});
        QCOMPARE(collected.changes.front().descriptors.size(), std::size_t{3});
        QVERIFY(collected.changes.front().descriptors[2].categoryPath
            == std::vector<std::string>({"Scan mode settings"}));
        collected.changes.clear();
    }

    // parameters.write: value, effect and readback cross the codec; the watcher gets a delta.
    QCOMPARE(request(QStringLiteral("write"), QStringLiteral("parameters.write"),
        QJsonObject{{QStringLiteral("target"), DeviceRuntimeProtocol::encodeTarget(scanMode.target)},
                    {QStringLiteral("value"), DeviceRuntimeProtocol::encodeValue(std::string("Surface"))},
                    {QStringLiteral("expectedRevision"), QString::number(scanMode.revision)}}).error,
        DeviceParameterError::None);
    QVERIFY(waitFor([&] {
        std::lock_guard<std::mutex> lock(collected.mutex);
        for (const auto& completion : collected.completions)
            if (completion.value(QStringLiteral("op")).toString() == QStringLiteral("parameters.write"))
                return true;
        return false;
    }));
    DeviceParameterResult written;
    {
        std::lock_guard<std::mutex> lock(collected.mutex);
        QString error;
        for (const auto& completion : collected.completions) {
            if (completion.value(QStringLiteral("op")).toString() != QStringLiteral("parameters.write"))
                continue;
            QVERIFY(DeviceRuntimeProtocol::decodeResult(
                completion.value(QStringLiteral("result")).toObject(), &written, &error));
        }
    }
    QCOMPARE(written.error, DeviceParameterError::None);
    QCOMPARE(written.effect, DeviceParameterEffect::Applied);
    QVERIFY(written.descriptor);
    QCOMPARE(std::get<std::string>(*written.descriptor->value), std::string("Surface"));
    QCOMPARE(resources.writes.size(), std::size_t{1});
    QCOMPARE(resources.writes.front(), std::string("/parameters/scanModeSettings/scanMode=3"));
    QVERIFY(waitFor([&] {
        std::lock_guard<std::mutex> lock(collected.mutex);
        return !collected.changes.empty();
    }));
    {
        std::lock_guard<std::mutex> lock(collected.mutex);
        QCOMPARE(collected.changes.front().kind, DeviceParameterChangeKind::Delta);
        QCOMPARE(collected.changes.front().sequence, std::uint64_t{2});
        QCOMPARE(collected.changes.front().descriptors.front().target.name,
            std::string("/parameters/scanModeSettings/scanMode"));
        collected.changes.clear();
    }

    // A queue overflow is delivered as one resync snapshot, never a silent sequence gap.
    auto descriptor = *written.descriptor;
    for (int index = 0; index < 20; ++index) {
        descriptor.revision = 10 + index;
        controller._notifications.publishDelta(connection, {descriptor}, {});
    }
    controller._notifications.drain();
    QVERIFY(waitFor([&] {
        std::lock_guard<std::mutex> lock(collected.mutex);
        for (const auto& change : collected.changes)
            if (change.resync) return true;
        return false;
    }));
    {
        std::lock_guard<std::mutex> lock(collected.mutex);
        std::uint64_t previous = 2;
        bool resync = false;
        for (const auto& change : collected.changes) {
            QVERIFY(change.sequence > previous);
            previous = change.sequence;
            if (!change.resync) continue;
            resync = true;
            QCOMPARE(change.kind, DeviceParameterChangeKind::Snapshot);
            QVERIFY(!change.overflowReason.empty());
        }
        QVERIFY(resync);
        collected.changes.clear();
    }

    // parameters.unwatch: delivery stops through the same transport.
    QCOMPARE(request(QStringLiteral("unwatch"), QStringLiteral("parameters.unwatch"),
        QJsonObject{{QStringLiteral("connectionId"), QString::fromStdString(connection)},
                    {QStringLiteral("subscriptionId"), QStringLiteral("loopback-watch")}}).error,
        DeviceParameterError::None);
    QVERIFY(waitFor([&] {
        std::lock_guard<std::mutex> lock(collected.mutex);
        for (const auto& completion : collected.completions)
            if (completion.value(QStringLiteral("op")).toString() == QStringLiteral("parameters.unwatch"))
                return true;
        return false;
    }));
    controller._notifications.publishDelta(connection, {descriptor}, {});
    controller._notifications.drain();
    QTest::qWait(100);
    {
        std::lock_guard<std::mutex> lock(collected.mutex);
        QVERIFY(collected.changes.empty());
    }

    // The same controller owns the standard acquisition lifecycle. Live does not disturb the
    // loopback parameter authority, and Single while Live is refused without a second effect.
    controller.setFrameContextSource(std::make_shared<GraphicsLocalFrameContextSource>());
    const auto connectionId = controller.currentConnectionId(4);
    QVERIFY(controller.connectionCapabilities().single.available);
    QVERIFY(controller.connectionCapabilities().live.available);
    const auto live = awaitConnection([&](auto done) {
        return controller.requestLiveStart(connectionId, done);
    });
    QCOMPARE(live.error, DeviceConnectionError::None);
    QVERIFY(resources.grabbing);
    QCOMPARE(resources.grabs, std::size_t{1});
    const auto refusedSingle = controller.requestSingle(connectionId, [](auto) {});
    QCOMPARE(refusedSingle.error, DeviceConnectionError::Busy);
    QCOMPARE(refusedSingle.message, std::string("Stop Live before a single grab."));
    QCOMPARE(resources.grabs, std::size_t{1});
    const auto stopped = awaitConnection([&](auto done) {
        return controller.requestLiveStop(connectionId, done);
    });
    QCOMPARE(stopped.error, DeviceConnectionError::None);
    QVERIFY(!resources.grabbing);
    QCOMPARE(resources.stops, std::size_t{1});

    loopback.close();
}

QTEST_MAIN(GocatorParameterLoopbackTests)
#include "GocatorParameterLoopbackTests.moc"
