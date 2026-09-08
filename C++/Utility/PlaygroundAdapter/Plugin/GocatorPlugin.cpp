#include "DevicePlugin.h"

#include "GocatorSourceController.h"
#include "Gocator.h"
#include "Utility/Qt/QGocatorWidget.h"

#include <QPointer>

#include <memory>
#include <mutex>

namespace {

QVariantList toDiscoveryData(const std::vector<Gocator::DeviceInfo>& devices)
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(devices.size()));
    for (const Gocator::DeviceInfo& device : devices) {
        result.append(QVariantMap{
            {QStringLiteral("address"), QString::fromStdString(device.address)},
            {QStringLiteral("model"), QString::fromStdString(device.model)},
            {QStringLiteral("serial"), QString::fromStdString(device.serial)},
            {QStringLiteral("isVirtual"), device.isVirtual},
        });
    }
    return result;
}

std::vector<Gocator::DeviceInfo> fromDiscoveryData(const QVariantList& values)
{
    std::vector<Gocator::DeviceInfo> devices;
    devices.reserve(static_cast<std::size_t>(values.size()));
    for (const QVariant& value : values) {
        const QVariantMap item = value.toMap();
        devices.push_back({
            item.value(QStringLiteral("address")).toString().toStdString(),
            item.value(QStringLiteral("model")).toString().toStdString(),
            item.value(QStringLiteral("serial")).toString().toStdString(),
            item.value(QStringLiteral("isVirtual")).toBool(),
        });
    }
    return devices;
}

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
        if (_widget) _widget->prepareForShutdown();
        _controller.reset();
        if (_gocator) _gocator->close();
    }

    QString title() const override { return _titleState->title(); }
    QWidget* createControlWidget(QWidget* parent) override
    {
        if (!_widget) {
            _widget = new QGocatorWidget(parent, _gocator.get());
            _widget->setDiscoveredDevices(_devices);
        }
        return _widget;
    }
    AbstractSourceController* sourceController() const override { return _controller.get(); }
    void setTitleChangedCallback(std::function<void(const QString&)> callback) override { _titleState->setCallback(std::move(callback)); }

private:
    std::unique_ptr<Gocator> _gocator;
    std::vector<Gocator::DeviceInfo> _devices;
    std::unique_ptr<GocatorSourceController> _controller;
    QPointer<QGocatorWidget> _widget;
    Gocator::CallbackId _statusCallback = 0;
    std::shared_ptr<GocatorPluginTitleState> _titleState = std::make_shared<GocatorPluginTitleState>();
};

class GocatorPlugin final : public QObject, public IDevicePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PlaygroundDevicePlugin_iid)
    Q_INTERFACES(IDevicePlugin)

public:
    DevicePluginDescriptor descriptor() const override
    {
        return {QStringLiteral("gocator"), QStringLiteral("LMI Gocator"), playgroundDevicePluginVersion(), true};
    }
    bool discoverDevices(QVariantMap* discoveryData, QString* errorMessage) override
    {
        try {
            auto gocator = std::make_unique<Gocator>();
            const auto devices = gocator->discoverDevices();
            if (discoveryData) discoveryData->insert(QStringLiteral("devices"), toDiscoveryData(devices));
            return true;
        } catch (const std::exception& error) {
            if (errorMessage) *errorMessage = QString::fromLocal8Bit(error.what());
            return false;
        }
    }

    std::unique_ptr<IDevicePluginSession> createSession(
        const QVariantMap& discoveryData, QString* errorMessage) override
    {
        try {
            auto gocator = std::make_unique<Gocator>();
            return std::make_unique<GocatorPluginSession>(
                std::move(gocator), fromDiscoveryData(discoveryData.value(QStringLiteral("devices")).toList()));
        } catch (const std::exception& error) {
            if (errorMessage) *errorMessage = QString::fromLocal8Bit(error.what());
            return {};
        }
    }
};

#include "GocatorPlugin.moc"
