#include <QtTest/QtTest>

#include <GoPxLSdk/GoDataSet.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpImage.h>
#include <kApi/Data/kArray2.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Utility/PlaygroundAdapter/GocatorDataSetGraphicsFrameAdapter.h"
#include "Utility/PlaygroundAdapter/Source/GocatorSourceController.h"
#include "DeviceRuntime/Contract/FrameContextClient.h"
#include "DeviceRuntime/Contract/SessionFrame.h"
#include "Internal/GocatorSdkRuntime.h"
#include "engine/GraphicsFrameContext.h"

class GoGdpMsgTests
{
public:
    static std::shared_ptr<GoPxLSdk::GoGdpImage> monoPixel()
    {
        gocator::GocatorSdkRuntime::ensureInitialized();
        auto image = std::make_shared<GoPxLSdk::GoGdpImage>();
        image->width = 1;
        image->height = 1;
        image->pixelSize = 1;
        image->pixelFormat = GoPxLSdk::GoGdpPixelFormat::Mono8;
        if (kArray2_Construct(image->pixels.Ref(), kTypeOf(k8u), 1, 1, kNULL) != kOK)
            throw std::runtime_error("Could not allocate the synthetic pixel.");
        kArray2_DataT(image->pixels.Get(), k8u)[0] = 42;
        return image;
    }

    /** A pixel whose format the adapter does not know: it is published as raw Mono8 bytes. */
    static std::shared_ptr<GoPxLSdk::GoGdpImage> unknownFormatPixel()
    {
        auto image = monoPixel();
        image->pixelFormat = GoPxLSdk::GoGdpPixelFormat::Null_Format;
        return image;
    }
};

namespace {

std::vector<DeviceImageRejectionObservation> imageObservations;
std::vector<DeviceRangeRejectionObservation> rangeObservations;

void collectRangeObservation(const DeviceRangeRejectionObservation& observation) noexcept
{
    rangeObservations.push_back(observation);
}

void collectImageObservation(const DeviceImageRejectionObservation& observation) noexcept
{
    imageObservations.push_back(observation);
}

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

} // namespace

class GocatorConnectionControlTests : public QObject {
    Q_OBJECT
private slots:
    void lifecycleAdmissionAndStaleGeneration();
    void singleAndLiveShareOneAuthority();
    void pendingLiveStartBlocksSourceStart();
    void singleStopRacingStartStaysExplicit();
    void singleGrabThrowReleasesStartReservation();
    void registeredCallbacksCompleteSingle();
    void registeredCallbacksCompleteSingle_data();
    void connectAcceptsUndiscoveredAddress();
    void virtualTargetsGetDistinctAddressIdentity();
    void duplicateDiscoveryCollapsesToSingleTarget();
    void duplicateEndpointMergesMetadata();
    void sameAddressDifferentPortsStayDistinct();
    void realSensorIdentitySurvivesAddressChange();
    void discoverPublishesCacheAndFailureStaysSilent();
    void startWithoutConnectionReportsInactive();
    void snapshotDoesNotWaitForControlLock();
    void busyGateAndTeardownDrain();
    void teardownStopFailureDoesNotEscape();
    void reentrantStopStatusDoesNotPublishStaleTrue();
    void closePublishesStoppedAfterDisconnect();
    void imageDiagnosticsResetPerAcquisition();
    void grabFailurePreservesEffectHint();
    void singleStopFailureIsNotSuccess();
    void singleStopFailureIsNotSuccess_data();
    void frameContextRequiredWithoutFallback();
    void frameContextTransactionAndNoReadyCreditDrop();
    void frameContextRefusalDropsOnlyThatFrame();
    void singleCompletesPromptlyAfterStorageRefusal();
    void frameAnomaliesReachTheDiagnosticSinks();
    void cancellationOrStopReleasesOpenBuilder();
    void stopAndRearmIgnoresLateCallbackFromOldRun();

private:
    static void bind(GocatorSourceController& controller, FakeGocator& fake);
    static void bindSyntheticGocator(GocatorSourceController& controller, std::atomic<bool>& grabbing);
};

