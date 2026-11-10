#include <QtTest/QtTest>

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>

#include "Utility/Qt/QGocatorWidget.h"

class QGocatorWidgetTests : public QObject
{
    Q_OBJECT

private slots:
    void selectedDeviceUsesDiscoveredAddress();
    void editedAddressOverridesSelectedDevice();
    void diagnosticsBubblesStartHidden();
    void diagnosticsBubblesFollowLiveAndFailureState();
};

namespace
{
std::vector<Gocator::DeviceInfo> discoveredDevices()
{
    return {
        {"192.168.1.10", "Gocator 2500", "12345", false},
        {"192.168.1.11", "Gocator 2600", "67890", false},
    };
}
}

void QGocatorWidgetTests::selectedDeviceUsesDiscoveredAddress()
{
    QGocatorWidget widget;
    widget.setDiscoveredDevices(discoveredDevices());

    auto* combo = widget.findChild<QComboBox*>(QStringLiteral("gocatorIpCombo"));
    QVERIFY(combo);
    QCOMPARE(combo->currentIndex(), 0);
    QCOMPARE(widget.ipAddress(), QStringLiteral("192.168.1.10"));

    combo->setCurrentIndex(1);
    QCOMPARE(widget.ipAddress(), QStringLiteral("192.168.1.11"));
}

void QGocatorWidgetTests::editedAddressOverridesSelectedDevice()
{
    QGocatorWidget widget;
    widget.setDiscoveredDevices(discoveredDevices());

    auto* combo = widget.findChild<QComboBox*>(QStringLiteral("gocatorIpCombo"));
    QVERIFY(combo);
    QVERIFY(combo->lineEdit());
    QCOMPARE(combo->currentData().toString(), QStringLiteral("192.168.1.10"));

    combo->lineEdit()->setText(QStringLiteral("10.20.30.40"));

    QCOMPARE(combo->currentData().toString(), QStringLiteral("192.168.1.10"));
    QCOMPARE(widget.ipAddress(), QStringLiteral("10.20.30.40"));
}

void QGocatorWidgetTests::diagnosticsBubblesStartHidden()
{
    QGocatorWidget widget;
    auto* failed = widget.findChild<QLabel*>(QStringLiteral("GocatorStatusFailed"));
    auto* fps = widget.findChild<QLabel*>(QStringLiteral("GocatorStatusFps"));
    QVERIFY(failed);
    QVERIFY(fps);
    QVERIFY(failed->isHidden());
    QVERIFY(fps->isHidden());
}

namespace
{
class DiagnosticsWidget final : public QGocatorWidget
{
public:
    using QGocatorWidget::QGocatorWidget;
    QLabel* failedBubble() const { return _failedLabel; }
    QLabel* fpsBubble() const { return _fpsLabel; }
    void setFakeDiagnostics(const GocatorRuntimeDiagnostics& diagnostics) { _fake = diagnostics; }
    void setGrabbing(bool grabbing) { updateGrabState(grabbing); }
    void refreshDiagnostics() { updateDiagnostics(); }

protected:
    GocatorRuntimeDiagnostics runtimeDiagnostics() const override { return _fake; }

private:
    GocatorRuntimeDiagnostics _fake;
};
}

void QGocatorWidgetTests::diagnosticsBubblesFollowLiveAndFailureState()
{
    DiagnosticsWidget widget;
    widget.setGrabbing(true);
    widget.setFakeDiagnostics({true, 0, 0});
    widget.refreshDiagnostics();
    QVERIFY(!widget.fpsBubble()->isHidden());
    QVERIFY(widget.failedBubble()->isHidden());

    widget.setFakeDiagnostics({true, 8, 2});
    widget.refreshDiagnostics();
    QVERIFY(!widget.fpsBubble()->isHidden());
    QVERIFY(!widget.failedBubble()->isHidden());
    QCOMPARE(widget.failedBubble()->property("failedState").toString(),
             QStringLiteral("warning"));

    widget.setFakeDiagnostics({true, 16, 0});
    widget.refreshDiagnostics();
    QVERIFY(widget.failedBubble()->isHidden());

    widget.setGrabbing(false);
    QVERIFY(widget.fpsBubble()->isHidden());
    QVERIFY(widget.failedBubble()->isHidden());
}

QTEST_MAIN(QGocatorWidgetTests)

#include "QGocatorWidgetTests.moc"
