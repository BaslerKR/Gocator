#include "QGocatorWidget.h"

#ifdef GOCATOR_HAS_UI
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QStatusBar>
#include <QPointer>
#include <QMetaObject>

QGocatorWidget::QGocatorWidget(QWidget *parent, Gocator *gocator)
    : QWidget(parent)
    , _gocator(gocator)
{
    setWindowTitle(QStringLiteral("LMI Gocator Control"));

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Group box for settings
    auto *settingsGroup = new QGroupBox(QStringLiteral("Acquisition Settings"), this);
    auto *groupLayout = new QVBoxLayout(settingsGroup);
    groupLayout->setContentsMargins(12, 12, 12, 12);
    groupLayout->setSpacing(10);

    auto *formLayout = new QFormLayout();
    formLayout->setSpacing(8);

    _ipEdit = new QLineEdit(settingsGroup);
    _ipEdit->setObjectName(QStringLiteral("gocatorIpEdit"));
    
    _scanLengthSpin = new QDoubleSpinBox(settingsGroup);
    _scanLengthSpin->setObjectName(QStringLiteral("gocatorScanLengthSpin"));
    _scanLengthSpin->setRange(0.1, 100000.0);
    _scanLengthSpin->setDecimals(3);
    _scanLengthSpin->setSuffix(QStringLiteral(" mm"));

    _profileOutputCheck = new QCheckBox(settingsGroup);
    _profileOutputCheck->setObjectName(QStringLiteral("gocatorProfileOutputCheck"));

    formLayout->addRow(QStringLiteral("IP Address"), _ipEdit);
    formLayout->addRow(QStringLiteral("Scan Length"), _scanLengthSpin);
    formLayout->addRow(QStringLiteral("Profile Output"), _profileOutputCheck);
    groupLayout->addLayout(formLayout);

    // Buttons
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);

    _startButton = new QPushButton(QStringLiteral("Start"), this);
    _startButton->setObjectName(QStringLiteral("gocatorStartButton"));
    
    _stopButton = new QPushButton(QStringLiteral("Stop"), this);
    _stopButton->setObjectName(QStringLiteral("gocatorStopButton"));
    _stopButton->setEnabled(false);

    buttonLayout->addWidget(_startButton);
    buttonLayout->addWidget(_stopButton);
    groupLayout->addLayout(buttonLayout);

    // Top padding layout container
    auto *contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(12, 12, 12, 12);
    contentLayout->addWidget(settingsGroup);
    contentLayout->addStretch();
    mainLayout->addLayout(contentLayout);

    // Status Bar
    _statusBar = new QStatusBar(this);
    _statusBar->setObjectName(QStringLiteral("GocatorStatusBar"));
    _statusBar->setSizeGripEnabled(false);
    _statusLabel = new QLabel(QStringLiteral("Disconnected"), _statusBar);
    _statusLabel->setStyleSheet(QStringLiteral("color: #a12622; font-weight: 600;"));
    _statusBar->addWidget(_statusLabel);
    
    mainLayout->addWidget(_statusBar);

    connect(_startButton, &QPushButton::clicked, this, &QGocatorWidget::onStartClicked);
    connect(_stopButton, &QPushButton::clicked, this, &QGocatorWidget::onStopClicked);

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

        // Initialize status based on current Gocator state
        if (_gocator->isGrabbing())
        {
            setStatus(QStringLiteral("Running"));
            setRunningState(true);
        }
        else if (_gocator->isOpened())
        {
            setStatus(QStringLiteral("Connected"));
            setRunningState(false);
        }
    }
}

QGocatorWidget::~QGocatorWidget()
{
    if (_gocator && _statusCallbackId != 0)
    {
        _gocator->deregisterStatusCallback(_statusCallbackId);
    }
}

void QGocatorWidget::onStartClicked()
{
    if (!_gocator) return;
    
    setStatus(QStringLiteral("Starting"));
    setRunningState(true);
    
    if (!_gocator->isOpened())
    {
        if (!_gocator->open(ipAddress().toStdString()))
        {
            setStatus(QStringLiteral("Error"));
            setRunningState(false);
            return;
        }
    }
    
    _gocator->configure(scanLengthMm(), configureProfileOutput());
    _gocator->grab();
}

void QGocatorWidget::onStopClicked()
{
    if (!_gocator) return;
    _gocator->stop();
}

void QGocatorWidget::handleStatusChanged(Gocator::Status status, bool on)
{
    if (status == Gocator::ConnectionStatus)
    {
        if (on)
        {
            setStatus(QStringLiteral("Connected"));
        }
        else
        {
            setStatus(QStringLiteral("Disconnected"));
            setRunningState(false);
        }
    }
    else if (status == Gocator::GrabbingStatus)
    {
        if (on)
        {
            setStatus(QStringLiteral("Running"));
            setRunningState(true);
        }
        else
        {
            setStatus(_gocator->isOpened() ? QStringLiteral("Connected") : QStringLiteral("Disconnected"));
            setRunningState(false);
        }
    }
}

void QGocatorWidget::setStatus(const QString& status)
{
    _statusLabel->setText(status);
    if (status == QStringLiteral("Running") || status == QStringLiteral("Connected"))
    {
        _statusLabel->setStyleSheet(QStringLiteral("color: #0a7a2f; font-weight: 600;"));
    }
    else if (status == QStringLiteral("Starting"))
    {
        _statusLabel->setStyleSheet(QStringLiteral("color: #b8860b; font-weight: 600;"));
    }
    else
    {
        _statusLabel->setStyleSheet(QStringLiteral("color: #a12622; font-weight: 600;"));
    }
}

void QGocatorWidget::setRunningState(bool running)
{
    _startButton->setEnabled(!running);
    _stopButton->setEnabled(running);
    _ipEdit->setEnabled(!running);
    _scanLengthSpin->setEnabled(!running);
    _profileOutputCheck->setEnabled(!running);
}

QString QGocatorWidget::ipAddress() const
{
    return _ipEdit->text().trimmed();
}

double QGocatorWidget::scanLengthMm() const
{
    return _scanLengthSpin->value();
}

bool QGocatorWidget::configureProfileOutput() const
{
    return _profileOutputCheck->isChecked();
}

void QGocatorWidget::setIpAddress(const QString& ip)
{
    _ipEdit->setText(ip);
}

void QGocatorWidget::setScanLengthMm(double length)
{
    _scanLengthSpin->setValue(length);
}

void QGocatorWidget::setConfigureProfileOutput(bool enable)
{
    _profileOutputCheck->setChecked(enable);
}
#endif