void GocatorConnectionControlTests::bind(GocatorSourceController& controller, FakeGocator& fake)
{
    controller.setFrameContextSource(std::make_shared<GraphicsLocalFrameContextSource>());
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
        fake.snapshot = {true, ++fake.generation, targetId, targetId};
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
    bool startRefused = false;
    try {
        controller.start();
    } catch (const std::runtime_error& error) {
        startRefused = true;
        QCOMPARE(std::string(error.what()), std::string("A single grab is in flight."));
    }
    QVERIFY(startRefused);
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

void GocatorConnectionControlTests::pendingLiveStartBlocksSourceStart()
{
    FakeGocator fake;
    GocatorSourceController controller(nullptr);
    bind(controller, fake);
    const auto connected = awaitState(controller, [&](auto done) {
        return controller.requestConnect("192.168.1.10", done);
    });
    QVERIFY(connected.state);

    // The live-start admission owns the acquisition authority before the SDK grab runs. A
    // concurrent source.start or Single must not reach grab() and overwrite the producer.
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
    QCOMPARE(controller.requestLiveStart(connected.state->connectionId,
        [promise](DeviceConnectionResult result) { promise->set_value(std::move(result)); }).error,
        DeviceConnectionError::None);
    QTRY_VERIFY(entered.load());
    QCOMPARE(fake.liveGrabs, std::size_t{0});

    const auto single = controller.requestSingle(connected.state->connectionId, [](auto) {});
    QCOMPARE(single.error, DeviceConnectionError::Busy);
    QCOMPARE(single.message, std::string("Live acquisition is starting."));
    bool startRefused = false;
    try {
        controller.start();
    } catch (const std::runtime_error& error) {
        startRefused = true;
        QCOMPARE(std::string(error.what()), std::string("Live acquisition is already starting."));
    }
    QVERIFY(startRefused);
    QCOMPARE(fake.liveGrabs, std::size_t{0});

    release->set_value();
    const auto live = future.get();
    QCOMPARE(live.error, DeviceConnectionError::None);
    QCOMPARE(fake.liveGrabs, std::size_t{1});
    controller.stop();
}

void GocatorConnectionControlTests::singleGrabThrowReleasesStartReservation()
{
    FakeGocator fake;
    GocatorSourceController controller(nullptr);
    bind(controller, fake);
    const auto connected = awaitState(controller, [&](auto done) {
        return controller.requestConnect("192.168.1.10", done);
    });
    QVERIFY(connected.state);

    controller._lifecycle.grab = [](std::size_t) -> bool {
        throw std::runtime_error("grab boom");
    };
    auto promise = std::make_shared<std::promise<DeviceConnectionResult>>();
    auto future = promise->get_future();
    QCOMPARE(controller.requestSingle(connected.state->connectionId,
        [promise](DeviceConnectionResult result) { promise->set_value(std::move(result)); }).error,
        DeviceConnectionError::None);
    const auto failed = future.get();
    QCOMPARE(failed.error, DeviceConnectionError::BackendFailure);
    QVERIFY(failed.message.find("grab boom") != std::string::npos);

    // A thrown producer start must not leave the acquisition reservation held for the session.
    controller._lifecycle.grab = [&fake](std::size_t frames) {
        if (frames == 1) ++fake.singleGrabs;
        else ++fake.liveGrabs;
        fake.diagnostics.grabbing = true;
        return true;
    };
    const auto live = awaitState(controller, [&](auto done) {
        return controller.requestLiveStart(connected.state->connectionId, done);
    });
    QCOMPARE(live.error, DeviceConnectionError::None);
    QCOMPARE(fake.liveGrabs, std::size_t{1});
    controller.stop();
}

void GocatorConnectionControlTests::registeredCallbacksCompleteSingle_data()
{
    QTest::addColumn<bool>("validImage");
    QTest::newRow("valid image") << true;
    QTest::newRow("no usable payload") << false;
}

void GocatorConnectionControlTests::registeredCallbacksCompleteSingle()
{
    QFETCH(bool, validImage);
    Gocator gocator;
    GocatorSourceController controller(&gocator);
    controller.setFrameContextSource(std::make_shared<GraphicsLocalFrameContextSource>());
    std::size_t frames = 0;
    controller.setFrameConsumer([&frames](SessionFrame&& frame, unsigned) {
        if (frame.payload.hasImage()) ++frames;
    });
    controller._lifecycle.snapshot = [] {
        return Gocator::ConnectionSnapshot{true, 9, "10.0.0.9", "10.0.0.9"};
    };
    controller._lifecycle.beginControl = [] { return true; };
    controller._lifecycle.endControl = [] {};
    controller._lifecycle.diagnostics = [] { return GocatorRuntimeDiagnostics{}; };
    controller._lifecycle.grab = [](std::size_t) { return true; };
    controller._lifecycle.stop = [] {};

    auto promise = std::make_shared<std::promise<DeviceConnectionResult>>();
    auto future = promise->get_future();
    QCOMPARE(controller.requestSingle(controller.currentConnectionId(9),
        [promise](DeviceConnectionResult result) { promise->set_value(std::move(result)); }).error,
        DeviceConnectionError::None);
    QTRY_VERIFY(controller._singleInFlight.load());
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, true);
    GoPxLSdk::GoDataSet dataSet;
    if (validImage) dataSet.Add(GoGdpMsgTests::monoPixel());
    gocator.receiveDataForTest(dataSet);
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, false);

    const auto single = future.get();
    QCOMPARE(frames, validImage ? std::size_t{1} : std::size_t{0});
    QCOMPARE(single.error, validImage ? DeviceConnectionError::None : DeviceConnectionError::BackendFailure);
    QCOMPARE(single.effect, DeviceConnectionEffect::Applied);
}

void GocatorConnectionControlTests::closePublishesStoppedAfterDisconnect()
{
    Gocator gocator;
    gocator.notifyStatusForTest(Gocator::ConnectionStatus, true);
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, true);
    std::optional<bool> connectedWhenStopped;
    const auto observer = gocator.registerStatusCallback([&](Gocator::Status status, bool on) {
        if (status == Gocator::GrabbingStatus && !on) connectedWhenStopped = gocator.isOpened();
    });
    gocator.close();
    QVERIFY(connectedWhenStopped.has_value());
    QVERIFY(!*connectedWhenStopped);
    QVERIFY(!gocator.isGrabbing());
    gocator.deregisterStatusCallback(observer);
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

void GocatorConnectionControlTests::virtualTargetsGetDistinctAddressIdentity()
{
    FakeGocator fake;
    fake.devices = {
        {"192.168.0.105", "", "0", true},
        {"192.168.1.100", "", "0", true},
        {"192.168.0.153", "", "0", true},
        {"10.0.0.7", "", "0", true},
    };
    GocatorSourceController controller(nullptr);
    bind(controller, fake);
    const auto targets = awaitTargets(controller, [&](auto done) {
        return controller.requestConnectionTargets(done);
    });
    QCOMPARE(targets.result.error, DeviceConnectionError::None);
    QCOMPARE(targets.targets.size(), std::size_t{4});

    std::set<std::vector<std::pair<std::string, std::string>>> identities;
    for (std::size_t i = 0; i < targets.targets.size(); ++i) {
        const auto& target = targets.targets[i];
        std::vector<std::pair<std::string, std::string>> identity;
        for (const auto& attribute : target.attributes)
            identity.emplace_back(attribute.name, attribute.value);
        std::sort(identity.begin(), identity.end());
        QVERIFY2(identities.insert(identity).second,
            "virtual targets must carry distinct identity attributes");
        bool hasAddress = false;
        for (const auto& attribute : target.attributes) {
            if (attribute.name != "address") continue;
            hasAddress = true;
            QCOMPARE(attribute.value, fake.devices[i].address);
        }
        QVERIFY(hasAddress);
    }
    QCOMPARE(identities.size(), std::size_t{4});
}

