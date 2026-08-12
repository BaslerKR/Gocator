#include <QtTest/QtTest>

#include <QComboBox>
#include <QLineEdit>

#include "Utility/Qt/QGocatorWidget.h"

class QGocatorWidgetTests : public QObject
{
    Q_OBJECT

private slots:
    void selectedDeviceUsesDiscoveredAddress();
    void editedAddressOverridesSelectedDevice();
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

QTEST_MAIN(QGocatorWidgetTests)

#include "QGocatorWidgetTests.moc"
