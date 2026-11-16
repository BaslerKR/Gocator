#include <QtTest/QtTest>

#include <QComboBox>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QToolButton>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
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
    std::string refusedAddress;
    std::string refusalReason;
    std::function<void()> beforeDiscover;
    bool failDiscover = false;
    bool refuseGrab = false;
    std::size_t singleGrabs = 0;
    std::size_t liveGrabs = 0;
    std::size_t stops = 0;
};

class FeatureRefreshWidget final : public QGocatorWidget {
public:
    using QGocatorWidget::QGocatorWidget;
    using QGocatorWidget::fetchFeatureData;

    void blockReads(std::shared_future<void> gate) { _gate = std::move(gate); }

protected:
    bool featureDataAvailable() const override { return true; }
    FeatureDataResult readFeatureData(bool) const override {
        if (_gate.valid()) _gate.wait();
        FeatureDataResult result;
        result.scannerData = QStringLiteral("{}");
        result.sensorData = QStringLiteral("{}");
        result.systemData = QStringLiteral("{}");
        return result;
    }

private:
    std::shared_future<void> _gate;
};

struct GateRelease {
    std::shared_ptr<std::promise<void>> gate;
    std::atomic<bool> released{false};

    void release() {
        if (!released.exchange(true)) gate->set_value();
    }
    ~GateRelease() { release(); }
};

} // namespace

class GocatorConnectionControlTests : public QObject {
    Q_OBJECT
private slots:
    void lifecycleAdmissionAndStaleGeneration();
    void singleAndLiveShareOneAuthority();
    void singleStopRacingStartStaysExplicit();
    void connectAcceptsUndiscoveredAddress();
    void discoverPublishesCacheAndFailureStaysSilent();
    void startWithoutConnectionReportsInactive();
    void snapshotDoesNotWaitForControlLock();
    void busyGateAndTeardownDrain();
    void guiAndApiShareControlGate();
    void externalControlEndWaitsForFeatureWatcher();

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
        if (fake.failDiscover) {
            if (error) *error = "fake discovery failure";
            return fake.devices;
        }
        fake.devices = {
            {"192.168.1.10", "Gocator 2500", "12345", false},
            {"192.168.1.11", "Gocator 2600", "0", true},
        };
        if (error) error->clear();
        return fake.devices;
    };
    controller._lifecycle.open = [&fake](const std::string& targetId, std::string* error) {
        ++fake.opens;
        if (!fake.refusedAddress.empty() && targetId == fake.refusedAddress) {
            if (error) *error = fake.refusalReason.empty()
                ? "GoPxL refused the address" : fake.refusalReason;
            return false;
        }
        // Discovery is advisory: an address that was not discovered is still opened.
        fake.snapshot = {true, ++fake.generation, targetId};
        return true;
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
    controller._lifecycle.grab = [&fake](std::size_t frames) {
        if (!fake.snapshot.opened || fake.diagnostics.grabbing || fake.refuseGrab) return false;
        if (frames == 1) ++fake.singleGrabs;
        else ++fake.liveGrabs;
        fake.diagnostics.grabbing = true;
        return true;
    };
    controller._lifecycle.stop = [&fake] {
        ++fake.stops;
        fake.diagnostics.grabbing = false;
    };
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
    QVERIFY(capabilities.single.available);
    QVERIFY(capabilities.live.available);
    QVERIFY(capabilities.single.reason.empty());
    QVERIFY(capabilities.live.reason.empty());

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
    QCOMPARE(controller.requestConnect("", [&](auto) { ++completions; }).error,
        DeviceConnectionError::InvalidArgument);
    QCOMPARE(completions, 0);

    // An undiscovered address is admitted; GoPxL decides whether it can be opened.
    fake.refusedAddress = "192.168.1.99";
    fake.refusalReason = "GoPxL refused 192.168.1.99: connection timed out";
    const auto refusedAddress = awaitState(controller, [&](auto done) {
        return controller.requestConnect("192.168.1.99", done);
    });
    QCOMPARE(refusedAddress.error, DeviceConnectionError::BackendFailure);
    QCOMPARE(refusedAddress.effect, DeviceConnectionEffect::None);
    QVERIFY(QString::fromStdString(refusedAddress.message).contains(
        QStringLiteral("connection timed out")));
    QVERIFY(refusedAddress.state && !refusedAddress.state->connected);
    QCOMPARE(fake.opens, 1);

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
    QCOMPARE(fake.opens, 2);

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
        DeviceConnectionError::Disconnected);
    QCOMPARE(controller.requestLiveStart(firstConnection, [&](auto) { ++completions; }).error,
        DeviceConnectionError::Disconnected);
    QCOMPARE(controller.requestLiveStop(firstConnection, [&](auto) { ++completions; }).error,
        DeviceConnectionError::Disconnected);
    QCOMPARE(completions, 0);

    const auto second = awaitState(controller, [&](auto done) {
        return controller.requestConnect("192.168.1.11", done);
    });
    QVERIFY(second.state && second.state->connected);
    QVERIFY(second.state->connectionId != firstConnection);
    QCOMPARE(awaitState(controller, [&](auto done) {
        return controller.requestDisconnect(second.state->connectionId, done);
    }).error, DeviceConnectionError::None);
}