void GocatorConnectionControlTests::duplicateDiscoveryCollapsesToSingleTarget()
{
    FakeGocator fake;
    fake.devices = {
        {"192.168.0.105", "", "0", true},
        {"192.168.0.105", "", "0", true},
        {"192.168.0.105", "", "0", true},
    };
    GocatorSourceController controller(nullptr);
    bind(controller, fake);
    const auto targets = awaitTargets(controller, [&](auto done) {
        return controller.requestConnectionTargets(done);
    });
    QCOMPARE(targets.result.error, DeviceConnectionError::None);
    QCOMPARE(targets.targets.size(), std::size_t{1});
    QCOMPARE(targets.targets[0].targetId, std::string("192.168.0.105"));
}

void GocatorConnectionControlTests::duplicateEndpointMergesMetadata()
{
    FakeGocator fake;
    fake.devices = {
        {"192.168.0.139", "", "0", true, 4400},
        {"192.168.0.139", "5512", "153472", false, 4400},
    };
    GocatorSourceController controller(nullptr);
    bind(controller, fake);
    const auto targets = awaitTargets(controller, [&](auto done) {
        return controller.requestConnectionTargets(done);
    });
    QCOMPARE(targets.result.error, DeviceConnectionError::None);
    QCOMPARE(targets.targets.size(), std::size_t{1});
    QCOMPARE(targets.targets[0].targetId, std::string("192.168.0.139:4400"));
    QCOMPARE(targets.targets[0].displayName, std::string("G5512 (153472) - 192.168.0.139:4400"));
    QCOMPARE(targets.targets[0].attributes[0].value, std::string("5512"));
    QCOMPARE(targets.targets[0].attributes[1].value, std::string("153472"));
    QCOMPARE(targets.targets[0].attributes[2].value, std::string("false"));
    const auto connected = awaitState(controller, [&](auto done) {
        return controller.requestConnect(targets.targets[0].targetId, done);
    });
    QCOMPARE(connected.error, DeviceConnectionError::None);
    QVERIFY(connected.state && connected.state->connected);
    QCOMPARE(connected.state->displayName, targets.targets[0].displayName);
}

void GocatorConnectionControlTests::sameAddressDifferentPortsStayDistinct()
{
    FakeGocator fake;
    fake.devices = {
        {"127.0.0.1", "5512", "153472", false, 0},
        {"192.168.0.139", "", "0", true, 3600},
        {"192.168.0.139", "", "0", true, 4400},
        {"192.168.0.139", "", "0", true, 4400},
    };
    GocatorSourceController controller(nullptr);
    bind(controller, fake);
    const auto targets = awaitTargets(controller, [&](auto done) {
        return controller.requestConnectionTargets(done);
    });
    QCOMPARE(targets.result.error, DeviceConnectionError::None);
    QCOMPARE(targets.targets.size(), std::size_t{2});
    QCOMPARE(targets.targets[0].targetId, std::string("192.168.0.139"));
    QCOMPARE(targets.targets[0].displayName, std::string("Unknown (Virtual) - 192.168.0.139"));
    QCOMPARE(targets.targets[1].targetId, std::string("192.168.0.139:4400"));
    QCOMPARE(targets.targets[1].displayName, std::string("Unknown (Virtual) - 192.168.0.139:4400"));
    bool hasPort = false;
    for (const auto& attribute : targets.targets[1].attributes) {
        if (attribute.name != "controlPort") continue;
        hasPort = true;
        QCOMPARE(attribute.value, std::string("4400"));
    }
    QVERIFY(hasPort);

    for (const auto& target : targets.targets) {
        const auto connected = awaitState(controller, [&](auto done) {
            return controller.requestConnect(target.targetId, done);
        });
        QCOMPARE(connected.error, DeviceConnectionError::None);
        QVERIFY(connected.state && connected.state->connected);
        QCOMPARE(connected.state->targetId, target.targetId);
        QCOMPARE(connected.state->displayName, target.displayName);
        const auto disconnected = awaitState(controller, [&](auto done) {
            return controller.requestDisconnect(connected.state->connectionId, done);
        });
        QCOMPARE(disconnected.error, DeviceConnectionError::None);
    }
    QCOMPARE(fake.opens, 2);
}

