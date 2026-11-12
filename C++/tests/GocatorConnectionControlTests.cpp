#include <QtTest/QtTest>

#include <QComboBox>
#include <QToolButton>
#include <atomic>
#include <future>
#include <stdexcept>
#include <string>
#include <vector>

#include "Utility/PlaygroundAdapter/Source/GocatorSourceController.h"
#include "Utility/Qt/QGocatorWidget.h"

namespace {

struct FakeGocator {
    std::vector<Gocator::DeviceInfo> devices{
        {"192.168.1.10", "Gocator 2500", "12345", false},
        {"192.168.1.11", "Gocator 2600", "0", true},
    };
    Gocator::ConnectionSnapshot snapshot;
    GocatorRuntimeDiagnostics diagnostics;
    bool gate = false;
    std::uint64_t generation = 0;
    int opens = 0;
    int closes = 0;
    std::function<void()> beforeDiscover;
};

} // namespace

class GocatorConnectionControlTests : public QObject {
    Q_OBJECT
private slots:
    void lifecycleAdmissionAndStaleGeneration();
    void busyGateAndTeardownDrain();
    void guiAndApiShareControlGate();

private:
    static void bind(GocatorSourceController& controller, FakeGocator& fake);
};

void GocatorConnectionControlTests::bind(GocatorSourceController& controller, FakeGocator& fake)
{
    controller._lifecycleAvailable = true;
    controller._lifecycle.cachedDevices = [&fake] { return fake.devices; };
    controller._lifecycle.snapshot = [&fake] { return fake.snapshot; };
    controller._lifecycle.diagnostics = [&fake] { return fake.diagnostics; };
    controller._lifecycle.discover = [&fake](std::string* error) {
        if (fake.beforeDiscover) fake.beforeDiscover();
        fake.devices = {
            {"192.168.1.10", "Gocator 2500", "12345", false},
            {"192.168.1.11", "Gocator 2600", "0", true},
        };
        if (error) error->clear();
        return fake.devices;
    };
    controller._lifecycle.open = [&fake](const std::string& targetId) {
        ++fake.opens;
        for (const auto& device : fake.devices) {
            if (device.address != targetId) continue;
            fake.snapshot = {true, ++fake.generation, targetId};
            return true;
        }
        return false;
    };
    controller._lifecycle.close = [&fake] {
        ++fake.closes;
        fake.snapshot = {};
    };
    controller._lifecycle.beginControl = [&fake] {
        if (fake.gate) return false;
        fake.gate = true;
        return true;
    };
    controller._lifecycle.endControl = [&fake] { fake.gate = false; };
}

namespace {

DeviceConnectionResult awaitState(GocatorSourceController& controller,
    const std::function<DeviceConnectionResult(IDeviceConnectionControl::Completion)>& request)
{
    Q_UNUSED(controller);
    auto promise = std::make_shared<std::promise<DeviceConnectionResult>>();
    auto future = promise->get_future();
    const auto admission = request([promise](DeviceConnectionResult result) {
        promise->set_value(std::move(result));
    });
    if (admission.error != DeviceConnectionError::None)
        throw std::runtime_error("Unexpected lifecycle admission refusal");
    if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        throw std::runtime_error("Missing lifecycle completion");
    return future.get();
}

DeviceConnectionTargets awaitTargets(GocatorSourceController& controller,
    const std::function<DeviceConnectionResult(IDeviceConnectionControl::TargetsCompletion)>& request)
{
    Q_UNUSED(controller);
    auto promise = std::make_shared<std::promise<DeviceConnectionTargets>>();
    auto future = promise->get_future();
    const auto admission = request([promise](DeviceConnectionTargets result) {
        promise->set_value(std::move(result));
    });
    if (admission.error != DeviceConnectionError::None)
        throw std::runtime_error("Unexpected lifecycle admission refusal");
    if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        throw std::runtime_error("Missing target completion");
    return future.get();
}

} // namespace

