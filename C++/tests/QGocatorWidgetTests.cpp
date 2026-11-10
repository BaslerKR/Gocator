#include <QtTest/QtTest>

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>

#include "GocatorDiscoveryDiagnostics.h"
#include "GrabCallbackState.h"
#include "Utility/Qt/QGocatorWidget.h"

class QGocatorWidgetTests : public QObject
{
    Q_OBJECT

private slots:
    void selectedDeviceUsesDiscoveredAddress();
    void editedAddressOverridesSelectedDevice();
    void diagnosticsBubblesStartHidden();
    void diagnosticsBubblesFollowLiveAndFailureState();
    void grabCallbackStateCountsOnlyDeliveredFrames();
    void discoveryMessageClassifiesNoDevices();
    void stringFeaturesAndEscapedPaths();
    void inheritedReadOnlySurvivesRefresh();
    void officialSystemSchemaAndDisconnectedGuards();
    void integerValuesRetainSigned64BitPrecision();
    void systemTreeMatchesOfficialControls();
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

void QGocatorWidgetTests::grabCallbackStateCountsOnlyDeliveredFrames()
{
    GrabCallbackState state;
    state.reset();

    state.beginFrame();
    state.noteFailure();
    state.finishFrame();
    QCOMPARE(state.deliveredFrames(), std::uint64_t{0});
    QCOMPARE(state.failedCallbacks(), std::uint64_t{1});

    state.beginFrame();
    state.noteFailure();
    state.noteSuccess();
    state.finishFrame();
    QCOMPARE(state.deliveredFrames(), std::uint64_t{1});
    QCOMPARE(state.failedCallbacks(), std::uint64_t{2});

    state.beginFrame();
    state.finishFrame();
    QCOMPARE(state.deliveredFrames(), std::uint64_t{1});
    QCOMPARE(state.failedCallbacks(), std::uint64_t{2});

    state.reset();
    QCOMPARE(state.deliveredFrames(), std::uint64_t{0});
    QCOMPARE(state.failedCallbacks(), std::uint64_t{0});
}

void QGocatorWidgetTests::discoveryMessageClassifiesNoDevices()
{
    QVERIFY(gocator::discoveryMeansNoDevices("Failed in discovering instances"));
    QVERIFY(gocator::discoveryMeansNoDevices("Discovery failed: Failed in discovering instances."));
    QVERIFY(!gocator::discoveryMeansNoDevices("Failed to connect to the device"));
    QVERIFY(!gocator::discoveryMeansNoDevices(""));
}

namespace {
class FeatureFixture final : public QGocatorWidget {
public:
    using QGocatorWidget::addFeatureNode;
    using QGocatorWidget::setFeatureEditorsEnabled;
    void refreshScanner(const QString& json) { applyFeatureValues({json, {}, {}}); }
};
}

void QGocatorWidgetTests::stringFeaturesAndEscapedPaths()
{
    FeatureFixture widget;
    const QString name = QStringLiteral("path/~name");
    const QString initial = QStringLiteral("initial");
    widget.addFeatureNode(nullptr, Gocator::ParameterTarget::Scanner, QString(), name,
        QJsonObject{{"type", "string"}}, QJsonObject{{name, initial}});
    auto* text = widget.findChild<QLineEdit*>(QStringLiteral("gocatorFeature_") + name);
    QVERIFY(text);
    QCOMPARE(text->text(), initial);
    const QString updated = QStringLiteral("quoted \"value\" \\ path\nline");
    widget.refreshScanner(QString::fromUtf8(QJsonDocument(QJsonObject{{name, updated}}).toJson()));
    QCOMPARE(text->text(), updated);
}

void QGocatorWidgetTests::inheritedReadOnlySurvivesRefresh()
{
    FeatureFixture widget;
    widget.addFeatureNode(nullptr, Gocator::ParameterTarget::Sensor, QString(), QStringLiteral("group"),
        QJsonObject{{"type", "object"}, {"readOnly", true}, {"properties", QJsonObject{
            {"enabled", QJsonObject{{"type", "boolean"}}}}}},
        QJsonObject{{"group", QJsonObject{{"enabled", true}}}});
    auto* check = widget.findChild<QCheckBox*>(QStringLiteral("gocatorFeature_enabled"));
    QVERIFY(check);
    QVERIFY(check->isChecked());
    QVERIFY(!check->isEnabled());
    widget.setFeatureEditorsEnabled(true);
    QVERIFY(!check->isEnabled());
}

void QGocatorWidgetTests::officialSystemSchemaAndDisconnectedGuards()
{
    Gocator device;
    const auto schema = QJsonDocument::fromJson(QByteArray::fromStdString(
        device.getParametersSchema(Gocator::ParameterTarget::System))).object();
    const auto fields = schema["properties"].toObject()["parameters"].toObject()["properties"].toObject();
    QCOMPARE(fields["quickEditEnabled"].toObject()["type"].toString(), QStringLiteral("boolean"));
    QVERIFY(fields["runningState"].toObject()["readOnly"].toBool());
    QVERIFY_EXCEPTION_THROWN(device.systemState(), std::logic_error);
    QVERIFY_EXCEPTION_THROWN(device.setQuickEditEnabled(true), std::logic_error);
    QVERIFY_EXCEPTION_THROWN(device.setParameterValue(Gocator::ParameterTarget::System,
        "/parameters/runningState", "true"), std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(device.getParametersData("unknown"), std::invalid_argument);
}

void QGocatorWidgetTests::integerValuesRetainSigned64BitPrecision()
{
    FeatureFixture widget;
    constexpr qint64 first = 9007199254740993LL;
    widget.addFeatureNode(nullptr, Gocator::ParameterTarget::Scanner, QString(), QStringLiteral("counter"),
        QJsonObject{{"type", "integer"}}, QJsonObject{{"counter", QJsonValue(first)}});
    auto* text = widget.findChild<QLineEdit*>(QStringLiteral("gocatorFeature_counter"));
    QVERIFY(text);
    QCOMPARE(text->text(), QStringLiteral("9007199254740993"));
    widget.refreshScanner(QStringLiteral("{\"counter\":9223372036854775807}"));
    QCOMPARE(text->text(), QStringLiteral("9223372036854775807"));
}

void QGocatorWidgetTests::systemTreeMatchesOfficialControls()
{
    FeatureFixture widget;
    Gocator device;
    const auto schema = QJsonDocument::fromJson(QByteArray::fromStdString(
        device.getParametersSchema(Gocator::ParameterTarget::System))).object();
    const auto fields = schema["properties"].toObject()["parameters"].toObject()["properties"].toObject();
    const QJsonObject values{{"runningState", "conflict"}, {"quickEditEnabled", true}};
    for (auto it = fields.begin(); it != fields.end(); ++it)
        widget.addFeatureNode(nullptr, Gocator::ParameterTarget::System, QStringLiteral("/parameters"),
            it.key(), it.value().toObject(), values);
    auto* running = widget.findChild<QComboBox*>(QStringLiteral("gocatorFeature_runningState"));
    auto* quick = widget.findChild<QCheckBox*>(QStringLiteral("gocatorFeature_quickEditEnabled"));
    QVERIFY(running);
    QCOMPARE(running->currentData().toString(), QStringLiteral("conflict"));
    QVERIFY(!running->isEnabled());
    QVERIFY(quick);
    QVERIFY(quick->isEnabled());
    QVERIFY(quick->isChecked());
}

QTEST_MAIN(QGocatorWidgetTests)

#include "QGocatorWidgetTests.moc"