void GocatorConnectionControlTests::singleAndLiveShareOneAuthority()
{
    FakeGocator fake;
    GocatorSourceController controller(nullptr);
    bind(controller, fake);

    int completions = 0;
    QCOMPARE(controller.requestSingle("x", [&](auto) { ++completions; }).error,
        DeviceConnectionError::Disconnected);
    QCOMPARE(controller.requestLiveStart("x", [&](auto) { ++completions; }).error,
        DeviceConnectionError::Disconnected);
    QCOMPARE(controller.requestLiveStop("x", [&](auto) { ++completions; }).error,
        DeviceConnectionError::Disconnected);
    QCOMPARE(completions, 0);

    const auto connected = awaitState(controller, [&](auto done) {
        return controller.requestConnect("192.168.1.10", done);
    });
    QVERIFY(connected.state);
    const std::string connection = connected.state->connectionId;
    QVERIFY(controller.connectionCapabilities().single.available);
    QVERIFY(controller.connectionCapabilities().live.available);

    // source.start and source.stop use the same authority as the lifecycle operations.
    controller.start();
    QCOMPARE(fake.liveGrabs, std::size_t{1});
    QVERIFY(fake.diagnostics.grabbing);
    controller.start();
    QCOMPARE(fake.liveGrabs, std::size_t{1});

    // Single while Live is refused with the owner wording and no producer effect.
    const auto liveSingle = controller.requestSingle(connection, [&](auto) { ++completions; });
    QCOMPARE(liveSingle.error, DeviceConnectionError::Busy);
    QCOMPARE(liveSingle.message, std::string("Stop Live before a single grab."));
    QCOMPARE(fake.singleGrabs, std::size_t{0});
    QCOMPARE(completions, 0);

    QCOMPARE(controller.requestLiveStart(connection, [&](auto) { ++completions; }).error,
        DeviceConnectionError::Conflict);
    QCOMPARE(fake.liveGrabs, std::size_t{1});

    controller.stop();
    QCOMPARE(fake.stops, std::size_t{1});
    QVERIFY(!fake.diagnostics.grabbing);
    QCOMPARE(controller.requestLiveStop(connection, [&](auto) { ++completions; }).error,
        DeviceConnectionError::Conflict);
    QCOMPARE(fake.stops, std::size_t{1});

    // One single owns the acquisition until its frame is delivered and the run ends.
    auto singlePromise = std::make_shared<std::promise<DeviceConnectionResult>>();
    auto singleFuture = singlePromise->get_future();
    QCOMPARE(controller.requestSingle(connection, [singlePromise](DeviceConnectionResult result) {
        singlePromise->set_value(std::move(result));
    }).error, DeviceConnectionError::None);
    QTRY_COMPARE(fake.singleGrabs, std::size_t{1});
    QVERIFY(fake.diagnostics.grabbing);

    const auto secondSingle = controller.requestSingle(connection, [&](auto) { ++completions; });
    QCOMPARE(secondSingle.error, DeviceConnectionError::Busy);
    QVERIFY(secondSingle.message.find("already in flight") != std::string::npos);
    QCOMPARE(fake.singleGrabs, std::size_t{1});
    QCOMPARE(completions, 0);

    QCOMPARE(controller.requestLiveStart(connection, [&](auto) { ++completions; }).error,
        DeviceConnectionError::Busy);
    QCOMPARE(controller.requestLiveStop(connection, [&](auto) { ++completions; }).error,
        DeviceConnectionError::Conflict);
    QCOMPARE(controller.requestDisconnect(connection, [&](auto) { ++completions; }).error,
        DeviceConnectionError::Busy);
    controller.start();
    QCOMPARE(fake.liveGrabs, std::size_t{1});
    QCOMPARE(completions, 0);

    controller.noteAcquisitionFrame();
    fake.diagnostics.grabbing = false;
    controller.noteAcquisitionEnded();
    const auto single = singleFuture.get();
    QCOMPARE(single.error, DeviceConnectionError::None);
    QCOMPARE(single.effect, DeviceConnectionEffect::Applied);
    QCOMPARE(fake.singleGrabs, std::size_t{1});

    // An ended single without a frame is an explicit failure, not an optimistic success.
    auto emptyPromise = std::make_shared<std::promise<DeviceConnectionResult>>();
    auto emptyFuture = emptyPromise->get_future();
    QCOMPARE(controller.requestSingle(connection, [emptyPromise](DeviceConnectionResult result) {
        emptyPromise->set_value(std::move(result));
    }).error, DeviceConnectionError::None);
    QTRY_COMPARE(fake.singleGrabs, std::size_t{2});
    fake.diagnostics.grabbing = false;
    controller.noteAcquisitionEnded();
    const auto empty = emptyFuture.get();
    QCOMPARE(empty.error, DeviceConnectionError::BackendFailure);
    QVERIFY(empty.message.find("without delivering") != std::string::npos);

    // source.stop aborts a pending single with its own explicit outcome.
    auto stoppedPromise = std::make_shared<std::promise<DeviceConnectionResult>>();
    auto stoppedFuture = stoppedPromise->get_future();
    QCOMPARE(controller.requestSingle(connection, [stoppedPromise](DeviceConnectionResult result) {
        stoppedPromise->set_value(std::move(result));
    }).error, DeviceConnectionError::None);
    QTRY_COMPARE(fake.singleGrabs, std::size_t{3});
    controller.stop();
    const auto stopped = stoppedFuture.get();
    QCOMPARE(stopped.error, DeviceConnectionError::BackendFailure);
    QVERIFY(stopped.message.find("stopped before") != std::string::npos);

    // Live starts again on the same connection and stops through the same authority.
    const auto live = awaitState(controller, [&](auto done) {
        return controller.requestLiveStart(connection, done);
    });
    QCOMPARE(live.error, DeviceConnectionError::None);
    QCOMPARE(fake.liveGrabs, std::size_t{2});
    QVERIFY(live.state && live.state->live);
    const auto liveStop = awaitState(controller, [&](auto done) {
        return controller.requestLiveStop(connection, done);
    });
    QCOMPARE(liveStop.error, DeviceConnectionError::None);
    QCOMPARE(liveStop.effect, DeviceConnectionEffect::Applied);
    QVERIFY(liveStop.state && !liveStop.state->live);

    QCOMPARE(controller.requestSingle(connection + "-stale", [&](auto) { ++completions; }).error,
        DeviceConnectionError::Conflict);
    QCOMPARE(controller.requestLiveStart(connection + "-stale", [&](auto) { ++completions; }).error,
        DeviceConnectionError::Conflict);
    QCOMPARE(controller.requestLiveStop(connection + "-stale", [&](auto) { ++completions; }).error,
        DeviceConnectionError::Conflict);
    QCOMPARE(completions, 0);
}

