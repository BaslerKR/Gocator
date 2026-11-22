#include "DeviceRuntime/Contract/DevicePluginSession.h"
#include "DeviceRuntime/Contract/DeviceOperationalDiagnostics.h"

#include "GocatorSourceController.h"
#include "Gocator.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace {

class GocatorPluginTitleState final {
public:
    QString title() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _title;
    }

    void setCallback(std::function<void(const QString&)> callback)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _callback = std::move(callback);
    }

    bool readConnectedAddress(Gocator* gocator, QString* title) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_active || !gocator || !title) return false;
        const QString address = QString::fromStdString(gocator->getConnectedAddress());
        if (address.isEmpty()) return false;
        *title = QStringLiteral("LMI Gocator - %1").arg(address);
        return true;
    }

    void publish(const QString& title)
    {
        std::function<void(const QString&)> callback;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_active) return;
            _title = title;
            callback = _callback;
        }
        if (callback) callback(title);
    }

    void deactivate()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _active = false;
        _callback = {};
    }

private:
    mutable std::mutex _mutex;
    bool _active = true;
    QString _title = QStringLiteral("LMI Gocator Session");
    std::function<void(const QString&)> _callback;
};

} // namespace

class GocatorPluginSession final : public IDevicePluginSession {
public:
    GocatorPluginSession(std::unique_ptr<Gocator> gocator, std::vector<Gocator::DeviceInfo> devices)
        : _gocator(std::move(gocator)), _devices(std::move(devices)),
          _controller(std::make_unique<GocatorSourceController>(_gocator.get()))
    {
        _gocator->setCachedDevices(_devices);
        std::weak_ptr<GocatorPluginTitleState> titleState = _titleState;
        Gocator* const gocatorForCallback = _gocator.get();
        _statusCallback = _gocator->registerStatusCallback([titleState, gocatorForCallback](Gocator::Status status, bool connected) {
            if (status != Gocator::ConnectionStatus || !connected || !gocatorForCallback) return;
            const auto state = titleState.lock();
            if (!state) return;

            QString title;
            if (state->readConnectedAddress(gocatorForCallback, &title)) state->publish(title);
        });
    }

    ~GocatorPluginSession() override
    {
        _titleState->deactivate();
        if (_gocator && _statusCallback != 0) _gocator->deregisterStatusCallback(_statusCallback);
        _controller.reset();
        if (_gocator) _gocator->close();
    }

    QString title() const override { return _titleState->title(); }

    AbstractSourceController* sourceController() const override { return _controller.get(); }
    unsigned int capabilities() const noexcept override
    {
        return DevicePluginSessionCapability::GraphicsEngine
            | DevicePluginSessionCapability::ScriptEditor;
    }
    void setTitleChangedCallback(std::function<void(const QString&)> callback) override { _titleState->setCallback(std::move(callback)); }

private:
    std::unique_ptr<Gocator> _gocator;
    std::vector<Gocator::DeviceInfo> _devices;
    std::unique_ptr<GocatorSourceController> _controller;
    Gocator::CallbackId _statusCallback = 0;
    std::shared_ptr<GocatorPluginTitleState> _titleState = std::make_shared<GocatorPluginTitleState>();
};

class GocatorPlugin final : public QObject, public IDeviceRuntime, public IDeviceOperationalDiagnostics {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PlaygroundDeviceRuntime_iid)
    Q_INTERFACES(IDeviceRuntime IDeviceOperationalDiagnostics)

public:
    void setOperationalDiagnosticSink(DeviceOperationalDiagnosticSink sink) noexcept override
    {
        _operationalSink.store(sink, std::memory_order_release);
    }

    DevicePluginDescriptor descriptor() const override
    {
        return {QStringLiteral("gocator"), playgroundDevicePluginDisplayName(), playgroundDevicePluginVersion(), true};
    }
    bool discoverDevices(QString* errorMessage) override
    {
        try {
            auto gocator = std::make_unique<Gocator>();
            const auto devices = gocator->discoverDevices();
            {
                std::lock_guard<std::mutex> lock(_devicesMutex);
                _devices = devices;
            }
            return true;
        } catch (const std::exception& error) {
            if (errorMessage) *errorMessage = QString::fromLocal8Bit(error.what());
            return false;
        }
    }

    std::unique_ptr<IDevicePluginSession> createSession(
        const QVariantMap& sessionOptions, QString* errorMessage) override
    {
        Q_UNUSED(sessionOptions)
        try {
            const auto sink = _operationalSink.load(std::memory_order_acquire);
            std::function<void(gocator::OutputPreparationFailure)> observer;
            if (sink) observer = [sink](gocator::OutputPreparationFailure stage) noexcept {
                const auto code = stage == gocator::OutputPreparationFailure::Preparation
                    ? DeviceOperationalDiagnostic::AcquisitionOutputPreparationFailed
                    : stage == gocator::OutputPreparationFailure::Enable
                        ? DeviceOperationalDiagnostic::AcquisitionOutputEnableFailed
                        : DeviceOperationalDiagnostic::AcquisitionOutputRegistrationFailed;
                sink(code);
            };
            auto gocator = std::make_unique<Gocator>(std::move(observer));
            std::vector<Gocator::DeviceInfo> devices;
            {
                std::lock_guard<std::mutex> lock(_devicesMutex);
                devices = _devices;
            }
            return std::make_unique<GocatorPluginSession>(std::move(gocator), std::move(devices));
        } catch (const std::exception& error) {
            if (errorMessage) *errorMessage = QString::fromLocal8Bit(error.what());
            return {};
        }
    }

private:
    std::atomic<DeviceOperationalDiagnosticSink> _operationalSink{nullptr};
    mutable std::mutex _devicesMutex;
    std::vector<Gocator::DeviceInfo> _devices;
};

#include "GocatorPlugin.moc"
