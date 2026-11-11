#include "QGocatorWidget.h"

#ifdef GOCATOR_HAS_UI
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QScrollBar>
#include <QLabel>
#include <QLineEdit>
#include <QStatusBar>
#include <QPointer>
#include <QMetaObject>
#include <QIcon>
#include <QSize>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QDebug>
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(gocatorDiagnosticLog, "diagnostics.Gocator", QtInfoMsg)
#include <QtConcurrent>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace
{
std::string formatDeviceName(const std::string& model, const std::string& serial, const std::string& address, bool isVirtual = false)
{
    if (model.empty() || model == "Unknown")
    {
        if (isVirtual)
        {
            return "Unknown (Virtual) - " + address;
        }
        return "Unknown - " + address;
    }

    std::string displayName = model;
    bool replaced = false;
    for (const std::string& target : {"Gocator ", "gocator ", "Gocator", "gocator"})
    {
        size_t pos = displayName.find(target);
        if (pos != std::string::npos)
        {
            displayName.replace(pos, target.length(), "G");
            replaced = true;
            break;
        }
    }
    if (!replaced && !displayName.empty() && std::isdigit(static_cast<unsigned char>(displayName[0])))
    {
        displayName = "G" + displayName;
    }
    
    std::string serialStr = serial;
    if (isVirtual)
    {
        serialStr = "Virtual";
    }
    else if (serialStr.empty() || serialStr == "0")
    {
        serialStr = "0";
    }
    
    return displayName + " (" + serialStr + ") - " + address;
}


/**
 * Returns the number of decimal places needed to represent a finite value.
 *
 * @param value Numeric value from a GoPxL schema or resource.
 * @return Decimal places, capped at the precision supported by the editor.
 */
int decimalPlaces(double value)
{
    if (!std::isfinite(value)) return 0;

    QString text = QString::number(std::abs(value), 'f', 12);
    while (text.endsWith(QLatin1Char('0'))) text.chop(1);

    const int decimalPoint = text.indexOf(QLatin1Char('.'));
    if (decimalPoint < 0) return 0;
    return std::min(12, static_cast<int>(text.size() - decimalPoint - 1));
}

/**
 * Derives a floating-point editor precision from standard and GoPxL schema hints.
 *
 * @param propSchema Property schema containing bounds or increment metadata.
 * @param currentValue Current resource value.
 * @return Decimal places required to avoid rounding the resource value.
 */
int schemaDecimalPlaces(const QJsonObject& propSchema, double currentValue)
{
    int decimals = decimalPlaces(currentValue);
    for (const QString& key : {QStringLiteral("minimum"),
                               QStringLiteral("maximum"),
                               QStringLiteral("multipleOf"),
                               QStringLiteral("step"),
                               QStringLiteral("increment")})
    {
        if (propSchema.contains(key))
        {
            decimals = std::max(decimals, decimalPlaces(propSchema.value(key).toDouble()));
        }
    }

    for (const QString& key : {QStringLiteral("decimals"),
                               QStringLiteral("decimalPlaces"),
                               QStringLiteral("precision")})
    {
        if (propSchema.contains(key))
        {
            decimals = std::max(decimals, propSchema.value(key).toInt());
        }
    }

    return std::clamp(decimals, 0, 12);
}

/**
 * Selects the increment advertised by a schema, falling back to its precision.
 *
 * @param propSchema Property schema containing increment metadata.
 * @param decimals Editor precision derived from the schema.
 * @return Positive editor increment.
 */
double schemaStep(const QJsonObject& propSchema, int decimals)
{
    for (const QString& key : {QStringLiteral("multipleOf"),
                               QStringLiteral("step"),
                               QStringLiteral("increment")})
    {
        const double step = propSchema.value(key).toDouble();
        if (std::isfinite(step) && step > 0.0) return step;
    }

    return decimals > 0 ? std::pow(10.0, -decimals) : 1.0;
}
}