void GocatorConnectionControlTests::singleStopRacingStartStaysExplicit()
{
    FakeGocator fake;
    GocatorSourceController controller(nullptr);
    bind(controller, fake);
    const auto connected = awaitState(controller, [&](auto done) {
        return controller.requestConnect("192.168.1.10", done);
    });
    QVERIFY(connected.state);

    // source.stop reaches the controller while the admitted single is still entering the producer.
    // The single must not wait out its timeout for a frame that stop already forbade.
    std::atomic<bool> entered{false};
    auto release = std::make_shared<std::promise<void>>();
    auto releaseFuture = release->get_future().share();
    controller._lifecycle.grab = [&fake, &entered, releaseFuture](std::size_t frames) {
        entered = true;
        releaseFuture.wait();
        if (!fake.snapshot.opened || fake.diagnostics.grabbing || fake.refuseGrab) return false;
        if (frames == 1) ++fake.singleGrabs;
        else ++fake.liveGrabs;
        fake.diagnostics.grabbing = true;
        return true;
    };

    auto promise = std::make_shared<std::promise<DeviceConnectionResult>>();
    auto future = promise->get_future();
    QCOMPARE(controller.requestSingle(connected.state->connectionId, [promise](DeviceConnectionResult result) {
        promise->set_value(std::move(result));
    }).error, DeviceConnectionError::None);
    QTRY_VERIFY(entered.load());
    controller.stop();
    release->set_value();
    const auto stopped = future.get();
    QCOMPARE(stopped.error, DeviceConnectionError::BackendFailure);
    QVERIFY(stopped.message.find("stopped before") != std::string::npos);
    QVERIFY(!fake.diagnostics.grabbing);
}