void GocatorConnectionControlTests::realSensorIdentitySurvivesAddressChange()
{
    const auto attributesFor = [](const std::string& address) {
        FakeGocator fake;
        fake.devices = {{address, "Gocator 2500", "12345", false}};
        GocatorSourceController controller(nullptr);
        bind(controller, fake);
        const auto targets = awaitTargets(controller, [&](auto done) {
            return controller.requestConnectionTargets(done);
        });
        return targets.targets.at(0).attributes;
    };
    const auto before = attributesFor("192.168.1.10");
    const auto after = attributesFor("192.168.1.20");
    QCOMPARE(before.size(), after.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        QCOMPARE(before[i].name, after[i].name);
        QCOMPARE(before[i].value, after[i].value);
        QVERIFY(before[i].name != "address");
    }
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

void GocatorConnectionControlTests::teardownStopFailureDoesNotEscape()
{
    FakeGocator fake;
    GocatorSourceController controller(nullptr);
    bind(controller, fake);
    controller._lifecycle.stop = [] {
        throw std::runtime_error("stop timeout");
    };
}

void GocatorConnectionControlTests::reentrantStopStatusDoesNotPublishStaleTrue()
{
    Gocator gocator;
    std::vector<bool> observed;
    const auto first = gocator.registerStatusCallback([&](Gocator::Status status, bool on) {
        if (status == Gocator::GrabbingStatus && on) gocator.stop();
    });
    const auto second = gocator.registerStatusCallback([&](Gocator::Status status, bool on) {
        if (status == Gocator::GrabbingStatus) observed.push_back(on);
    });
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, true);
    bool sawFalse = false;
    for (const bool value : observed) {
        if (sawFalse) QVERIFY(!value);
        if (!value) sawFalse = true;
    }
    QVERIFY(sawFalse);
    gocator.deregisterStatusCallback(first);
    gocator.deregisterStatusCallback(second);
}

void GocatorConnectionControlTests::imageDiagnosticsResetPerAcquisition()
{
    imageObservations.clear();
    FakeGocator fake;
    GocatorSourceController controller(nullptr, nullptr, nullptr, &collectImageObservation);
    bind(controller, fake);
    const auto connected = awaitState(controller, [&](auto done) {
        return controller.requestConnect("192.168.1.10", done);
    });
    QVERIFY(connected.state);
    QVERIFY(controller.beginGrab(0));
    GocatorImageRejection rejection;
    rejection.reason = GraphicsImageFailure::StorageRefused;
    rejection.width = 2;
    rejection.height = 1;
    controller.reportImageRejection(rejection, 11);
    controller.reportImageRejection(rejection, 12);
    QCOMPARE(imageObservations.size(), std::size_t{1});
    QCOMPARE(imageObservations.front().frameSequence, std::uint64_t{11});
    controller.stop();
    QVERIFY(controller.beginGrab(0));
    controller.reportImageRejection(rejection, 13);
    QCOMPARE(imageObservations.size(), std::size_t{2});
    QCOMPARE(imageObservations.back().frameSequence, std::uint64_t{13});
    controller.stop();
}

void GocatorConnectionControlTests::grabFailurePreservesEffectHint()
{
    FakeGocator fake;
    GocatorSourceController controller(nullptr);
    bind(controller, fake);
    const auto connected = awaitState(controller, [&](auto done) {
        return controller.requestConnect("192.168.1.10", done);
    });
    QVERIFY(connected.state);
    controller._lifecycle.grab = [](std::size_t) -> bool {
        throw Gocator::GrabError(Gocator::GrabFailure::StartUncertain, "start timeout", true);
    };
    const auto uncertain = awaitState(controller, [&](auto done) {
        return controller.requestLiveStart(connected.state->connectionId, done);
    });
    QCOMPARE(uncertain.error, DeviceConnectionError::BackendFailure);
    QCOMPARE(uncertain.effect, DeviceConnectionEffect::Unknown);
    controller._lifecycle.grab = [](std::size_t) -> bool {
        throw Gocator::GrabError(Gocator::GrabFailure::Attach, "replay stopped", false);
    };
    const auto refused = awaitState(controller, [&](auto done) {
        return controller.requestLiveStart(connected.state->connectionId, done);
    });
    QCOMPARE(refused.error, DeviceConnectionError::BackendFailure);
    QCOMPARE(refused.effect, DeviceConnectionEffect::None);
}

void GocatorConnectionControlTests::singleStopFailureIsNotSuccess_data()
{
    QTest::addColumn<bool>("delivered");
    QTest::newRow("frame delivered") << true;
    QTest::newRow("no frame delivered") << false;
}

void GocatorConnectionControlTests::singleStopFailureIsNotSuccess()
{
    QFETCH(bool, delivered);
    FakeGocator fake;
    GocatorSourceController controller(nullptr);
    bind(controller, fake);
    const auto connected = awaitState(controller, [&](auto done) {
        return controller.requestConnect("192.168.1.10", done);
    });
    QVERIFY(connected.state);
    auto promise = std::make_shared<std::promise<DeviceConnectionResult>>();
    auto future = promise->get_future();
    QCOMPARE(controller.requestSingle(connected.state->connectionId,
        [promise](DeviceConnectionResult result) { promise->set_value(std::move(result)); }).error,
        DeviceConnectionError::None);
    QTRY_VERIFY(controller._singleInFlight.load());
    if (delivered) controller.noteAcquisitionFrame();
    fake.diagnostics.stopFailed = true;
    fake.diagnostics.stopError = "stop timeout";
    controller.noteAcquisitionEnded();
    const auto result = future.get();
    QCOMPARE(result.error, DeviceConnectionError::BackendFailure);
    QCOMPARE(result.effect, DeviceConnectionEffect::Unknown);
    QCOMPARE(QString::fromStdString(result.message), QStringLiteral("stop timeout"));
}

namespace {

class TestManagedAllocation final : public GraphicsAllocation
{
public:
    [[nodiscard]] static GraphicsAllocation* create(
        const std::size_t capacityBytes,
        const std::uint64_t domainId,
        std::atomic<std::size_t>* const liveCount) noexcept
    {
        auto* rawBuffer = new (std::nothrow) std::byte[capacityBytes + 64U];
        if (!rawBuffer) return nullptr;
        const auto raw = reinterpret_cast<std::uintptr_t>(rawBuffer);
        auto* aligned = reinterpret_cast<std::byte*>((raw + 63U) & ~std::uintptr_t(63U));
        auto* alloc = new (std::nothrow) TestManagedAllocation(
            rawBuffer, aligned, capacityBytes, domainId, liveCount);
        if (!alloc) delete[] rawBuffer;
        return alloc;
    }