QGocatorWidget::QGocatorWidget(QWidget *parent, Gocator *gocator)
    : QWidget(parent)
    , _gocator(gocator)
{
    GocatorControlUi::create(this);
    _diagnosticsTimer = new QTimer(this);
    _diagnosticsTimer->setInterval(1000);
    connect(_diagnosticsTimer, &QTimer::timeout, this, &QGocatorWidget::updateDiagnostics);
    // Connections
    connect(_toolRefresh, &QToolButton::clicked, this, &QGocatorWidget::onRefreshClicked);
    connect(_toolConnect, &QToolButton::toggled, this, &QGocatorWidget::onConnectToggled);
    connect(_toolGrabOne, &QToolButton::clicked, this, &QGocatorWidget::onGrabOneClicked);
    connect(_toolGrabLive, &QToolButton::toggled, this, &QGocatorWidget::onGrabLiveToggled);

    // Async Watchers
    connect(&_connectWatcher, &QFutureWatcher<bool>::finished, this, [this]() {
        if (_shuttingDown) return;
        bool success = _connectWatcher.result();
        setConnectionOperationActive(false);
        if (success)
        {
            applyConnectionState(true);
        }
        else
        {
            setStatus(QStringLiteral("Connection Failed"));
            QSignalBlocker blocker(_toolConnect);
            _toolConnect->setChecked(false);
            applyConnectionState(false);
        }
    });

    connect(&_discoverWatcher, &QFutureWatcher<std::vector<Gocator::DeviceInfo>>::finished, this, [this]() {
        if (_shuttingDown) return;
        std::vector<Gocator::DeviceInfo> devices = _discoverWatcher.result();
        applyDiscoveredDevices(devices);
        updateStatusLabel();
        emit discoveryFinished();
    });

    connect(&_paramWatcher, &QFutureWatcher<void>::finished, this, [this]() {
        if (_shuttingDown) return;
        _parameterUpdateActive = false;
        setConnectionOperationActive(false);
        setRunningState(_grabbing);
        updateStatusLabel();
        updateFeatureValues();
    });

    connect(&_featureDataWatcher, &QFutureWatcher<FeatureDataResult>::finished, this, [this]() {
        if (_shuttingDown) return;
        if (featureDataAvailable()) {
            try {
                const auto result = _featureDataWatcher.result();
                if (result.rebuild) applyFeatureSnapshot(result);
                else applyFeatureValues(result);
            } catch (const std::exception& error) { logMessage(QString::fromUtf8(error.what()), true); }
        }
        _featureRefreshActive = false;
        const bool pending = _featureRefreshPending;
        const bool rebuild = _featureRebuildPending;
        _featureRefreshPending = _featureRebuildPending = false;
        if (pending) {
            fetchFeatureData(rebuild);
            if (_featureRefreshActive) return;
        }
        setConnectionOperationActive(false);
        setRunningState(_grabbing);
    });

    if (_gocator)
    {
        _statusCallbackId = _gocator->registerStatusCallback([guard = QPointer<QGocatorWidget>(this)](Gocator::Status status, bool on) {
            if (guard)
            {
                QMetaObject::invokeMethod(guard.data(), [guard, status, on]() {
                    if (guard)
                    {
                        guard->handleStatusChanged(status, on);
                    }
                }, Qt::QueuedConnection);
            }
        });

        // Sync initial status
        applyConnectionState(_gocator->isOpened());
        updateGrabState(_gocator->isGrabbing());
    }
}

QGocatorWidget::~QGocatorWidget()
{
    prepareForShutdown();
    clearFeatures();
}

void QGocatorWidget::prepareForShutdown()
{
    _shuttingDown = true;
    if (_diagnosticsTimer) _diagnosticsTimer->stop();
    _connectWatcher.cancel();
    _connectWatcher.waitForFinished();
    _discoverWatcher.cancel();
    _discoverWatcher.waitForFinished();
    _paramWatcher.cancel();
    _paramWatcher.waitForFinished();
    _featureDataWatcher.cancel();
    _featureDataWatcher.waitForFinished();

    if (_gocator && _statusCallbackId != 0)
    {
        _gocator->deregisterStatusCallback(_statusCallbackId);
        _statusCallbackId = 0;
    }
    _gocator = nullptr;
}

void QGocatorWidget::onRefreshClicked()
{
    if (!_gocator || _shuttingDown) return;
    if (_discoverWatcher.isRunning()) return;

    _toolRefresh->setEnabled(false);
    _toolConnect->setEnabled(false);
    setStatus(QStringLiteral("Discovering"));
    emit discoveryStarted();

    auto future = QtConcurrent::run([this]() {
        if (!_gocator) return std::vector<Gocator::DeviceInfo>();
        return _gocator->discoverDevices();
    });
    _discoverWatcher.setFuture(future);
    updateStatusLabel();
}

void QGocatorWidget::setDiscoveredDevices(const std::vector<Gocator::DeviceInfo>& devices)
{
    if (_shuttingDown) return;
    applyDiscoveredDevices(devices);
}

void QGocatorWidget::applyDiscoveredDevices(const std::vector<Gocator::DeviceInfo>& devices)
{
    QString currentAddress = ipAddress();
    _ipCombo->clear();

    if (devices.empty())
    {
        setStatus(QStringLiteral("No devices found"));
        _ipCombo->setEditText(currentAddress.isEmpty() ? QStringLiteral("192.168.1.10") : currentAddress);
        _toolRefresh->setEnabled(true);
        _toolConnect->setEnabled(true);
        return;
    }

    for (const auto& device : devices)
    {
        QString text = QString::fromStdString(formatDeviceName(device.model, device.serial, device.address, device.isVirtual));
        _ipCombo->addItem(text, QString::fromStdString(device.address));
    }

    if (!currentAddress.isEmpty())
    {
        int index = _ipCombo->findData(currentAddress);
        if (index >= 0) _ipCombo->setCurrentIndex(index);
        else _ipCombo->setEditText(currentAddress);
    }

    setStatus(QStringLiteral("Discovered %1 devices").arg(devices.size()));
    _toolRefresh->setEnabled(true);
    _toolConnect->setEnabled(true);
}

void QGocatorWidget::onConnectToggled(bool toggled)
{
    if (!_gocator || _shuttingDown) return;

    setConnectionOperationActive(true);
    if (toggled)
    {
        setStatus(QStringLiteral("Connecting"));
        std::string ip = ipAddress().toStdString();
        auto future = QtConcurrent::run([this, ip]() {
            if (!_gocator) return false;
            return _gocator->open(ip);
        });
        _connectWatcher.setFuture(future);
        updateStatusLabel();
    }
    else
    {
        auto future = QtConcurrent::run([this]() {
            if (_gocator) _gocator->close();
            return false;
        });
        _connectWatcher.setFuture(future);
        updateStatusLabel();
    }
}

void QGocatorWidget::onGrabOneClicked()
{
    if (!_gocator || _shuttingDown) return;
    logMessage(tr("Starting single grab..."), false);
    _gocator->configure(scanLengthMm(), scanMode(), intensityEnabled(), uniformSpacingEnabled(), exposureUs());
    _gocator->grab(1);
}

