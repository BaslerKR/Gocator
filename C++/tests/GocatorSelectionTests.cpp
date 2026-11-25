#include "Gocator.h"
#include "Internal/GocatorDataSource.h"
#include "Internal/GocatorDiscovery.h"
#include "Internal/GocatorOutputRequest.h"
#include "Internal/GocatorSelection.h"
#include "Internal/GocatorSystemStartOwnership.h"

#include <QtTest/QtTest>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

class GocatorSelectionTests final : public QObject {
    Q_OBJECT
private slots:
    void dataSourceIdFromLink_data();
    void dataSourceIdFromLink();
    void engineDetectionDistinguishesEmptyAndFailure();
    void replayDataSourceReadFailurePropagates();
    void replayDataSourceMalformedResponseFails();
    void replaySelectionPrefersProfileAndFallsBack();
    void replaySelectionSkipsInvalidAndKeepsOrder();
    void replayImageSelectionSkipsNonImages();
    void outputRequestEscapesSource();
    void cachedControlPortIsUsed();
    void unknownControlPortUsesDefault();
    void ambiguousControlPortIsRejected();
    void targetIdEncodingAndParsing();
    void controlPortSelectionSkipsZeroAndPrefersDefault();
    void systemStartOwnershipTracksOnlyConfirmedStarts();
    void discoveryEligibilityRequiresUsablePorts();
};

void GocatorSelectionTests::dataSourceIdFromLink_data()
{
    QTest::addColumn<QString>("link");
    QTest::addColumn<QString>("id");
    QTest::newRow("api") << "/api//dataSources/Gocator.Top" << "Gocator.Top";
    QTest::newRow("resource") << "/dataSources/Replay.Surface" << "Replay.Surface";
    QTest::newRow("trailing") << "/dataSources/Gocator.Top/" << "Gocator.Top";
    QTest::newRow("empty") << "/dataSources/" << "";
    QTest::newRow("nested") << "/dataSources/group/source" << "";
    QTest::newRow("unrelated") << "/scan/engines/LMILaserLineProfiler" << "";
}

void GocatorSelectionTests::dataSourceIdFromLink()
{
    QFETCH(QString, link);
    QFETCH(QString, id);
    QCOMPARE(QString::fromStdString(gocator::dataSourceIdFromLink(link.toStdString())), id);
}

void GocatorSelectionTests::engineDetectionDistinguishesEmptyAndFailure()
{
    const auto empty = gocator::detectEngineId([](const std::string&) {
        return GoPxLSdk::GoJson(R"({"sensors":[]})");
    });
    QVERIFY(!empty.engineId);
    QVERIFY(!empty.failed);

    const auto malformed = gocator::detectEngineId([](const std::string&) {
        return GoPxLSdk::GoJson(R"({})");
    });
    QVERIFY(!malformed.engineId);
    QVERIFY(malformed.failed);

    const auto emptyEngine = gocator::detectEngineId([](const std::string&) {
        return GoPxLSdk::GoJson(R"({"sensors":[{"engineId":""}]})");
    });
    QVERIFY(!emptyEngine.engineId);
    QVERIFY(emptyEngine.failed);

    const auto timeout = gocator::detectEngineId([](const std::string&) -> GoPxLSdk::GoJson {
        throw std::runtime_error("timeout");
    });
    QVERIFY(!timeout.engineId);
    QVERIFY(timeout.failed);
}

void GocatorSelectionTests::replayDataSourceReadFailurePropagates()
{
    const GoPxLSdk::GoJson collection(R"({"_embedded":{"item":[
        {"_links":{"self":{"href":"/dataSources/Replay.Profile"}}}
    ]}})");
    QVERIFY_EXCEPTION_THROWN((void)gocator::replayDataSources([&](const std::string& path) {
        if (path == "/dataSources/") return collection;
        throw std::runtime_error("timeout");
    }), std::runtime_error);
}