void GocatorConnectionControlTests::connectAcceptsUndiscoveredAddress()
{
    FakeGocator fake;
    fake.devices.clear();
    GocatorSourceController controller(nullptr);
    bind(controller, fake);

    QVERIFY(controller.requestConnectionTargets([](auto) {}).error == DeviceConnectionError::None);
    const auto connected = awaitState(controller, [&](auto done) {
        return controller.requestConnect("10.20.30.40", done);
    });
    QCOMPARE(connected.error, DeviceConnectionError::None);
    QCOMPARE(connected.effect, DeviceConnectionEffect::Applied);
    QVERIFY(connected.state && connected.state->connected);
    QCOMPARE(connected.state->targetId, std::string("10.20.30.40"));
    QCOMPARE(connected.state->displayName, std::string("10.20.30.40"));
    QCOMPARE(fake.opens, 1);
}

void GocatorConnectionControlTests::discoverPublishesCacheAndFailureStaysSilent()
{
    FakeGocator fake;
    fake.devices = {{"192.168.1.99", "Gocator 9900", "9900", false}};
    GocatorSourceController controller(nullptr);
    bind(controller, fake);
    QSignalSpy changed(&controller, &GocatorSourceController::inventoryChanged);

    const auto before = awaitTargets(controller, [&](auto done) {
        return controller.requestConnectionTargets(done);
    });
    QCOMPARE(before.targets.size(), std::size_t{1});
    QCOMPARE(before.targets[0].targetId, std::string("192.168.1.99"));

    const auto discovered = awaitTargets(controller, [&](auto done) {
        return controller.requestDiscover(done);
    });
    QCOMPARE(discovered.result.error, DeviceConnectionError::None);
    QCOMPARE(discovered.targets.size(), std::size_t{2});
    QTRY_COMPARE(changed.count(), 1);

    const auto refreshed = awaitTargets(controller, [&](auto done) {
        return controller.requestConnectionTargets(done);
    });
    QCOMPARE(refreshed.targets.size(), std::size_t{2});
    QCOMPARE(refreshed.targets[0].targetId, std::string("192.168.1.10"));

    fake.failDiscover = true;
    const auto failed = awaitTargets(controller, [&](auto done) {
        return controller.requestDiscover(done);
    });
    QCOMPARE(failed.result.error, DeviceConnectionError::BackendFailure);
    QCOMPARE(changed.count(), 1);
}