void QGocatorWidget::onGrabLiveToggled(bool toggled)
{
    if (!_gocator || _shuttingDown) return;

    if (toggled)
    {
        logMessage(tr("Starting live grab..."), false);
        _gocator->configure(scanLengthMm(), scanMode(), intensityEnabled(), uniformSpacingEnabled(), exposureUs());
        _gocator->grab();
    }
    else
    {
        logMessage(tr("Stopping grab..."), false);
        _gocator->stop();
    }
}

void QGocatorWidget::handleStatusChanged(Gocator::Status status, bool on)
{
    if (_shuttingDown) return;

    if (status == Gocator::GrabbingStatus)
    {
        updateGrabState(on);
        setRunningState(on);
        logMessage(on ? tr("Grabbing started.") : tr("Grabbing stopped."));
        if (!on && gocatorDiagnosticLog().isDebugEnabled()) updateFeatureValues();

        QSignalBlocker blocker(_toolGrabLive);
        _toolGrabLive->setChecked(on); updateLiveAction();
    }
    else if (status == Gocator::ConnectionStatus)
    {
        applyConnectionState(on);
        QSignalBlocker blocker(_toolConnect);
        _toolConnect->setChecked(on);
    }
}

void QGocatorWidget::setConnectionOperationActive(bool active)
{
    if (active)
    {
        _ipCombo->setEnabled(false);
        _toolRefresh->setEnabled(false);
        _toolConnect->setEnabled(false);
        updateStatusLabel();
        return;
    }

    const bool opened = _gocator && _gocator->isOpened();
    _ipCombo->setEnabled(!opened);
    _toolRefresh->setEnabled(!opened);
    _toolConnect->setEnabled(true);
    updateStatusLabel();
}

void QGocatorWidget::applyConnectionState(bool opened)
{
    if (_shuttingDown) return;

    if (opened)
    {

    }

    _ipCombo->setEnabled(!opened);
    _toolRefresh->setEnabled(!opened);
    _toolConnect->setEnabled(true);
    _toolGrabOne->setEnabled(opened);
    _toolGrabLive->setEnabled(opened);

    updateStatusLabel();

    if (opened)
    {
        logMessage(tr("Gocator connected successfully."), false);
        setRunningState(false);
        populateFeatures();

        if (_gocator)
        {
            Gocator::DeviceInfo devInfo = _gocator->getConnectedDeviceInfo();
            if (!devInfo.address.empty())
            {
                QString ip = QString::fromStdString(devInfo.address);
                QString text = QString::fromStdString(formatDeviceName(devInfo.model, devInfo.serial, devInfo.address, devInfo.isVirtual));

                int index = _ipCombo->findData(ip);
                if (index >= 0)
                {
                    _ipCombo->setItemText(index, text);
                    _ipCombo->setCurrentIndex(index);
                }
                else
                {
                    _ipCombo->addItem(text, ip);
                    int newIndex = _ipCombo->findData(ip);
                    if (newIndex >= 0) _ipCombo->setCurrentIndex(newIndex);
                }
            }
        }
    }
    else
    {
        {
            QSignalBlocker blocker(_toolGrabLive);
            _toolGrabLive->setChecked(false); updateLiveAction();
        }
        updateGrabState(false);
        setRunningState(false);
        clearFeatures();
    }
}

void QGocatorWidget::setStatus(const QString& status)
{
    logMessage(status);
}

void QGocatorWidget::logMessage(const QString& message, bool error)
{
    if (message.isEmpty()) return;
    if (error) qWarning().noquote() << "[Gocator UI]" << message;
    else qInfo().noquote() << "[Gocator UI]" << message;
}

void QGocatorWidget::updateGrabState(bool grabbing)
{
    const bool wasGrabbing = _grabbing;
    _grabbing = grabbing;
    if (grabbing && !wasGrabbing) {
        _lastDelivered = 0;
        _hasDiagnosticsSample = false;
        _lastDiagnosticsLogMs = 0;
        _diagnosticsClock.start();
        setDiagnosticsVisible(true);
        _diagnosticsTimer->start();
    } else if (!grabbing && wasGrabbing) {
        _diagnosticsTimer->stop();
        _hasDiagnosticsSample = false;
        setDiagnosticsVisible(false);
    }
    updateStatusLabel();
}

void QGocatorWidget::setDiagnosticsVisible(const bool visible)
{
    if (visible) {
        if (_failedLabel) {
            _failedLabel->hide();
            _failedLabel->setText(QStringLiteral("0 failed"));
            _failedLabel->setProperty("failedState", QStringLiteral("normal"));
        }
        if (_fpsLabel) {
            _fpsLabel->setText(QStringLiteral("0.0 fps"));
            _fpsLabel->show();
        }
    } else {
        if (_failedLabel) _failedLabel->hide();
        if (_fpsLabel) _fpsLabel->hide();
    }
}

GocatorRuntimeDiagnostics QGocatorWidget::runtimeDiagnostics() const
{
    return _gocator ? _gocator->runtimeDiagnostics() : GocatorRuntimeDiagnostics{};
}

