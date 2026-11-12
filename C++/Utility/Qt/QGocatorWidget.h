#ifndef QGOCATORWIDGET_H
#define QGOCATORWIDGET_H

#ifdef GOCATOR_HAS_UI
#include "GocatorControlUi.h"
#include <QElapsedTimer>
#include <QJsonValue>
#include <QMap>
#include <QWidget>
#include <QString>
#include <QFutureWatcher>
#include <chrono>
#include <cstdint>
#include "Gocator.h"

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QToolButton;
class QTreeWidget;
class QLabel;
class QStatusBar;
class QTimer;

class QGocatorWidget : public QWidget, protected GocatorControlUi
{
    Q_OBJECT
public:
    explicit QGocatorWidget(QWidget *parent = nullptr, Gocator *gocator = nullptr);
    ~QGocatorWidget() override;

    void setStatus(const QString& status);
    void setRunningState(bool running);

    QString ipAddress() const;
    double scanLengthMm() const;
    Gocator::ScanMode scanMode() const;
    int exposureUs() const;
    bool intensityEnabled() const;
    bool uniformSpacingEnabled() const;

    void setIpAddress(const QString& ip);
    void setScanLengthMm(double length);
    void setScanMode(Gocator::ScanMode mode);
    void setExposureUs(int exposure);
    void setIntensityEnabled(bool enable);
    void setUniformSpacingEnabled(bool enable);
    void setDiscoveredDevices(const std::vector<Gocator::DeviceInfo>& devices);
    void prepareForShutdown();

signals:
    void discoveryStarted();
    void discoveryFinished();

private slots:
    void onRefreshClicked();
    void onConnectToggled(bool toggled);
    void onGrabOneClicked();
    void onGrabLiveToggled(bool toggled);
    void handleStatusChanged(Gocator::Status status, bool on);
    void onParameterChanged();

protected:
    /** @brief Returns the live producer counters; tests may override with a fake source. */
    [[nodiscard]] virtual GocatorRuntimeDiagnostics runtimeDiagnostics() const;
    /** @brief Samples the counters and refreshes the telemetry bubbles. */
    void updateDiagnostics();
    /** @brief Applies the observed grabbing state and updates the statusbar. */
    void updateGrabState(bool grabbing);

private:
    struct FeatureMapping {
        Gocator::ParameterTarget target;
        QString path; // JSON pointer path
        QString label;
    };

protected:
    /** @brief Copied resource data applied on the GUI thread, also usable by native fixtures. */
    struct FeatureDataResult {
        QString scannerData;
        QString sensorData;
        QString systemData;
        QString scannerSchema;
        QString sensorSchema;
        QString systemSchema;
        QString deviceLabel;
        bool rebuild = false;
    };

private:
    void setConnectionOperationActive(bool active);
    /** @brief Disables controls while an external lifecycle operation owns the shared gate. */
    void setExternalControlActive(bool active);
    void applyConnectionState(bool opened);

    /** Rebuilds scanner and sensor feature editors from live resource schemas. */
    void populateFeatures();
protected:
    /** @brief Queues a refresh, retaining rebuild priority while a worker is active. */
    void fetchFeatureData(bool rebuild);
    /** @brief Reports whether resource reads can start; native fixtures may supply synthetic data. */
    virtual bool featureDataAvailable() const;
    /** @brief Copies SDK resource data on the worker; never writes widgets. */
    virtual FeatureDataResult readFeatureData(bool rebuild) const;
private:
    void clearFeatures();
    void applyDiscoveredDevices(const std::vector<Gocator::DeviceInfo>& devices);

    /**
     * Adds one schema property and its editor to the feature tree.
     *
     * @param parentItem Parent tree item receiving the property.
     * @param target REST resource family receiving edits.
     * @param basePath JSON pointer prefix for the property.
     * @param name Resource property name.
     * @param propSchema Property schema.
     * @param valuesObj Current values at the property's parent object.
     */
protected:
    void addFeatureNode(class QTreeWidgetItem* parentItem, Gocator::ParameterTarget target, const QString& basePath, const QString& name, const class QJsonObject& propSchema, const class QJsonObject& valuesObj, bool inheritedReadOnly = false);

private:

    /** Refreshes current scanner and sensor values asynchronously. */
    void updateFeatureValues();

protected:
    /** Applies asynchronously fetched values to existing feature editors. */
    void applyFeatureValues(const FeatureDataResult& result);
    /** @brief Rebuilds the production tree from copied resource schemas and values. */
    void applyFeatureSnapshot(const FeatureDataResult& result);

    /** Enables editable feature controls while preserving schema read-only fields. */
    void setFeatureEditorsEnabled(bool enabled);

private:
    Gocator *_gocator = nullptr;
    Gocator::CallbackId _statusCallbackId = 0;
    bool _shuttingDown = false;
    bool _localControlActive = false;
    bool _externalControlActive = false;


    QMap<QWidget*, FeatureMapping> _widgetToFeatureMap;
    QMap<QString, class QJsonValue> _pendingParams;


    /** Routes operation diagnostics to application logs. */
    void logMessage(const QString& msg, bool isError = false);
    void updateStatusLabel();
    void setDiagnosticsVisible(bool visible);

    QFutureWatcher<bool> _connectWatcher;
    QFutureWatcher<std::vector<Gocator::DeviceInfo>> _discoverWatcher;
    QFutureWatcher<void> _paramWatcher;
    QFutureWatcher<FeatureDataResult> _featureDataWatcher;
    QTimer* _diagnosticsTimer = nullptr;
    QElapsedTimer _diagnosticsClock;
    std::chrono::steady_clock::time_point _diagnosticsSample{};
    bool _hasDiagnosticsSample = false;
    std::uint64_t _lastDelivered = 0;
    qint64 _lastDiagnosticsLogMs = 0;
    bool _updatingFeatures = false;
    bool _grabbing = false;
    bool _parameterUpdateActive = false;
    // GUI-owned until the finished slot consumes the result, even if the future is already done.
    bool _featureRefreshActive = false;
    bool _featureRefreshPending = false;
    bool _featureRebuildPending = false;
};
#endif

#endif // QGOCATORWIDGET_H
