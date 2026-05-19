#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <QApplication>
#include <QAbstractItemView>
#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "gocator/GocatorDiscovery.h"
#include "gocator/GocatorSettingsManager.h"

namespace
{

constexpr const char* kDefaultManualAddress = "192.168.1.10";

struct OperationResult
{
    bool ok = false;
    std::string message;
    std::string selectedAddress;
    std::vector<gocator::GocatorDeviceInfo> devices;
};

QString toQString(const std::string& value)
{
    return QString::fromStdString(value);
}

std::string scannerInfoText(const gocator::ScannerInfo& scanner)
{
    std::ostringstream out;
    out << "model=" << scanner.model << '\n'
        << "serial=" << scanner.serialNumber << '\n'
        << "engine=" << scanner.engineId << '\n'
        << "scannerPath=" << scanner.scannerPath << '\n'
        << "profileSource=" << scanner.profileSourceId;
    return out.str();
}

void appendLog(QPlainTextEdit& log, const QString& text)
{
    const QString now = QDateTime::currentDateTime().toString("HH:mm:ss");
    log.appendPlainText(QString("[%1] %2").arg(now, text));
}

void setDevices(QTableWidget& table, const std::vector<gocator::GocatorDeviceInfo>& devices)
{
    table.setRowCount(static_cast<int>(devices.size()));
    for (int row = 0; row < static_cast<int>(devices.size()); ++row)
    {
        const gocator::GocatorDeviceInfo& device = devices[static_cast<std::size_t>(row)];
        table.setItem(row, 0, new QTableWidgetItem(toQString(device.address)));
        table.setItem(row, 1, new QTableWidgetItem(toQString(device.deviceModel)));
        table.setItem(row, 2, new QTableWidgetItem(QString::number(device.serialNumber)));
        table.setItem(row, 3, new QTableWidgetItem(QString::number(device.controlPort)));
        table.setItem(row, 4, new QTableWidgetItem(device.canConnectLocally() ? "yes" : "no"));
    }
}

gocator::GocatorConnectionConfig configFromUi(const QLineEdit& address, const QSpinBox& port, const QSpinBox& timeout)
{
    return gocator::GocatorDiscovery::manualTarget(
        address.text().trimmed().toStdString(),
        static_cast<std::uint16_t>(port.value()),
        timeout.value());
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle("Gocator Debug UI");
    window.resize(980, 700);

    auto* central = new QWidget(&window);
    auto* root = new QVBoxLayout(central);

    auto* connectionBox = new QGroupBox("Connection", central);
    auto* connectionLayout = new QHBoxLayout(connectionBox);

    auto* addressEdit = new QLineEdit(kDefaultManualAddress, connectionBox);
    auto* portSpin = new QSpinBox(connectionBox);
    portSpin->setRange(1, 65535);
    portSpin->setValue(gocator::kDefaultControlPort);

    auto* timeoutSpin = new QSpinBox(connectionBox);
    timeoutSpin->setRange(100, 120000);
    timeoutSpin->setValue(gocator::kDefaultCommandTimeoutMs);
    timeoutSpin->setSingleStep(1000);

    connectionLayout->addWidget(new QLabel("IP", connectionBox));
    connectionLayout->addWidget(addressEdit, 3);
    connectionLayout->addWidget(new QLabel("Control port", connectionBox));
    connectionLayout->addWidget(portSpin, 1);
    connectionLayout->addWidget(new QLabel("Timeout ms", connectionBox));
    connectionLayout->addWidget(timeoutSpin, 1);

    auto* buttonRow = new QWidget(central);
    auto* buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);

    auto* discoverButton = new QPushButton("Discover", buttonRow);
    auto* infoButton = new QPushButton("Connect Info", buttonRow);
    auto* profileButton = new QPushButton("Profile Output", buttonRow);
    auto* clearLogButton = new QPushButton("Clear Log", buttonRow);

    buttonLayout->addWidget(discoverButton);
    buttonLayout->addWidget(infoButton);
    buttonLayout->addWidget(profileButton);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(clearLogButton);

    auto* resourceBox = new QGroupBox("Resource Read", central);
    auto* resourceLayout = new QHBoxLayout(resourceBox);
    auto* resourcePathEdit = new QLineEdit("/scan/visibleSensors/", resourceBox);
    auto* readButton = new QPushButton("Read", resourceBox);
    resourceLayout->addWidget(resourcePathEdit, 1);
    resourceLayout->addWidget(readButton);

    auto* devicesTable = new QTableWidget(0, 5, central);
    devicesTable->setHorizontalHeaderLabels({"IP", "Model", "Serial", "Port", "Local"});
    devicesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    devicesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    devicesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* logEdit = new QPlainTextEdit(central);
    logEdit->setReadOnly(true);
    logEdit->setLineWrapMode(QPlainTextEdit::NoWrap);

    root->addWidget(connectionBox);
    root->addWidget(buttonRow);
    root->addWidget(resourceBox);
    root->addWidget(new QLabel("Discovered devices", central));
    root->addWidget(devicesTable, 2);
    root->addWidget(new QLabel("Log", central));
    root->addWidget(logEdit, 3);