void GocatorConnectionControlTests::lifecycleAdmissionAndStaleGeneration()
{
    FakeGocator fake;
    GocatorSourceController controller(nullptr);
    bind(controller, fake);
    QVERIFY(qobject_cast<IDeviceConnectionControl*>(&controller));

    const auto capabilities = controller.connectionCapabilities();
    QVERIFY(capabilities.targets.available);
    QVERIFY(capabilities.discover.available);
    QVERIFY(capabilities.connect.available);
    QVERIFY(capabilities.disconnect.available);
    QVERIFY(!capabilities.single.available);
    QVERIFY(!capabilities.live.available);
    QVERIFY(capabilities.single.reason.find("source.start") != std::string::npos);

    const auto targets = awaitTargets(controller, [&](auto done) {
        return controller.requestConnectionTargets(done);
    });
    QCOMPARE(targets.result.error, DeviceConnectionError::None);
    QCOMPARE(targets.targets.size(), std::size_t{2});
    QCOMPARE(targets.targets[0].targetId, std::string("192.168.1.10"));
    QCOMPARE(targets.targets[0].displayName, std::string("G2500 (12345) - 192.168.1.10"));
    QCOMPARE(targets.targets[0].attributes.size(), std::size_t{3});
    QCOMPARE(targets.targets[1].attributes[2].value, std::string("true"));

    fake.diagnostics = {false, 7, 0};
    const auto disconnected = awaitState(controller, [&](auto done) {
        return controller.requestConnectionState(done);
    });
    QVERIFY(disconnected.state);
    QVERIFY(!disconnected.state->connected);
    QVERIFY(disconnected.state->deliveredFrames
        && *disconnected.state->deliveredFrames == std::uint64_t{7});

    int completions = 0;
    QCOMPARE(controller.requestConnect("192.168.1.99", [&](auto) { ++completions; }).error,
        DeviceConnectionError::NotFound);
    QCOMPARE(controller.requestConnect("", [&](auto) { ++completions; }).error,
        DeviceConnectionError::InvalidArgument);
    QCOMPARE(completions, 0);

    const auto connected = awaitState(controller, [&](auto done) {
        return controller.requestConnect("192.168.1.10", done);
    });
    QCOMPARE(connected.error, DeviceConnectionError::None);
    QCOMPARE(connected.effect, DeviceConnectionEffect::Applied);
    QVERIFY(connected.state);
    QVERIFY(connected.state->connected);
    QCOMPARE(connected.state->targetId, std::string("192.168.1.10"));
    QCOMPARE(connected.state->displayName, std::string("G2500 (12345) - 192.168.1.10"));
    QVERIFY(!connected.state->connectionId.empty());
    const std::string firstConnection = connected.state->connectionId;
    QCOMPARE(fake.opens, 1);

    QCOMPARE(controller.requestConnect("192.168.1.11", [&](auto) { ++completions; }).error,
        DeviceConnectionError::Conflict);
    QCOMPARE(controller.requestDisconnect(firstConnection + "-stale", [&](auto) { ++completions; }).error,
        DeviceConnectionError::Conflict);
    QCOMPARE(controller.requestDiscover([&](auto) { ++completions; }).error,
        DeviceConnectionError::Busy);
    QCOMPARE(completions, 0);

    fake.diagnostics.grabbing = true;
    QCOMPARE(controller.requestDisconnect(firstConnection, [&](auto) { ++completions; }).error,
        DeviceConnectionError::Busy);
    QCOMPARE(completions, 0);
    fake.diagnostics.grabbing = false;

    const auto disconnectedAgain = awaitState(controller, [&](auto done) {
        return controller.requestDisconnect(firstConnection, done);
    });
    QCOMPARE(disconnectedAgain.error, DeviceConnectionError::None);
    QCOMPARE(disconnectedAgain.effect, DeviceConnectionEffect::Applied);
    QVERIFY(disconnectedAgain.state && !disconnectedAgain.state->connected);
    QCOMPARE(fake.closes, 1);

    const auto rediscovered = awaitTargets(controller, [&](auto done) {
        return controller.requestDiscover(done);
    });
    QCOMPARE(rediscovered.result.error, DeviceConnectionError::None);
    QCOMPARE(rediscovered.targets.size(), std::size_t{2});

    QCOMPARE(controller.requestSingle(firstConnection, [&](auto) { ++completions; }).error,
        DeviceConnectionError::Unsupported);
    QCOMPARE(controller.requestLiveStart(firstConnection, [&](auto) { ++completions; }).error,
        DeviceConnectionError::Unsupported);

    const auto second = awaitState(controller, [&](auto done) {
        return controller.requestConnect("192.168.1.11", done);
    });
    QVERIFY(second.state && second.state->connected);
    QVERIFY(second.state->connectionId != firstConnection);
    QCOMPARE(awaitState(controller, [&](auto done) {
        return controller.requestDisconnect(second.state->connectionId, done);
    }).error, DeviceConnectionError::None);
}