void QGocatorWidget::updateDiagnostics()
{
    if (_shuttingDown || !_grabbing) return;
    const auto now = std::chrono::steady_clock::now();
    const auto diagnostics = runtimeDiagnostics();
    double fps = 0.0;
    if (_hasDiagnosticsSample) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - _diagnosticsSample).count();
        if (elapsed > 0 && diagnostics.delivered >= _lastDelivered) {
            fps = static_cast<double>(diagnostics.delivered - _lastDelivered)
                * 1.0e9 / static_cast<double>(elapsed);
        }
    }
    _lastDelivered = diagnostics.delivered;
    _diagnosticsSample = now;
    _hasDiagnosticsSample = true;

    if (_failedLabel) {
        const bool showFailed = diagnostics.failed > 0;
        if (showFailed) {
            _failedLabel->setText(QStringLiteral("%1 failed").arg(
                static_cast<qulonglong>(diagnostics.failed)));
            if (_failedLabel->property("failedState").toString()
                != QStringLiteral("warning")) {
                _failedLabel->setProperty("failedState", QStringLiteral("warning"));
                _failedLabel->style()->unpolish(_failedLabel);
                _failedLabel->style()->polish(_failedLabel);
            }
        }
        _failedLabel->setVisible(showFailed);
    }
    if (_fpsLabel) {
        _fpsLabel->setText(QStringLiteral("%1 fps").arg(fps, 0, 'f', 1));
    }

    const qint64 elapsed = _diagnosticsClock.elapsed();
    if (elapsed - _lastDiagnosticsLogMs >= 5000) {
        const QString camera = _gocator
            ? QString::fromStdString(_gocator->getConnectedAddress())
            : QStringLiteral("camera");
        qInfo().noquote() << QStringLiteral(
            "[Gocator UI] diagnostics: camera=%1 live=%2 failed=%3 fps=%4")
            .arg(camera)
            .arg(diagnostics.grabbing ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(static_cast<qulonglong>(diagnostics.failed))
            .arg(fps, 0, 'f', 1);
        _lastDiagnosticsLogMs = elapsed;
    }
}

void QGocatorWidget::updateStatusLabel()
{
    if (!_statusLabel || _shuttingDown) return;

    const bool opened = _gocator && _gocator->isOpened();
    _statusLabel->setText(!opened ? QStringLiteral("Idle")
        : _grabbing ? QStringLiteral("Live") : QStringLiteral("Connected"));
}

void QGocatorWidget::setRunningState(bool running)
{
    _toolGrabOne->setEnabled(_gocator && _gocator->isOpened() && !running);
    _toolGrabLive->setEnabled(_gocator && _gocator->isOpened());

    setFeatureEditorsEnabled(!running && !_parameterUpdateActive && !_featureRefreshActive);
}

QString QGocatorWidget::ipAddress() const
{
    const QString currentText = _ipCombo->currentText().trimmed();
    const int currentIndex = _ipCombo->currentIndex();
    if (currentIndex >= 0
        && currentText == _ipCombo->itemText(currentIndex).trimmed())
    {
        const QString selectedAddress = _ipCombo->itemData(currentIndex).toString().trimmed();
        if (!selectedAddress.isEmpty())
        {
            return selectedAddress;
        }
    }

    return currentText;
}

double QGocatorWidget::scanLengthMm() const
{
    QString path = QStringLiteral("/parameters/scanModeSettings/scanLengthMm");
    if (_pendingParams.contains(path)) return _pendingParams[path].toDouble();
    for (auto it = _widgetToFeatureMap.begin(); it != _widgetToFeatureMap.end(); ++it)
    {
        if (it.value().path == path)
        {
            if (auto* spin = qobject_cast<QDoubleSpinBox*>(it.key())) return spin->value();
        }
    }
    return 20.0;
}

Gocator::ScanMode QGocatorWidget::scanMode() const
{
    QString path = QStringLiteral("/parameters/scanModeSettings/scanMode");
    int val = 2; // Default Profile
    if (_pendingParams.contains(path)) val = _pendingParams[path].toInt();
    else
    {
        for (auto it = _widgetToFeatureMap.begin(); it != _widgetToFeatureMap.end(); ++it)
        {
            if (it.value().path == path)
            {
                if (auto* combo = qobject_cast<QComboBox*>(it.key())) val = combo->currentData().toInt();
            }
        }
    }
    return (val == 3) ? Gocator::ScanMode::SurfaceMode : Gocator::ScanMode::ProfileMode;
}

int QGocatorWidget::exposureUs() const
{
    QString path = QStringLiteral("/parameters/exposureSettings/singleExposure");
    if (_pendingParams.contains(path)) return _pendingParams[path].toInt();
    for (auto it = _widgetToFeatureMap.begin(); it != _widgetToFeatureMap.end(); ++it)
    {
        if (it.value().path == path)
        {
            if (auto* spin = qobject_cast<QSpinBox*>(it.key())) return spin->value();
        }
    }
    return 1000;
}

bool QGocatorWidget::intensityEnabled() const
{
    QString path = QStringLiteral("/parameters/scanModeSettings/intensityEnabled");
    if (_pendingParams.contains(path)) return _pendingParams[path].toBool();
    for (auto it = _widgetToFeatureMap.begin(); it != _widgetToFeatureMap.end(); ++it)
    {
        if (it.value().path == path)
        {
            if (auto* check = qobject_cast<QCheckBox*>(it.key())) return check->isChecked();
        }
    }
    return true;
}

bool QGocatorWidget::uniformSpacingEnabled() const
{
    QString path = QStringLiteral("/parameters/scanModeSettings/uniformSpacingEnabled");
    if (_pendingParams.contains(path)) return _pendingParams[path].toBool();
    for (auto it = _widgetToFeatureMap.begin(); it != _widgetToFeatureMap.end(); ++it)
    {
        if (it.value().path == path)
        {
            if (auto* check = qobject_cast<QCheckBox*>(it.key())) return check->isChecked();
        }
    }
    return true;
}

void QGocatorWidget::setIpAddress(const QString& ip)
{
    int index = _ipCombo->findData(ip);
    if (index >= 0)
    {
        _ipCombo->setCurrentIndex(index);
    }
    else
    {
        _ipCombo->setCurrentText(ip);
    }
}