    ~TestManagedAllocation() override
    {
        if (_liveCount) _liveCount->fetch_sub(1U, std::memory_order_relaxed);
    }

    void destroy() noexcept override { delete this; }

private:
    TestManagedAllocation(
        std::byte* rawBuffer,
        std::byte* alignedBase,
        std::size_t capacityBytes,
        std::uint64_t domainId,
        std::atomic<std::size_t>* liveCount) noexcept
        : GraphicsAllocation(alignedBase, capacityBytes, GraphicsAllocationOrigin::Managed, 1U,
                             GraphicsAllocationState::Writable, domainId)
        , _buffer(rawBuffer)
        , _liveCount(liveCount)
    {
        if (_liveCount) _liveCount->fetch_add(1U, std::memory_order_relaxed);
    }

    std::unique_ptr<std::byte[]> _buffer;
    std::atomic<std::size_t>* _liveCount = nullptr;
};

class TestFrameAllocator final : public GraphicsFrameAllocator
{
public:
    TestFrameAllocator(
        GraphicsAllocationRef backing,
        std::size_t sampleLimit,
        std::uint64_t domainId,
        std::atomic<std::size_t>* reserveCalls)
        : _backing(std::move(backing))
        , _sampleLimit(sampleLimit)
        , _domainId(domainId)
        , _reserveCalls(reserveCalls)
    {
    }

    GraphicsResult<GraphicsAllocationSlice> allocate(
        const std::size_t bytes, const std::size_t alignment) noexcept override
    {
        if (_reserveCalls) _reserveCalls->fetch_add(1U, std::memory_order_relaxed);
        if (alignment == 0U || (alignment & (alignment - 1U)) != 0U)
            return GraphicsFrameError::InvalidInput;
        const auto baseAddr = reinterpret_cast<std::uintptr_t>(_backing->base());
        const auto curAddr = baseAddr + static_cast<std::uintptr_t>(_cursor);
        const std::size_t misalignment = static_cast<std::size_t>(curAddr & (alignment - 1U));
        const std::size_t pad = misalignment == 0U ? 0U : (alignment - misalignment);
        if (pad > _sampleLimit || _cursor + pad > _sampleLimit
            || bytes > _sampleLimit - (_cursor + pad))
            return GraphicsFrameError::Capacity;
        const std::size_t offset = _cursor + pad;
        _cursor = offset + bytes;
        return GraphicsAllocationSlice{_backing, offset, bytes};
    }

    bool isManaged() const noexcept override { return true; }
    std::uint64_t domainId() const noexcept override { return _domainId; }

private:
    GraphicsAllocationRef _backing;
    std::size_t _sampleLimit = 0U;
    std::uint64_t _domainId = 0U;
    std::size_t _cursor = 0U;
    std::atomic<std::size_t>* _reserveCalls = nullptr;
};

class ScriptedContextSource final : public GraphicsFrameContextSource
{
public:
    static constexpr std::uint64_t kDomainId = 9001U;

    std::atomic<std::size_t> calls{0};
    std::atomic<std::size_t> reserveCalls{0};
    std::atomic<std::size_t> liveAllocations{0};
    std::atomic<GraphicsFrameError> nextError{GraphicsFrameError::None};
    std::function<void()> onAfterBeginFrame;

    GraphicsResult<GraphicsFrameContext> beginFrame(const std::size_t sampleBytes) noexcept override
    {
        calls.fetch_add(1U, std::memory_order_relaxed);
        const auto err = nextError.load(std::memory_order_acquire);
        if (err != GraphicsFrameError::None) return err;
        auto* rawAlloc = TestManagedAllocation::create(sampleBytes, kDomainId, &liveAllocations);
        if (!rawAlloc) return GraphicsFrameError::Allocation;
        auto allocator = std::make_shared<TestFrameAllocator>(
            GraphicsAllocationRef::adoptNew(rawAlloc), sampleBytes, kDomainId, &reserveCalls);
        GraphicsFrameContext ctx(std::move(allocator));
        if (onAfterBeginFrame) onAfterBeginFrame();
        return ctx;
    }
};

} // namespace

void GocatorConnectionControlTests::bindSyntheticGocator(
    GocatorSourceController& controller, std::atomic<bool>& grabbing)
{
    controller._lifecycle.snapshot = [] {
        return Gocator::ConnectionSnapshot{true, 9, "10.0.0.9", "10.0.0.9"};
    };
    controller._lifecycle.beginControl = [] { return true; };
    controller._lifecycle.endControl = [] {};
    controller._lifecycle.diagnostics = [&grabbing] {
        return GocatorRuntimeDiagnostics{grabbing.load(), 0, 0};
    };
    controller._lifecycle.grab = [&grabbing](std::size_t) {
        grabbing.store(true);
        return true;
    };
    controller._lifecycle.stop = [&grabbing] { grabbing.store(false); };
}