void GocatorSelectionTests::replayDataSourceMalformedResponseFails()
{
    const GoPxLSdk::GoJson badLink(R"({"_embedded":{"item":[{}]}})");
    QVERIFY_EXCEPTION_THROWN((void)gocator::replayDataSources(
        [&](const std::string&) { return badLink; }), std::runtime_error);

    const GoPxLSdk::GoJson collection(R"({"_embedded":{"item":[
        {"_links":{"self":{"href":"/dataSources/Replay.Profile"}}}
    ]}})");
    QVERIFY_EXCEPTION_THROWN((void)gocator::replayDataSources([&](const std::string& path) {
        if (path == "/dataSources/") return collection;
        return GoPxLSdk::GoJson(R"({"dataFormat":{}})");
    }), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN((void)gocator::replayDataSources(
        [&](const std::string&) { return GoPxLSdk::GoJson(R"({})"); }), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN((void)gocator::replayDataSources([&](const std::string& path) {
        if (path == "/dataSources/") return collection;
        return GoPxLSdk::GoJson(R"({"dataFormat":{"type":65552}})");
    }), std::runtime_error);
}

void GocatorSelectionTests::replaySelectionPrefersProfileAndFallsBack()
{
    using GoPxLSdk::MessageType;
    const std::vector<gocator::GocatorDataSource> sources{
        {"surface", MessageType::SURFACE_POINT_CLOUD},
        {"profile", MessageType::UNIFORM_PROFILE},
        {"uniform-surface", MessageType::UNIFORM_SURFACE},
        {"raw-profile", MessageType::PROFILE_POINT_CLOUD},
    };
    QCOMPARE(gocator::selectReplayDataSource(sources), std::optional<std::string>("profile"));

    const std::vector<gocator::GocatorDataSource> surfaceOnly{
        {"surface", MessageType::UNIFORM_SURFACE},
    };
    QCOMPARE(gocator::selectReplayDataSource(surfaceOnly), std::optional<std::string>("surface"));
}

void GocatorSelectionTests::replaySelectionSkipsInvalidAndKeepsOrder()
{
    using GoPxLSdk::MessageType;
    const std::vector<gocator::GocatorDataSource> sources{
        {"", MessageType::UNIFORM_SURFACE},
        {"measurement", MessageType::MEASUREMENT},
        {"first", MessageType::UNIFORM_PROFILE},
        {"second", MessageType::PROFILE_POINT_CLOUD},
    };
    QCOMPARE(gocator::selectReplayDataSource(sources), std::optional<std::string>("first"));
    QVERIFY(!gocator::selectReplayDataSource({}));
    QVERIFY(!gocator::selectReplayDataSource({{"measurement", MessageType::MEASUREMENT}}));
}

void GocatorSelectionTests::replayImageSelectionSkipsNonImages()
{
    using GoPxLSdk::MessageType;
    const std::vector<gocator::GocatorDataSource> sources{
        {"stamp", MessageType::STAMP},
        {"surface", MessageType::UNIFORM_SURFACE},
        {"image", MessageType::IMAGE},
        {"second-image", MessageType::IMAGE},
    };
    QCOMPARE(gocator::selectReplayImageDataSource(sources), std::optional<std::string>("image"));
    QVERIFY(!gocator::selectReplayImageDataSource({{"surface", MessageType::UNIFORM_SURFACE}}));
}

void GocatorSelectionTests::outputRequestEscapesSource()
{
    const std::string source = "scan:engine:scanner-0:image\"\\";
    const auto payload = gocator::outputRequest(source, 1);
    QCOMPARE(payload.Get<std::string>("/source"), source);
    QCOMPARE(payload.Get<int>("/outputId"), 1);
    QVERIFY(payload.Get<bool>("/autoShift"));
}

void GocatorSelectionTests::cachedControlPortIsUsed()
{
    const std::vector<Gocator::DeviceInfo> devices{
        {"192.168.0.10", "G2500", "1", false, 3600},
        {"192.168.0.11", "G2500", "2", true, 3690},
    };
    const auto cached = gocator::controlPortForTarget(devices, "192.168.0.11");
    QCOMPARE(cached.port, std::uint16_t{3690});
    QVERIFY(!cached.ambiguous);
}

void GocatorSelectionTests::unknownControlPortUsesDefault()
{
    const std::vector<Gocator::DeviceInfo> devices{
        {"192.168.0.11", "G2500", "2", true, 3690},
    };
    const auto unknown = gocator::controlPortForTarget(devices, "127.0.0.1");
    QCOMPARE(unknown.port, gocator::kDefaultControlPort);
    QVERIFY(!unknown.ambiguous);
    const auto empty = gocator::controlPortForTarget(
        std::vector<Gocator::DeviceInfo>{}, "127.0.0.1");
    QCOMPARE(empty.port, gocator::kDefaultControlPort);
    QVERIFY(!empty.ambiguous);
}

void GocatorSelectionTests::ambiguousControlPortIsRejected()
{
    const std::vector<Gocator::DeviceInfo> devices{
        {"192.168.0.139", "", "0", true, 4400},
        {"192.168.0.139", "", "0", true, 4500},
    };
    const auto cached = gocator::controlPortForTarget(devices, "192.168.0.139");
    QVERIFY(cached.ambiguous);
    QCOMPARE(cached.port, std::uint16_t{0});
}

void GocatorSelectionTests::targetIdEncodingAndParsing()
{
    const Gocator::DeviceInfo defaultTarget{"192.168.0.139", "", "0", true, 3600};
    const Gocator::DeviceInfo emulatorTarget{"192.168.0.139", "", "0", true, 4400};
    QCOMPARE(gocator::targetIdForDevice(defaultTarget), std::string("192.168.0.139"));
    QCOMPARE(gocator::targetIdForDevice(emulatorTarget), std::string("192.168.0.139:4400"));

    const auto plain = gocator::parseTargetId("192.168.0.139");
    QCOMPARE(plain.address, std::string("192.168.0.139"));
    QCOMPARE(plain.controlPort, gocator::kDefaultControlPort);
    QVERIFY(!plain.explicitPort);

    const auto emulator = gocator::parseTargetId("192.168.0.139:4400");
    QCOMPARE(emulator.address, std::string("192.168.0.139"));
    QCOMPARE(emulator.controlPort, std::uint16_t{4400});
    QVERIFY(emulator.explicitPort);

    for (const auto* invalid : {":4400", "192.168.0.139:0", "192.168.0.139:65536",
                                "192.168.0.139:bad"}) {
        const auto parsed = gocator::parseTargetId(invalid);
        QCOMPARE(parsed.address, std::string(invalid));
        QCOMPARE(parsed.controlPort, gocator::kDefaultControlPort);
        QVERIFY(!parsed.explicitPort);
    }
}

void GocatorSelectionTests::controlPortSelectionSkipsZeroAndPrefersDefault()
{
    const std::vector<Gocator::DeviceInfo> emulator{
        {"192.168.0.139", "5512", "153472", false, 0},
        {"192.168.0.139", "", "0", true, 4400},
    };
    const auto selected = gocator::controlPortForTarget(emulator, "192.168.0.139");
    QCOMPARE(selected.port, std::uint16_t{4400});
    QVERIFY(!selected.ambiguous);

    const std::vector<Gocator::DeviceInfo> withDefault{
        {"192.168.0.139", "", "0", true, 4400},
        {"192.168.0.139", "", "0", true, 4500},
        {"192.168.0.139", "", "0", true, 3600},
    };
    const auto preferred = gocator::controlPortForTarget(withDefault, "192.168.0.139");
    QCOMPARE(preferred.port, gocator::kDefaultControlPort);
    QVERIFY(!preferred.ambiguous);
}

void GocatorSelectionTests::systemStartOwnershipTracksOnlyConfirmedStarts()
{
    gocator::GocatorSystemStartOwnership ownership;
    QVERIFY(!ownership.started());
    QVERIFY(!ownership.end());
    ownership.markStarted();
    QVERIFY(ownership.started());
    ownership.markStopUncertain();
    QVERIFY(ownership.stopUncertain());
    QVERIFY(ownership.end());
    QVERIFY(!ownership.started());
    QVERIFY(!ownership.stopUncertain());
    QVERIFY(!ownership.end());
    ownership.markStarted();
    ownership.reset();
    QVERIFY(!ownership.started());
    QVERIFY(!ownership.stopUncertain());
}

void GocatorSelectionTests::discoveryEligibilityRequiresUsablePorts()
{
    gocator::GocatorDeviceInfo device;
    device.address = "192.168.0.139";
    QVERIFY(device.canConnectLocally());
    device.controlPort = 0;
    QVERIFY(!device.canConnectLocally());
    device.controlPort = gocator::kDefaultControlPort;
    device.gdpPort = 0;
    QVERIFY(!device.canConnectLocally());
    device.gdpPort = gocator::kDefaultGdpPort;
    device.addressConflict = true;
    QVERIFY(!device.canConnectLocally());
    device.addressConflict = false;
    device.remote = true;
    QVERIFY(!device.canConnectLocally());
}

QTEST_APPLESS_MAIN(GocatorSelectionTests)
#include "GocatorSelectionTests.moc"