void QGocatorWidget::setScanLengthMm(double length)
{
    QString path = QStringLiteral("/parameters/scanModeSettings/scanLengthMm");
    _pendingParams[path] = length;
    if (_gocator && _gocator->isOpened())
    {
        _gocator->setScanLengthMm(length);
    }
    populateFeatures();
}

void QGocatorWidget::setScanMode(Gocator::ScanMode mode)
{
    int val = (mode == Gocator::ScanMode::SurfaceMode) ? 3 : 2;
    QString path = QStringLiteral("/parameters/scanModeSettings/scanMode");
    _pendingParams[path] = val;
    if (_gocator && _gocator->isOpened())
    {
        _gocator->setScanMode(mode);
    }
    populateFeatures();
}

void QGocatorWidget::setExposureUs(int exposure)
{
    QString path = QStringLiteral("/parameters/exposureSettings/singleExposure");
    _pendingParams[path] = exposure;
    if (_gocator && _gocator->isOpened())
    {
        _gocator->setExposureUs(exposure);
    }
    populateFeatures();
}

void QGocatorWidget::setIntensityEnabled(bool enable)
{
    QString path = QStringLiteral("/parameters/scanModeSettings/intensityEnabled");
    _pendingParams[path] = enable;
    if (_gocator && _gocator->isOpened())
    {
        _gocator->setIntensityEnabled(enable);
    }
    populateFeatures();
}

void QGocatorWidget::setUniformSpacingEnabled(bool enable)
{
    QString path = QStringLiteral("/parameters/scanModeSettings/uniformSpacingEnabled");
    _pendingParams[path] = enable;
    if (_gocator && _gocator->isOpened())
    {
        _gocator->setUniformSpacingEnabled(enable);
    }
    populateFeatures();
}

void QGocatorWidget::clearFeatures()
{
    _widgetToFeatureMap.clear();
    _featuresWidget->clear();
}

void QGocatorWidget::populateFeatures()
{
    fetchFeatureData(true);
}

void QGocatorWidget::applyFeatureSnapshot(const FeatureDataResult& result)
{
    _updatingFeatures = true;
    clearFeatures();
    const QJsonObject scannerSchema = QJsonDocument::fromJson(result.scannerSchema.toUtf8()).object();
    const QJsonObject scannerData = QJsonDocument::fromJson(result.scannerData.toUtf8()).object();
    const QJsonObject sensorSchema = QJsonDocument::fromJson(result.sensorSchema.toUtf8()).object();
    const QJsonObject sensorData = QJsonDocument::fromJson(result.sensorData.toUtf8()).object();
    const QString rootText = result.deviceLabel;

    QTreeWidgetItem* rootItem = new QTreeWidgetItem(_featuresWidget, QStringList() << rootText);

    const auto addResourceCategory = [this, rootItem](const QString& label,
                                                       Gocator::ParameterTarget target,
                                                       const QJsonObject& resourceSchema,
                                                       const QJsonObject& resourceData) {
        const QJsonObject resourceProperties = resourceSchema.value(QStringLiteral("properties")).toObject();
        if (resourceProperties.isEmpty()) return;

        QTreeWidgetItem* category = new QTreeWidgetItem(rootItem, QStringList() << label);
        const QJsonObject parameterSchema = resourceProperties.value(QStringLiteral("parameters")).toObject();
        if (!parameterSchema.isEmpty())
        {
            const QJsonObject parameterValues = resourceData.value(QStringLiteral("parameters")).toObject();
            const QJsonObject parameterProperties = parameterSchema.value(QStringLiteral("properties")).toObject();
            for (auto it = parameterProperties.begin(); it != parameterProperties.end(); ++it)
            {
                addFeatureNode(category, target, QStringLiteral("/parameters"), it.key(), it.value().toObject(), parameterValues,
                    resourceSchema.value(QStringLiteral("readOnly")).toBool() || parameterSchema.value(QStringLiteral("readOnly")).toBool());
            }

            // Some GoPxL versions expose motion/alignment beside the
            // /parameters envelope on the scanner resource.
            for (auto it = resourceProperties.begin(); it != resourceProperties.end(); ++it)
            {
                if (it.key() == QStringLiteral("parameters")) continue;
                addFeatureNode(category, target, QString(), it.key(), it.value().toObject(), resourceData, resourceSchema.value(QStringLiteral("readOnly")).toBool());
            }
        }
        else
        {
            // Firmware variants without a parameters envelope expose their
            // writable values at the resource root.
            for (auto it = resourceProperties.begin(); it != resourceProperties.end(); ++it)
            {
                addFeatureNode(category, target, QString(), it.key(), it.value().toObject(), resourceData, resourceSchema.value(QStringLiteral("readOnly")).toBool());
            }
        }
        category->setExpanded(false);
    };

    addResourceCategory(QStringLiteral("System"), Gocator::ParameterTarget::System,
            QJsonDocument::fromJson(result.systemSchema.toUtf8()).object(),
            QJsonDocument::fromJson(result.systemData.toUtf8()).object());

    addResourceCategory(QStringLiteral("Scanner"), Gocator::ParameterTarget::Scanner, scannerSchema, scannerData);
    addResourceCategory(QStringLiteral("Sensor"), Gocator::ParameterTarget::Sensor, sensorSchema, sensorData);

    rootItem->setExpanded(true);
    _updatingFeatures = false;
}