void GocatorConnectionControlTests::frameContextRequiredWithoutFallback()
{
    FakeGocator fake;
    GocatorSourceController controller(nullptr);
    bind(controller, fake);

    auto* client = qobject_cast<IFrameContextClient*>(&controller);
    QVERIFY(client != nullptr);
    QCOMPARE(QString::fromLatin1(qobject_interface_iid<IFrameContextClient*>()),
             QStringLiteral("org.playground.DeviceRuntime.FrameContextClient/1"));

    const auto connected = awaitState(controller, [&](auto done) {
        return controller.requestConnect("192.168.1.10", done);
    });
    QVERIFY(connected.state && connected.state->connected);
    const std::string connectionId = connected.state->connectionId;

    // Clearing the context source must refuse Single, LiveStart, and start() without Local fallback.
    client->setFrameContextSource({});
    int completions = 0;
    const auto single = controller.requestSingle(connectionId, [&](auto) { ++completions; });
    QCOMPARE(single.error, DeviceConnectionError::BackendFailure);
    QCOMPARE(single.effect, DeviceConnectionEffect::None);
    const auto live = controller.requestLiveStart(connectionId, [&](auto) { ++completions; });
    QCOMPARE(live.error, DeviceConnectionError::BackendFailure);
    QCOMPARE(live.effect, DeviceConnectionEffect::None);
    QCOMPARE(completions, 0);
    QCOMPARE(fake.singleGrabs, std::size_t{0});
    QCOMPARE(fake.liveGrabs, std::size_t{0});

    bool startThrew = false;
    try {
        controller.start();
    } catch (const std::runtime_error&) {
        startThrew = true;
    }
    QVERIFY(startThrew);
    QCOMPARE(fake.liveGrabs, std::size_t{0});
}

void GocatorConnectionControlTests::frameContextTransactionAndNoReadyCreditDrop()
{
    Gocator gocator;
    GocatorSourceController controller(&gocator);
    std::atomic<bool> grabbing{false};
    bindSyntheticGocator(controller, grabbing);

    auto source = std::make_shared<ScriptedContextSource>();
    std::atomic<std::size_t> delivered{0};
    controller.setFrameConsumer([&delivered](SessionFrame&& frame, unsigned) {
        if (frame.payload.hasImage()) delivered.fetch_add(1);
    });
    controller.setFrameContextSource(source);

    const auto live = awaitState(controller, [&](auto done) {
        return controller.requestLiveStart(controller.currentConnectionId(9), done);
    });
    QCOMPARE(live.error, DeviceConnectionError::None);
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, true);

    GoPxLSdk::GoDataSet dataSet;
    dataSet.Add(GoGdpMsgTests::monoPixel());

    // Success: one beginFrame + one reserve, live allocation released after frame drops.
    gocator.receiveDataForTest(dataSet);
    QCOMPARE(delivered.load(), std::size_t{1});
    QCOMPARE(source->calls.load(), std::size_t{1});
    QCOMPARE(source->reserveCalls.load(), std::size_t{1});
    QCOMPARE(controller.droppedFrames(), std::uint64_t{0});

    // NoReadyCredit: drop before reserve/copy, count the drop, and recover when credit returns.
    source->nextError.store(GraphicsFrameError::NoReadyCredit, std::memory_order_release);
    gocator.receiveDataForTest(dataSet);
    gocator.receiveDataForTest(dataSet);
    QCOMPARE(delivered.load(), std::size_t{1});
    QCOMPARE(source->calls.load(), std::size_t{3});
    QCOMPARE(source->reserveCalls.load(), std::size_t{1});
    QCOMPARE(controller.droppedFrames(), std::uint64_t{2});
    QCOMPARE(controller.sourceRuntimeDiagnostics(0)->failed, std::uint64_t{2});

    source->nextError.store(GraphicsFrameError::None, std::memory_order_release);
    gocator.receiveDataForTest(dataSet);
    QCOMPARE(delivered.load(), std::size_t{2});
    QCOMPARE(source->calls.load(), std::size_t{4});
    QCOMPARE(source->reserveCalls.load(), std::size_t{2});

    controller.stop();
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, false);
    QCOMPARE(source->liveAllocations.load(), std::size_t{0});
}

void GocatorConnectionControlTests::frameContextRefusalDropsOnlyThatFrame()
{
    Gocator gocator;
    GocatorSourceController controller(&gocator);
    std::atomic<bool> grabbing{false};
    bindSyntheticGocator(controller, grabbing);

    auto source = std::make_shared<ScriptedContextSource>();
    std::atomic<std::size_t> delivered{0};
    controller.setFrameConsumer([&delivered](SessionFrame&&, unsigned) { delivered.fetch_add(1); });
    controller.setFrameContextSource(source);

    const std::string connectionId = controller.currentConnectionId(9);
    source->nextError.store(GraphicsFrameError::ClassRefused, std::memory_order_release);

    // A Live run keeps acquiring after a refusal, so it still exercises "drop only that frame".
    // A Single completes at its first refusal; that is covered by
    // singleCompletesPromptlyAfterStorageRefusal.
    const auto live = awaitState(controller, [&](auto done) {
        return controller.requestLiveStart(connectionId, done);
    });
    QCOMPARE(live.error, DeviceConnectionError::None);
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, true);
    GoPxLSdk::GoDataSet dataSet;
    dataSet.Add(GoGdpMsgTests::monoPixel());

    // The first dataset hits ClassRefused and is dropped and counted; the next dataset in the
    // same run asks the source again and publishes.
    gocator.receiveDataForTest(dataSet);
    QCOMPARE(source->calls.load(), std::size_t{1});
    QCOMPARE(delivered.load(), std::size_t{0});
    QCOMPARE(controller._droppedFrames.load(), std::uint64_t{1});
    source->nextError.store(GraphicsFrameError::None, std::memory_order_release);
    gocator.receiveDataForTest(dataSet);
    QCOMPARE(source->calls.load(), std::size_t{2});
    QCOMPARE(delivered.load(), std::size_t{1});

    controller.stop();
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, false);

    // A Single whose only dataset was refused reports the refusal; it does not latch, so the
    // following Single asks the source again and succeeds.
    source->nextError.store(GraphicsFrameError::Capacity, std::memory_order_release);
    auto refusedPromise = std::make_shared<std::promise<DeviceConnectionResult>>();
    auto refusedFuture = refusedPromise->get_future();
    QCOMPARE(controller.requestSingle(connectionId, [refusedPromise](DeviceConnectionResult result) {
        refusedPromise->set_value(std::move(result));
    }).error, DeviceConnectionError::None);
    QTRY_VERIFY(controller._singleInFlight.load());
    QTRY_VERIFY(grabbing.load());
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, true);
    gocator.receiveDataForTest(dataSet);
    // The refusal completes the Single before the acquisition reports that it ended.
    QCOMPARE(refusedFuture.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    grabbing.store(false);
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, false);
    const auto refused = refusedFuture.get();
    QCOMPARE(refused.error, DeviceConnectionError::BackendFailure);
    QVERIFY(!refused.message.empty());
    QCOMPARE(delivered.load(), std::size_t{1});

    source->nextError.store(GraphicsFrameError::None, std::memory_order_release);

    auto okPromise = std::make_shared<std::promise<DeviceConnectionResult>>();
    auto okFuture = okPromise->get_future();
    QCOMPARE(controller.requestSingle(connectionId, [okPromise](DeviceConnectionResult result) {
        okPromise->set_value(std::move(result));
    }).error, DeviceConnectionError::None);
    QTRY_VERIFY(controller._singleInFlight.load());
    QTRY_VERIFY(grabbing.load());
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, true);
    gocator.receiveDataForTest(dataSet);
    grabbing.store(false);
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, false);
    const auto ok = okFuture.get();
    QCOMPARE(ok.error, DeviceConnectionError::None);
    QCOMPARE(delivered.load(), std::size_t{2});
}

