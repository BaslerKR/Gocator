#include "Internal/GocatorConnection.h"

#include <GoPxLSdk/Def.h>
#include <GoPxLSdk/GoChannelError.h>

#include <QtTest/QtTest>
#include <memory>
#include <stdexcept>

namespace {

class FakeSystemControl final : public gocator::GocatorSystemControl
{
public:
    GoPxLSdk::GoSystem::State state = GoPxLSdk::GoSystem::State::Ready;
    int starts = 0;
    int stops = 0;
    bool stateTimeout = false;
    bool startTimeout = false;
    bool stopTimeout = false;
    bool startRefused = false;
    bool stopRefused = false;

    GoPxLSdk::GoSystem::State runningState() override
    {
        if (stateTimeout) throw GoPxLSdk::GoChannelError("state timeout");
        return state;
    }

    void start() override
    {
        ++starts;
        if (startTimeout) throw GoPxLSdk::GoChannelError("start timeout");
        if (startRefused) throw std::runtime_error("start refused");
        state = GoPxLSdk::GoSystem::State::Running;
    }

    void stop() override
    {
        ++stops;
        if (stopTimeout) throw GoPxLSdk::GoChannelError("stop timeout");
        if (stopRefused) throw std::runtime_error("stop refused");
        state = GoPxLSdk::GoSystem::State::Ready;
    }
};

std::unique_ptr<FakeSystemControl> control()
{
    return std::make_unique<FakeSystemControl>();
}

gocator::GocatorConnectionConfig config()
{
    return {"127.0.0.1", gocator::kDefaultControlPort, 1000};
}

}

class GocatorConnectionStateTests final : public QObject
{
    Q_OBJECT
private slots:
    void confirmedStartAndAlreadyRunning();
    void stateQueryTimeoutDoesNotStart();
    void conflictDoesNotStart();
    void startTimeoutDoesNotClaimOwnership();
    void stopTimeoutRetainsOwnershipAndBlocksAttach();
    void uncertainStopClearsOnlyAfterObservedStop();
    void attachNeverStarts();
    void attachedReplayIsNeverStopped();
};

void GocatorConnectionStateTests::confirmedStartAndAlreadyRunning()
{
    auto fake = control();
    auto* pointer = fake.get();
    gocator::GocatorConnection connection(config(), std::move(fake));
    QCOMPARE(connection.tryStart(), gocator::GocatorStartResult::Started);
    QCOMPARE(pointer->starts, 1);
    QCOMPARE(connection.tryStart(), gocator::GocatorStartResult::AlreadyRunning);
    QCOMPARE(pointer->starts, 1);
    QCOMPARE(connection.tryStop(), gocator::GocatorStopResult::Stopped);
    QCOMPARE(pointer->stops, 1);
}

void GocatorConnectionStateTests::stateQueryTimeoutDoesNotStart()
{
    auto fake = control();
    auto* pointer = fake.get();
    pointer->stateTimeout = true;
    gocator::GocatorConnection connection(config(), std::move(fake));
    QCOMPARE(connection.tryStart(), gocator::GocatorStartResult::StateUnknown);
    QCOMPARE(pointer->starts, 0);
}

void GocatorConnectionStateTests::startTimeoutDoesNotClaimOwnership()
{
    auto fake = control();
    auto* pointer = fake.get();
    pointer->startTimeout = true;
    gocator::GocatorConnection connection(config(), std::move(fake));
    QCOMPARE(connection.tryStart(), gocator::GocatorStartResult::StartUncertain);
    QCOMPARE(pointer->starts, 1);
    QCOMPARE(connection.tryStop(), gocator::GocatorStopResult::NotOwned);
    QCOMPARE(pointer->stops, 0);
}

void GocatorConnectionStateTests::conflictDoesNotStart()
{
    auto fake = control();
    auto* pointer = fake.get();
    pointer->state = GoPxLSdk::GoSystem::State::Conflict;
    gocator::GocatorConnection connection(config(), std::move(fake));
    QVERIFY_EXCEPTION_THROWN(connection.isRunning(), std::runtime_error);
    QCOMPARE(connection.tryStart(), gocator::GocatorStartResult::StateUnknown);
    QCOMPARE(pointer->starts, 0);
}

void GocatorConnectionStateTests::stopTimeoutRetainsOwnershipAndBlocksAttach()
{
    auto fake = control();
    auto* pointer = fake.get();
    gocator::GocatorConnection connection(config(), std::move(fake));
    QCOMPARE(connection.tryStart(), gocator::GocatorStartResult::Started);
    pointer->stopTimeout = true;
    QCOMPARE(connection.tryStop(), gocator::GocatorStopResult::Uncertain);
    QCOMPARE(pointer->stops, 1);
    pointer->stopTimeout = false;
    QCOMPARE(connection.tryStart(), gocator::GocatorStartResult::PriorStopUncertain);
    QVERIFY_EXCEPTION_THROWN(connection.attachToRunning(), std::runtime_error);
    QCOMPARE(pointer->starts, 1);
    QCOMPARE(pointer->stops, 1);
    connection.disconnect();
    QCOMPARE(pointer->stops, 1);
}

void GocatorConnectionStateTests::uncertainStopClearsOnlyAfterObservedStop()
{
    auto fake = control();
    auto* pointer = fake.get();
    gocator::GocatorConnection connection(config(), std::move(fake));
    QCOMPARE(connection.tryStart(), gocator::GocatorStartResult::Started);
    pointer->stopTimeout = true;
    QCOMPARE(connection.tryStop(), gocator::GocatorStopResult::Uncertain);
    pointer->stopTimeout = false;
    QCOMPARE(connection.tryStop(), gocator::GocatorStopResult::Uncertain);
    QCOMPARE(pointer->stops, 1);
    pointer->state = GoPxLSdk::GoSystem::State::Conflict;
    QCOMPARE(connection.tryStart(), gocator::GocatorStartResult::PriorStopUncertain);
    QCOMPARE(pointer->starts, 1);
    pointer->state = GoPxLSdk::GoSystem::State::Ready;
    QCOMPARE(connection.tryStart(), gocator::GocatorStartResult::Started);
    QCOMPARE(pointer->starts, 2);
}

void GocatorConnectionStateTests::attachNeverStarts()
{
    auto fake = control();
    auto* pointer = fake.get();
    pointer->state = GoPxLSdk::GoSystem::State::Running;
    gocator::GocatorConnection connection(config(), std::move(fake));
    connection.attachToRunning();
    QCOMPARE(pointer->starts, 0);
    pointer->state = GoPxLSdk::GoSystem::State::Ready;
    QVERIFY_EXCEPTION_THROWN(connection.attachToRunning(), std::runtime_error);
    QCOMPARE(pointer->starts, 0);
}

void GocatorConnectionStateTests::attachedReplayIsNeverStopped()
{
    auto fake = control();
    auto* pointer = fake.get();
    pointer->state = GoPxLSdk::GoSystem::State::Running;
    gocator::GocatorConnection connection(config(), std::move(fake));
    connection.attachToRunning();
    QCOMPARE(connection.tryStop(), gocator::GocatorStopResult::NotOwned);
    connection.stopNoThrow();
    connection.disconnect();
    QCOMPARE(pointer->stops, 0);
}

QTEST_APPLESS_MAIN(GocatorConnectionStateTests)
#include "GocatorConnectionStateTests.moc"