void QGocatorWidget::addFeatureNode(QTreeWidgetItem* parentItem, Gocator::ParameterTarget target, const QString& basePath, const QString& name, const QJsonObject& propSchema, const QJsonObject& valuesObj, bool inheritedReadOnly)
{
    QString propType = propSchema.value(QStringLiteral("type")).toString();
    QString token = name;
    token.replace(QStringLiteral("~"), QStringLiteral("~0")).replace(QStringLiteral("/"), QStringLiteral("~1"));
    const QString path = basePath + "/" + token;
    const bool readOnly = inheritedReadOnly || propSchema.value(QStringLiteral("readOnly")).toBool(false);
    
    // Use 'label' or 'title' for user-friendly name if available, otherwise fallback to the key 'name'
    QString displayName = propSchema.value(QStringLiteral("label")).toString();
    if (displayName.isEmpty()) displayName = propSchema.value(QStringLiteral("title")).toString();
    if (displayName.isEmpty()) displayName = name;

    if (propType == QStringLiteral("object"))
    {
        QJsonObject subProperties = propSchema.value(QStringLiteral("properties")).toObject();
        if (subProperties.isEmpty()) return;

        QTreeWidgetItem* groupItem = nullptr;
        if (parentItem)
        {
            groupItem = new QTreeWidgetItem(parentItem, QStringList() << displayName);
        }
        else
        {
            groupItem = new QTreeWidgetItem(_featuresWidget, QStringList() << displayName);
        }

        QJsonObject subValues = valuesObj.value(name).toObject();
        for (auto it = subProperties.begin(); it != subProperties.end(); ++it)
        {
            addFeatureNode(groupItem, target, path, it.key(), it.value().toObject(), subValues, readOnly);
        }
        groupItem->setExpanded(false);
    }
    else
    {
        QTreeWidgetItem* item = nullptr;
        if (parentItem)
        {
            item = new QTreeWidgetItem(parentItem, QStringList() << displayName);
        }
        else
        {
            item = new QTreeWidgetItem(_featuresWidget, QStringList() << displayName);
        }

        QWidget* editorWidget = nullptr;
        QJsonValue curVal = valuesObj.value(name);

        if (_pendingParams.contains(path))
        {
            curVal = _pendingParams.value(path);
        }

        if (propType == QStringLiteral("boolean"))
        {
            QCheckBox* check = new QCheckBox(_featuresWidget);
            check->setChecked(curVal.toBool());
            connect(check, &QCheckBox::checkStateChanged, this, &QGocatorWidget::onParameterChanged);
            editorWidget = check;
        }
        else if (propType == QStringLiteral("integer") || propType == QStringLiteral("number"))
        {
            if (propSchema.contains(QStringLiteral("enum")))
            {
                QComboBox* combo = new QComboBox(_featuresWidget);
                QJsonArray enumVals = propSchema.value(QStringLiteral("enum")).toArray();
                QJsonArray enumTexts = propSchema.value(QStringLiteral("enumText")).toArray();
                int activeIndex = 0;
                for (int i = 0; i < enumVals.size(); ++i)
                {
                    QJsonValue enumV = enumVals.at(i);
                    QString displayText = (i < enumTexts.size()) ? enumTexts.at(i).toString() : QString();
                    if (enumV.isDouble())
                    {
                        if (displayText.isEmpty()) displayText = enumV.toVariant().toString();
                        combo->addItem(displayText, enumV.toVariant());
                        if (curVal == enumV) activeIndex = i;
                    }
                    else
                    {
                        if (displayText.isEmpty()) displayText = enumV.toString();
                        combo->addItem(displayText, enumV.toString());
                        if (curVal.toString() == enumV.toString()) activeIndex = i;
                    }
                }
                combo->setCurrentIndex(activeIndex);
                connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QGocatorWidget::onParameterChanged);
                editorWidget = combo;
            }
            else
            {
                if (propType == QStringLiteral("integer"))
                {
                    const qint64 minimum = propSchema.value(QStringLiteral("minimum")).toInteger(std::numeric_limits<qint64>::min());
                    const qint64 maximum = propSchema.value(QStringLiteral("maximum")).toInteger(std::numeric_limits<qint64>::max());
                    if (minimum >= std::numeric_limits<int>::min() && maximum <= std::numeric_limits<int>::max()) {
                        auto* spin = new QSpinBox(_featuresWidget);
                        spin->setRange(static_cast<int>(minimum), static_cast<int>(maximum));
                        spin->setValue(curVal.toInt());
                        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &QGocatorWidget::onParameterChanged);
                        editorWidget = spin;
                    } else {
                        auto* text = new QLineEdit(QString::number(curVal.toInteger()), _featuresWidget);
                        text->setProperty("gocatorInteger", true);
                        text->setProperty("gocatorMinimum", minimum);
                        text->setProperty("gocatorMaximum", maximum);
                        connect(text, &QLineEdit::editingFinished, this, &QGocatorWidget::onParameterChanged);
                        editorWidget = text;
                    }
                }
                else // number
                {
                    QDoubleSpinBox* spin = new QDoubleSpinBox(_featuresWidget);
                    const double currentValue = curVal.toDouble(0.0);
                    const int decimals = schemaDecimalPlaces(propSchema, currentValue);
                    spin->setDecimals(decimals);
                    spin->setRange(propSchema.value(QStringLiteral("minimum")).toDouble(-std::numeric_limits<double>::max()),
                        propSchema.value(QStringLiteral("maximum")).toDouble(std::numeric_limits<double>::max()));
                    spin->setValue(currentValue);
                    spin->setSingleStep(schemaStep(propSchema, decimals));
                    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &QGocatorWidget::onParameterChanged);
                    editorWidget = spin;
                }
            }
        }
        else if (propType == QStringLiteral("string"))
        {
            if (propSchema.contains(QStringLiteral("enum")))
            {
                QComboBox* combo = new QComboBox(_featuresWidget);
                QJsonArray enumVals = propSchema.value(QStringLiteral("enum")).toArray();
                QJsonArray enumTexts = propSchema.value(QStringLiteral("enumText")).toArray();
                int activeIndex = 0;
                for (int i = 0; i < enumVals.size(); ++i)
                {
                    QString enumValue = enumVals.at(i).toString();
                    QString displayText = (i < enumTexts.size()) ? enumTexts.at(i).toString() : enumValue;
                    combo->addItem(displayText, enumValue);
                    if (curVal.toString() == enumValue) activeIndex = i;
                }
                combo->setCurrentIndex(activeIndex);
                connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QGocatorWidget::onParameterChanged);
                editorWidget = combo;
            }
            else
            {
                auto* text = new QLineEdit(curVal.toString(), _featuresWidget);
                connect(text, &QLineEdit::editingFinished, this, &QGocatorWidget::onParameterChanged);
                editorWidget = text;
            }
        }

        if (editorWidget)
        {
            editorWidget->setObjectName(QStringLiteral("gocatorFeature_") + name);
            editorWidget->setProperty("gocatorReadOnly", readOnly);
            const bool observed = !curVal.isUndefined() && !curVal.isNull();
            editorWidget->setProperty("gocatorObserved", observed);
            editorWidget->setEnabled(observed && !readOnly);
            if (!observed) {
                editorWidget->setToolTip(tr("Value not observed."));
                if (auto* check = qobject_cast<QCheckBox*>(editorWidget)) {
                    check->setTristate(true);
                    check->setCheckState(Qt::PartiallyChecked);
                } else if (auto* combo = qobject_cast<QComboBox*>(editorWidget)) combo->setCurrentIndex(-1);
                else if (auto* spin = qobject_cast<QAbstractSpinBox*>(editorWidget)) spin->clear();
                else if (auto* text = qobject_cast<QLineEdit*>(editorWidget)) text->clear();
            }
            _featuresWidget->setItemWidget(item, 1, editorWidget);
            _widgetToFeatureMap.insert(editorWidget, FeatureMapping{target, path, displayName});
        }
    }
}