void GocatorConnectionControlTests::singleCompletesPromptlyAfterStorageRefusal()
{
    struct Case { GraphicsFrameError error; const char* reason; };
    const Case cases[] = {
        {GraphicsFrameError::ClassRefused, "class refused by host"},
        {GraphicsFrameError::Capacity, "exceeds capacity"},
        {GraphicsFrameError::ClassNotPrepared, "not prepared"},
    };
    for (const auto& refusal : cases) {
        Gocator gocator;
        GocatorSourceController controller(&gocator);
        std::atomic<bool> grabbing{false};
        bindSyntheticGocator(controller, grabbing);
        auto source = std::make_shared<ScriptedContextSource>();
        std::atomic<std::size_t> delivered{0};
        controller.setFrameConsumer([&delivered](SessionFrame&&, unsigned) { delivered.fetch_add(1); });
        controller.setFrameContextSource(source);
        source->nextError.store(refusal.error, std::memory_order_release);

        auto promise = std::make_shared<std::promise<DeviceConnectionResult>>();
        auto future = promise->get_future();
        QCOMPARE(controller.requestSingle(controller.currentConnectionId(9),
            [promise](DeviceConnectionResult result) { promise->set_value(std::move(result)); }).error,
            DeviceConnectionError::None);
        QTRY_VERIFY(controller._singleInFlight.load());
        QTRY_VERIFY(grabbing.load());
        gocator.notifyStatusForTest(Gocator::GrabbingStatus, true);
        GoPxLSdk::GoDataSet dataSet;
        dataSet.Add(GoGdpMsgTests::monoPixel());
        gocator.receiveDataForTest(dataSet);

        // The Single completes at the refusal: no 30 s deadline, no wait for the acquisition end
        // notification, which this test deliberately never sends before the result is ready.
        QCOMPARE(future.wait_for(std::chrono::seconds(10)), std::future_status::ready);
        const auto result = future.get();
        QCOMPARE(result.error, DeviceConnectionError::BackendFailure);
        QVERIFY2(result.message.find(refusal.reason) != std::string::npos, result.message.c_str());
        QCOMPARE(result.effect, DeviceConnectionEffect::Applied);
        QCOMPARE(delivered.load(), std::size_t{0});
        QCOMPARE(controller._droppedFrames.load(), std::uint64_t{1});
        QVERIFY(!grabbing.load());
        gocator.notifyStatusForTest(Gocator::GrabbingStatus, false);
    }
}

void GocatorConnectionControlTests::frameAnomaliesReachTheDiagnosticSinks()
{
    imageObservations.clear();
    rangeObservations.clear();
    Gocator gocator;
    GocatorSourceController controller(&gocator, nullptr, &collectRangeObservation, &collectImageObservation);
    std::atomic<bool> grabbing{false};
    bindSyntheticGocator(controller, grabbing);
    auto source = std::make_shared<ScriptedContextSource>();
    std::atomic<std::size_t> delivered{0};
    controller.setFrameConsumer([&delivered](SessionFrame&& frame, unsigned) {
        if (frame.payload.hasImage()) delivered.fetch_add(1);
    });
    controller.setFrameContextSource(source);

    const auto live = awaitState(controller, [&](auto done) {
        return controller.requestLiveStart(controller.currentConnectionId(9), done);
    });
    QCOMPARE(live.error, DeviceConnectionError::None);
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, true);

    // An image in a format the adapter does not know is still published, as raw bytes, and the
    // anomaly is counted through the existing image diagnostic sink.
    GoPxLSdk::GoDataSet dataSet;
    dataSet.Add(GoGdpMsgTests::unknownFormatPixel());
    gocator.receiveDataForTest(dataSet);
    QCOMPARE(delivered.load(), std::size_t{1});
    QCOMPARE(controller._droppedFrames.load(), std::uint64_t{0});
    QCOMPARE(imageObservations.size(), std::size_t{1});
    QCOMPARE(imageObservations.front().reason, DeviceImageRejection::UnsupportedFormat);
    // The same dataset has no range part: the missing range is counted too and the image stays.
    QCOMPARE(rangeObservations.size(), std::size_t{1});
    QCOMPARE(rangeObservations.front().reason, DeviceRangeRejection::MissingRange);
    QVERIFY(rangeObservations.front().frameRetained && rangeObservations.front().imageRetained);

    controller.stop();
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, false);
}