void GocatorConnectionControlTests::busyGateAndTeardownDrain()
{
    FakeGocator fake;
    GocatorSourceController controller(nullptr);
    bind(controller, fake);
    fake.gate = true;
    int completions = 0;
    QCOMPARE(controller.requestDiscover([&](auto) { ++completions; }).error,
        DeviceConnectionError::Busy);
    QCOMPARE(completions, 0);
    fake.gate = false;

    auto gate = std::make_shared<std::promise<void>>();
    auto released = gate->get_future().share();
    fake.beforeDiscover = [released] { released.wait(); };
    QCOMPARE(controller.requestDiscover([&](auto) { ++completions; }).error,
        DeviceConnectionError::None);
    QCOMPARE(controller.requestDiscover([&](auto) { ++completions; }).error,
        DeviceConnectionError::Busy);
    QCOMPARE(controller.requestConnect("192.168.1.10", [&](auto) { ++completions; }).error,
        DeviceConnectionError::Busy);
    gate->set_value();
    controller._controlWork.waitForFinished();
    QCOMPARE(completions, 1);
    QVERIFY(!fake.gate);

    auto teardownGate = std::make_shared<std::promise<void>>();
    auto teardownReleased = teardownGate->get_future().share();
    std::atomic<int> teardownCompletions{0};
    {
        GocatorSourceController pending(nullptr);
        fake.beforeDiscover = [teardownReleased] { teardownReleased.wait(); };
        bind(pending, fake);
        QCOMPARE(pending.requestDiscover([&](auto) { ++teardownCompletions; }).error,
            DeviceConnectionError::None);
        QCOMPARE(pending.requestDiscover([&](auto) { ++teardownCompletions; }).error,
            DeviceConnectionError::Busy);
        teardownGate->set_value();
        // Destruction must drain the accepted worker and call its completion once.
    }
    QCOMPARE(teardownCompletions.load(), 1);
    QVERIFY(!fake.gate);
}

void GocatorConnectionControlTests::guiAndApiShareControlGate()
{
    Gocator gocator;
    QGocatorWidget widget(nullptr, &gocator);
    GocatorSourceController controller(&gocator);

    auto* combo = widget.findChild<QComboBox*>(QStringLiteral("gocatorIpCombo"));
    auto* connect = widget.findChild<QToolButton*>(QStringLiteral("gocatorToolConnect"));
    QVERIFY(combo);
    QVERIFY(connect);

    QVERIFY(gocator.tryBeginControl());
    QTRY_VERIFY(!combo->isEnabled());
    QTRY_VERIFY(!connect->isEnabled());

    int completions = 0;
    QCOMPARE(controller.requestDiscover([&](auto) { ++completions; }).error,
        DeviceConnectionError::Busy);
    QCOMPARE(completions, 0);

    QMetaObject::invokeMethod(&widget, "onConnectToggled", Qt::DirectConnection, Q_ARG(bool, true));
    QVERIFY(!connect->isChecked());
    QVERIFY(gocator.controlBusy());

    gocator.endControl();
    QTRY_VERIFY(combo->isEnabled());
    QTRY_VERIFY(connect->isEnabled());
}

QTEST_MAIN(GocatorConnectionControlTests)
#include "GocatorConnectionControlTests.moc"