void QGocatorWidget::onParameterChanged()
{
    if (_updatingFeatures) return;

    QWidget* senderWidget = qobject_cast<QWidget*>(sender());
    if (!senderWidget || !_gocator || _shuttingDown) return;

    auto it = _widgetToFeatureMap.find(senderWidget);
    if (it == _widgetToFeatureMap.end()) return;

    const FeatureMapping& mapping = it.value();

    QJsonValue jsonVal;
    if (auto* check = qobject_cast<QCheckBox*>(senderWidget))
    {
        jsonVal = check->isChecked();
    }
    else if (auto* spin = qobject_cast<QSpinBox*>(senderWidget))
    {
        jsonVal = spin->value();
    }
    else if (auto* dspin = qobject_cast<QDoubleSpinBox*>(senderWidget))
    {
        jsonVal = dspin->value();
    }
    else if (auto* combo = qobject_cast<QComboBox*>(senderWidget))
    {
        jsonVal = QJsonValue::fromVariant(combo->currentData());
    }
    else if (auto* text = qobject_cast<QLineEdit*>(senderWidget))
    {
        if (text->property("gocatorInteger").toBool()) {
            bool valid = false;
            const auto number = text->text().toLongLong(&valid);
            if (!valid || number < text->property("gocatorMinimum").toLongLong()
                || number > text->property("gocatorMaximum").toLongLong()) {
                logMessage(tr("Integer value is outside the schema bounds."), true);
                return;
            }
            jsonVal = QJsonValue(number);
        } else jsonVal = text->text();
    }

    if (!jsonVal.isNull())
    {
        if (_paramWatcher.isRunning())
        {
            logMessage(tr("Parameter update already in progress."), false);
            return;
        }

        // JSON serialization must preserve quotes, backslashes, control characters and Unicode.
        QByteArray encoded = QJsonDocument(QJsonArray{jsonVal}).toJson(QJsonDocument::Compact);
        const QString jsonValueStr = QString::fromUtf8(encoded.mid(1, encoded.size() - 2));

        Gocator::ParameterTarget target = mapping.target;
        std::string path = mapping.path.toStdString();
        std::string valStr = jsonValueStr.toStdString();

        logMessage(tr("Updating parameter '%1' to %2...").arg(mapping.label, jsonValueStr),
                          false);

        Gocator* gocator = _gocator;
        auto future = QtConcurrent::run([gocator, target, path, valStr]() {
            if (gocator)
            {
                try { gocator->setParameterValue(target, path, valStr); }
                catch (const std::exception& error) {
                    qCWarning(gocatorDiagnosticLog) << "Parameter write failed:" << error.what();
                    return;
                }
                // The core reports SDK failures; this record confirms dispatch, not readback acceptance.
                qCDebug(gocatorDiagnosticLog).noquote() << "@diagnostic " + QString::fromUtf8(QJsonDocument(QJsonObject{
                    {"event", "parameter_dispatched"}, {"fields", QJsonObject{
                    {"target", static_cast<int>(target)}, {"path", QString::fromStdString(path)},
                    {"requestedValue", QString::fromStdString(valStr)}, {"verification", "follow-up-resource-read"}}}}).toJson(QJsonDocument::Compact));
            }
        });
        _parameterUpdateActive = true;
        setConnectionOperationActive(true);
        _toolGrabOne->setEnabled(false);
        _toolGrabLive->setEnabled(false);
        setFeatureEditorsEnabled(false);
        updateStatusLabel();
        _paramWatcher.setFuture(future);
    }
}

