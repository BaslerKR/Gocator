#include "Internal/GocatorOutputPreparation.h"

#include <QtTest/QtTest>
#include <stdexcept>
#include <vector>

class GocatorOutputPreparationTests final : public QObject {
    Q_OBJECT
private slots:
    void stageFailuresAndContinuation_data();
    void stageFailuresAndContinuation();
    void toleratedFailures();
};

void GocatorOutputPreparationTests::stageFailuresAndContinuation_data()
{
    QTest::addColumn<int>("failure");
    QTest::newRow("success") << -1;
    QTest::newRow("prepare") << 0;
    QTest::newRow("enable") << 1;
    QTest::newRow("register") << 2;
}

void GocatorOutputPreparationTests::stageFailuresAndContinuation()
{
    QFETCH(int, failure);
    std::vector<gocator::OutputPreparationFailure> observations;
    QStringList steps;
    const auto run = [failure, &steps](int stage, const char* name) {
        steps.append(QString::fromLatin1(name));
        if (failure == stage) throw std::runtime_error("private detail");
    };
    gocator::prepareOutput([&] { run(0, "prepare"); }, [&] { run(1, "enable"); },
        [&] { steps.append("remove"); }, [&] { run(2, "register"); },
        [&](auto stage) { observations.push_back(stage); });
    steps.append("continue"); // The caller proceeds to its existing GDP/start sequence.
    const auto expected = failure == 0 ? QStringList{"prepare", "continue"}
        : failure == 1 ? QStringList{"prepare", "enable", "continue"}
        : QStringList{"prepare", "enable", "remove", "register", "continue"};
    QCOMPARE(steps, expected);
    QCOMPARE(int(observations.size()), failure < 0 ? 0 : 1);
    if (failure >= 0) QCOMPARE(int(observations.front()), failure);
}

void GocatorOutputPreparationTests::toleratedFailures()
{
    int observed = 0;
    bool registered = false;
    gocator::prepareOutput(
        [] { try { throw std::runtime_error("unsupported scan mode"); } catch (...) {} },
        [] {}, [] { throw std::runtime_error("remove failed"); },
        [&] { registered = true; }, [&](auto) { ++observed; });
    QVERIFY(registered);
    QCOMPARE(observed, 0);
}

QTEST_APPLESS_MAIN(GocatorOutputPreparationTests)
#include "GocatorOutputPreparationTests.moc"