void GocatorConnectionControlTests::startWithoutConnectionReportsInactive()
{
    // A grab without a connection is refused inside Gocator::grab(). A worker proxy marks
    // source.start optimistically, so the controller must publish the observed inactive state.
    Gocator gocator;
    GocatorSourceController controller(&gocator);
    QSignalSpy changed(&controller, &GocatorSourceController::diagnosticAcquisitionChanged);
    controller.start();
    QCOMPARE(controller.isGrabbing(), false);
    QVERIFY(!changed.isEmpty());
    const auto arguments = changed.last();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("gocator"));
    controller.stop();
}

void GocatorConnectionControlTests::snapshotDoesNotWaitForControlLock()
{
    Gocator gocator;
    auto entered = std::make_shared<std::promise<void>>();
    auto release = std::make_shared<std::promise<void>>();
    auto enteredFuture = entered->get_future();
    auto releaseFuture = release->get_future().share();
    auto holder = std::async(std::launch::async, [&] {
        gocator.holdControlForTest([&] { entered->set_value(); }, [&] { releaseFuture.wait(); });
    });

    const bool lockHeld = enteredFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    auto snapshotFuture = std::async(std::launch::async, [&gocator] { return gocator.connectionSnapshot(); });
    const auto snapshotStatus = snapshotFuture.wait_for(std::chrono::seconds(2));
    release->set_value();
    holder.wait();
    const auto snapshot = snapshotFuture.get();

    QVERIFY(lockHeld);
    QCOMPARE(snapshotStatus, std::future_status::ready);
    QVERIFY(!snapshot.opened);
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
    auto* refresh = widget.findChild<QToolButton*>(QStringLiteral("gocatorToolRefresh"));
    auto* connect = widget.findChild<QToolButton*>(QStringLiteral("gocatorToolConnect"));
    QVERIFY(combo);
    QVERIFY(refresh);
    QVERIFY(connect);

    QVERIFY(gocator.tryBeginControl());
    QTRY_VERIFY(!combo->isEnabled());
    QTRY_VERIFY(!refresh->isEnabled());
    QTRY_VERIFY(!connect->isEnabled());

    widget.setDiscoveredDevices({{"192.168.1.10", "Gocator 2500", "12345", false}});
    QVERIFY(!refresh->isEnabled());
    QVERIFY(!connect->isEnabled());

    int completions = 0;
    QCOMPARE(controller.requestDiscover([&](auto) { ++completions; }).error,
        DeviceConnectionError::Busy);
    QCOMPARE(completions, 0);

    QMetaObject::invokeMethod(&widget, "onConnectToggled", Qt::DirectConnection, Q_ARG(bool, true));
    QVERIFY(!connect->isChecked());
    QVERIFY(gocator.controlBusy());

    gocator.endControl();
    QTRY_VERIFY(combo->isEnabled());
    QTRY_VERIFY(refresh->isEnabled());
    QTRY_VERIFY(connect->isEnabled());
}

void GocatorConnectionControlTests::externalControlEndWaitsForFeatureWatcher()
{
    Gocator gocator;
    FeatureRefreshWidget widget(nullptr, &gocator);
    auto* refresh = widget.findChild<QToolButton*>(QStringLiteral("gocatorToolRefresh"));
    auto* connect = widget.findChild<QToolButton*>(QStringLiteral("gocatorToolConnect"));
    QVERIFY(refresh);
    QVERIFY(connect);

    auto gate = std::make_shared<std::promise<void>>();
    GateRelease release{gate};
    widget.blockReads(gate->get_future().share());
    widget.fetchFeatureData(false);
    QVERIFY(!refresh->isEnabled());
    QVERIFY(!connect->isEnabled());

    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("Device control is active")));
    QVERIFY(gocator.tryBeginControl());
    QCoreApplication::processEvents();
    QVERIFY(!connect->isEnabled());

    gocator.endControl();
    QCoreApplication::processEvents();
    const bool deferred = !connect->isEnabled() && !refresh->isEnabled();

    release.release();
    QTRY_VERIFY(connect->isEnabled());
    QTRY_VERIFY(refresh->isEnabled());
    QVERIFY(deferred);
}

QTEST_MAIN(GocatorConnectionControlTests)
#include "GocatorConnectionControlTests.moc"