void QGocatorWidget::updateFeatureValues()
{
    fetchFeatureData(false);
}

bool QGocatorWidget::featureDataAvailable() const
{
    return _gocator && _gocator->isOpened();
}

QGocatorWidget::FeatureDataResult QGocatorWidget::readFeatureData(bool rebuild) const
{
    FeatureDataResult result{
        QString::fromStdString(_gocator->getParametersData(Gocator::ParameterTarget::Scanner)),
        QString::fromStdString(_gocator->getParametersData(Gocator::ParameterTarget::Sensor)), {}};
    try { result.systemData = QString::fromStdString(_gocator->getParametersData(Gocator::ParameterTarget::System)); }
    catch (const std::exception& error) { qCWarning(gocatorDiagnosticLog) << "System observation failed:" << error.what(); }
    result.rebuild = rebuild;
    if (rebuild) {
        result.scannerSchema = QString::fromStdString(_gocator->getParametersSchema(Gocator::ParameterTarget::Scanner));
        result.sensorSchema = QString::fromStdString(_gocator->getParametersSchema(Gocator::ParameterTarget::Sensor));
        result.systemSchema = QString::fromStdString(_gocator->getParametersSchema(Gocator::ParameterTarget::System));
        const auto info = _gocator->getConnectedDeviceInfo();
        result.deviceLabel = QString::fromStdString(info.address.empty() ? _gocator->getConnectedAddress()
            : formatDeviceName(info.model, info.serial, info.address, info.isVirtual));
    }
    return result;
}

void QGocatorWidget::fetchFeatureData(bool rebuild)
{
    if (_shuttingDown || !featureDataAvailable()) return;
    if (_featureRefreshActive) {
        _featureRefreshPending = true;
        _featureRebuildPending |= rebuild;
        return;
    }
    _featureRefreshActive = true;
    auto future = QtConcurrent::run([this, rebuild] { return readFeatureData(rebuild); });
    setConnectionOperationActive(true);
    _toolGrabOne->setEnabled(false);
    _toolGrabLive->setEnabled(false);
    setFeatureEditorsEnabled(false);
    _featureDataWatcher.setFuture(future);
}

void QGocatorWidget::applyFeatureValues(const FeatureDataResult& result)
{
    QJsonObject scannerData = QJsonDocument::fromJson(result.scannerData.toUtf8()).object();
    QJsonObject sensorData = QJsonDocument::fromJson(result.sensorData.toUtf8()).object();
    if (gocatorDiagnosticLog().isDebugEnabled())
        qCDebug(gocatorDiagnosticLog).noquote() << "@diagnostic " + QString::fromUtf8(QJsonDocument(QJsonObject{
            {"event", "feature_snapshot"}, {"fields", QJsonObject{{"coverage", "scanner-and-sensor-resources"},
            {"scanner", scannerData}, {"sensor", sensorData},
            {"complete", !scannerData.isEmpty() && !sensorData.isEmpty()}}}}).toJson(QJsonDocument::Compact));

    const QJsonObject systemData = QJsonDocument::fromJson(result.systemData.toUtf8()).object();

    _updatingFeatures = true;

    for (auto it = _widgetToFeatureMap.begin(); it != _widgetToFeatureMap.end(); ++it)
    {
        QWidget* widget = it.key();
        const FeatureMapping& mapping = it.value();

        if (!widget) continue;

        QJsonValue targetVal = mapping.target == Gocator::ParameterTarget::System ? systemData
            : mapping.target == Gocator::ParameterTarget::Scanner ? scannerData : sensorData;
        const auto segments = mapping.path.mid(1).split(QStringLiteral("/"));
        for (QString token : segments) {
            token.replace(QStringLiteral("~1"), QStringLiteral("/")).replace(QStringLiteral("~0"), QStringLiteral("~"));
            targetVal = targetVal.toObject().value(token);
        }

        if (targetVal.isUndefined() || targetVal.isNull())
        {
            continue;
        }

        QSignalBlocker blocker(widget);
        widget->setProperty("gocatorObserved", true);
        widget->setToolTip(QString());

        if (auto* check = qobject_cast<QCheckBox*>(widget))
        {
            check->setTristate(false);
            check->setChecked(targetVal.toBool());
        }
        else if (auto* spin = qobject_cast<QSpinBox*>(widget))
        {
            spin->setValue(targetVal.toInt());
        }
        else if (auto* dspin = qobject_cast<QDoubleSpinBox*>(widget))
        {
            dspin->setValue(targetVal.toDouble());
        }
        else if (auto* text = qobject_cast<QLineEdit*>(widget))
        {
            text->setText(text->property("gocatorInteger").toBool()
                ? QString::number(targetVal.toInteger()) : targetVal.toString());
        }
        else if (auto* combo = qobject_cast<QComboBox*>(widget))
        {
            const int index = combo->findData(targetVal.toVariant());
            if (index >= 0)
            {
                combo->setCurrentIndex(index);
            }
        }
    }

    _updatingFeatures = false;
}

void QGocatorWidget::setFeatureEditorsEnabled(bool enabled)
{
    for (auto it = _widgetToFeatureMap.begin(); it != _widgetToFeatureMap.end(); ++it)
    {
        if (it.key())
        {
            const bool readOnly = it.key()->property("gocatorReadOnly").toBool();
            it.key()->setEnabled(enabled && !readOnly && it.key()->property("gocatorObserved").toBool());
        }
    }
}
#endif