void GocatorConnectionControlTests::cancellationOrStopReleasesOpenBuilder()
{
    Gocator gocator;
    GocatorSourceController controller(&gocator);
    std::atomic<bool> grabbing{false};
    bindSyntheticGocator(controller, grabbing);

    auto source = std::make_shared<ScriptedContextSource>();
    auto entered = std::make_shared<std::promise<void>>();
    auto enteredFuture = entered->get_future();
    auto release = std::make_shared<std::promise<void>>();
    auto releaseFuture = release->get_future().share();
    source->onAfterBeginFrame = [entered, releaseFuture]() {
        entered->set_value();
        releaseFuture.wait();
    };

    std::atomic<std::size_t> delivered{0};
    controller.setFrameConsumer([&delivered](SessionFrame&&, unsigned) { delivered.fetch_add(1); });
    controller.setFrameContextSource(source);

    const auto live = awaitState(controller, [&](auto done) {
        return controller.requestLiveStart(controller.currentConnectionId(9), done);
    });
    QCOMPARE(live.error, DeviceConnectionError::None);
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, true);

    GoPxLSdk::GoDataSet dataSet;
    dataSet.Add(GoGdpMsgTests::monoPixel());
    auto callbackWorker = std::async(std::launch::async, [&] {
        gocator.receiveDataForTest(dataSet);
    });

    QCOMPARE(enteredFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    QCOMPARE(source->liveAllocations.load(), std::size_t{1});

    // Stopping while the builder/context is open revokes the run; once unblocked, it must abort
    // under RAII without publishing or retaining the backing.
    controller.stop();
    release->set_value();
    callbackWorker.wait();

    QCOMPARE(delivered.load(), std::size_t{0});
    QCOMPARE(source->liveAllocations.load(), std::size_t{0});
}

void GocatorConnectionControlTests::stopAndRearmIgnoresLateCallbackFromOldRun()
{
    Gocator gocator;
    GocatorSourceController controller(&gocator);
    std::atomic<bool> grabbing{false};
    bindSyntheticGocator(controller, grabbing);

    auto source = std::make_shared<ScriptedContextSource>();
    auto entered = std::make_shared<std::promise<void>>();
    auto enteredFuture = entered->get_future();
    auto release = std::make_shared<std::promise<void>>();
    auto releaseFuture = release->get_future().share();
    std::atomic<bool> blockFirst{true};
    source->onAfterBeginFrame = [&blockFirst, entered, releaseFuture]() {
        if (blockFirst.exchange(false)) {
            entered->set_value();
            releaseFuture.wait();
        }
    };

    std::vector<std::uint64_t> deliveredIndices;
    std::mutex deliveredMutex;
    controller.setFrameConsumer([&](SessionFrame&& frame, unsigned) {
        std::lock_guard<std::mutex> lock(deliveredMutex);
        deliveredIndices.push_back(frame.payload.metadata().frameIndex);
    });
    controller.setFrameContextSource(source);

    // Run 1 (Live): starts and enters beginFrame on a background thread.
    QCOMPARE(awaitState(controller, [&](auto done) {
        return controller.requestLiveStart(controller.currentConnectionId(9), done);
    }).error, DeviceConnectionError::None);
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, true);

    GoPxLSdk::GoDataSet oldDataSet;
    auto oldImg = GoGdpMsgTests::monoPixel();
    oldDataSet.Add(oldImg);
    auto oldWorker = std::async(std::launch::async, [&] {
        gocator.receiveDataForTest(oldDataSet);
    });
    QCOMPARE(enteredFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    // Stop Run 1 while its callback is still blocked, then arm Run 2 (Single) while Run 1 is
    // still unwinding. Run 1's late callback must abort without completing Run 2's Single.
    controller.stop();
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, false);

    auto singlePromise = std::make_shared<std::promise<DeviceConnectionResult>>();
    auto singleFuture = singlePromise->get_future();
    QCOMPARE(controller.requestSingle(controller.currentConnectionId(9), [singlePromise](DeviceConnectionResult result) {
        singlePromise->set_value(std::move(result));
    }).error, DeviceConnectionError::None);
    QTRY_VERIFY(controller._singleInFlight.load());

    release->set_value();
    oldWorker.wait();
    {
        std::lock_guard<std::mutex> lock(deliveredMutex);
        QVERIFY(deliveredIndices.empty());
    }
    QCOMPARE(singleFuture.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);

    QTRY_VERIFY(grabbing.load());
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, true);

    GoPxLSdk::GoDataSet newDataSet;
    newDataSet.Add(GoGdpMsgTests::monoPixel());
    gocator.receiveDataForTest(newDataSet);
    grabbing.store(false);
    gocator.notifyStatusForTest(Gocator::GrabbingStatus, false);

    const auto singleResult = singleFuture.get();
    QCOMPARE(singleResult.error, DeviceConnectionError::None);
    {
        std::lock_guard<std::mutex> lock(deliveredMutex);
        QCOMPARE(deliveredIndices.size(), std::size_t{1});
    }
    QCOMPARE(source->liveAllocations.load(), std::size_t{0});
}

QTEST_MAIN(GocatorConnectionControlTests)
#include "GocatorConnectionControlTests.moc"