    window.setCentralWidget(central);

    std::vector<QPushButton*> actionButtons = {
        discoverButton,
        infoButton,
        profileButton,
        readButton,
    };

    bool cursorBusy = false;
    auto setBusy = [&](bool busy) {
        for (QPushButton* button : actionButtons)
        {
            button->setEnabled(!busy);
        }

        if (busy && !cursorBusy)
        {
            QApplication::setOverrideCursor(Qt::BusyCursor);
            cursorBusy = true;
        }
        else if (!busy && cursorBusy)
        {
            QApplication::restoreOverrideCursor();
            cursorBusy = false;
        }
    };

    auto runOperation = [&](const QString& title, std::function<OperationResult()> task) {
        appendLog(*logEdit, title + "...");
        setBusy(true);

        auto future = std::make_shared<std::future<OperationResult>>(
            std::async(std::launch::async, [task = std::move(task)] {
                try
                {
                    return task();
                }
                catch (const std::exception& e)
                {
                    return OperationResult{false, e.what(), {}, {}};
                }
                catch (...)
                {
                    return OperationResult{false, "Unknown error", {}, {}};
                }
            }));

        auto* timer = new QTimer(&window);
        QObject::connect(timer, &QTimer::timeout, &window, [&, future, timer] {
            if (future->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            {
                return;
            }

            timer->stop();
            const OperationResult result = future->get();
            if (!result.devices.empty())
            {
                setDevices(*devicesTable, result.devices);
            }
            if (!result.selectedAddress.empty())
            {
                addressEdit->setText(toQString(result.selectedAddress));
            }

            appendLog(*logEdit, QString("%1: %2")
                .arg(result.ok ? "OK" : "ERROR", toQString(result.message)));
            setBusy(false);
            timer->deleteLater();
        });
        timer->start(50);
    };

    QObject::connect(clearLogButton, &QPushButton::clicked, logEdit, &QPlainTextEdit::clear);

    QObject::connect(devicesTable, &QTableWidget::cellDoubleClicked, &window, [&](int row, int) {
        const QTableWidgetItem* addressItem = devicesTable->item(row, 0);
        const QTableWidgetItem* portItem = devicesTable->item(row, 3);
        if (addressItem != nullptr)
        {
            addressEdit->setText(addressItem->text());
        }
        if (portItem != nullptr)
        {
            portSpin->setValue(portItem->text().toInt());
        }
    });

    QObject::connect(discoverButton, &QPushButton::clicked, &window, [&] {
        const int timeoutMs = timeoutSpin->value();
        runOperation("Discover", [timeoutMs] {
            gocator::GocatorDiscoveryOptions options;
            options.timeoutMs = static_cast<std::uint64_t>(timeoutMs);

            const gocator::GocatorDiscovery discovery;
            OperationResult result;
            result.devices = discovery.discover(options);
            result.ok = !result.devices.empty();

            std::ostringstream out;
            out << "found=" << result.devices.size();
            for (const gocator::GocatorDeviceInfo& device : result.devices)
            {
                out << '\n'
                    << device.address
                    << " model=" << device.deviceModel
                    << " serial=" << device.serialNumber
                    << " port=" << device.controlPort
                    << " local=" << (device.canConnectLocally() ? "yes" : "no");

                if (result.selectedAddress.empty() && device.canConnectLocally())
                {
                    result.selectedAddress = device.address;
                }
            }

            result.message = out.str();
            return result;
        });
    });

    QObject::connect(infoButton, &QPushButton::clicked, &window, [&] {
        const gocator::GocatorConnectionConfig config = configFromUi(*addressEdit, *portSpin, *timeoutSpin);
        runOperation("Connect Info", [config] {
            gocator::GocatorSettingsManager settings(config);
            settings.connect();
            const gocator::ScannerInfo scanner = settings.detectPrimaryScanner();
            return OperationResult{true, scannerInfoText(scanner), {}, {}};
        });
    });

    QObject::connect(readButton, &QPushButton::clicked, &window, [&] {
        const gocator::GocatorConnectionConfig config = configFromUi(*addressEdit, *portSpin, *timeoutSpin);
        const std::string path = resourcePathEdit->text().trimmed().toStdString();
        runOperation("Read " + resourcePathEdit->text().trimmed(), [config, path] {
            gocator::GocatorSettingsManager settings(config);
            settings.connect();
            return OperationResult{true, settings.read(path).ToString(), {}, {}};
        });
    });

    QObject::connect(profileButton, &QPushButton::clicked, &window, [&] {
        const gocator::GocatorConnectionConfig config = configFromUi(*addressEdit, *portSpin, *timeoutSpin);
        runOperation("Profile Output", [config] {
            gocator::GocatorSettingsManager settings(config);
            settings.connect();
            const gocator::ScannerInfo scanner = settings.prepareProfileOutput();
            return OperationResult{true, "configured\n" + scannerInfoText(scanner), {}, {}};
        });
    });

    appendLog(*logEdit, "Ready");
    window.show();

    return app.exec();
}
