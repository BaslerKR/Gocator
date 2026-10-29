#include "GocatorControlUi.h"
#include <QtWidgets>


void GocatorControlUi::create(QWidget* owner) {
    owner->setProperty("actionDensity", "compact");
    owner->setProperty("controlRole", "devicePanel");

    owner->setWindowTitle(QStringLiteral("LMI Gocator Control"));
    owner->setMinimumSize(300, 350);

    auto *mainLayout = new QVBoxLayout(owner);
    mainLayout->setObjectName(QStringLiteral("DeviceRootLayout"));
    mainLayout->setProperty("layoutRole", "flush");

    // IP selection & Control tools (Same layout style as QCameraWidget)
    _ipCombo = new QComboBox(owner);
    _ipCombo->setObjectName(QStringLiteral("gocatorIpCombo"));
    _ipCombo->setEditable(true);
    _ipCombo->setMinimumWidth(120);
    _ipCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    _toolRefresh = new QToolButton(owner);
    _toolRefresh->setObjectName(QStringLiteral("gocatorToolRefresh"));
    _toolRefresh->setIcon(QIcon(QStringLiteral(":/Resources/icon/refresh-48.png")));
    _toolRefresh->setToolButtonStyle(Qt::ToolButtonIconOnly);
    _toolRefresh->setIconSize(QSize(16, 16));

    _toolConnect = new QToolButton(owner);
    _toolConnect->setObjectName(QStringLiteral("gocatorToolConnect"));
    _toolConnect->setCheckable(true);
    _toolConnect->setToolButtonStyle(Qt::ToolButtonIconOnly);
    _toolConnect->setIconSize(QSize(16, 16));
    {
        QIcon icon;
        // The glyph names the current state; the shared theme previews the other state on hover.
        icon.addFile(QStringLiteral(":/Resources/icon/disconnected-48.png"), QSize(), QIcon::Normal, QIcon::Off);
        icon.addFile(QStringLiteral(":/Resources/icon/connect-48.png"), QSize(), QIcon::Normal, QIcon::On);
        _toolConnect->setIcon(icon);
    }

    _toolGrabOne = new QToolButton(owner);
    _toolGrabOne->setObjectName(QStringLiteral("gocatorToolGrabOne"));
    _toolGrabOne->setIcon(QIcon(QStringLiteral(":/Resources/icon/camera-48.png")));
    _toolGrabOne->setEnabled(false);
    _toolGrabOne->setToolButtonStyle(Qt::ToolButtonIconOnly);
    _toolGrabOne->setIconSize(QSize(16, 16));

    _toolGrabLive = new QToolButton(owner);
    _toolGrabLive->setObjectName(QStringLiteral("gocatorToolGrabLive"));
    _toolGrabLive->setCheckable(true);
    _toolGrabLive->setEnabled(false);
    _toolGrabLive->setToolButtonStyle(Qt::ToolButtonIconOnly);
    _toolGrabLive->setIconSize(QSize(16, 16));
    {
        QIcon icon;
        // Paused while stopped, live while grabbing.
        icon.addFile(QStringLiteral(":/Resources/icon/pause-48.png"), QSize(), QIcon::Normal, QIcon::Off);
        icon.addFile(QStringLiteral(":/Resources/icon/cameras-48.png"), QSize(), QIcon::Normal, QIcon::On);
        _toolGrabLive->setIcon(icon);
    }

    auto *ipLayout = new QHBoxLayout;
    ipLayout->setObjectName(QStringLiteral("DeviceSelectorLayout"));
    ipLayout->setProperty("layoutRole", "row");
    ipLayout->addWidget(_ipCombo);
    ipLayout->addWidget(_toolRefresh);

    auto *toolLayout = new QHBoxLayout;
    toolLayout->setObjectName(QStringLiteral("DeviceToolLayout"));
    toolLayout->setProperty("layoutRole", "actions");
    toolLayout->addWidget(_toolConnect);
    toolLayout->addWidget(_toolGrabOne);
    toolLayout->addWidget(_toolGrabLive);

    auto *topBarLayout = new QHBoxLayout;
    topBarLayout->setObjectName(QStringLiteral("DeviceTopBarLayout"));
    topBarLayout->setProperty("layoutRole", "content");
    topBarLayout->addLayout(ipLayout);
    topBarLayout->addLayout(toolLayout);

    // Param Tree (Same widget style as QCameraWidget Features Tree)
    _featuresWidget = new QTreeWidget(owner);
    _featuresWidget->setObjectName(QStringLiteral("GocatorFeaturesTree"));
    _featuresWidget->setProperty("treeRole", QStringLiteral("DeviceFeatureTree"));
    _featuresWidget->setHeaderLabels(QStringList() << QStringLiteral("Feature") << QStringLiteral("Value"));

    auto *treeLayout = new QVBoxLayout;
    treeLayout->setObjectName(QStringLiteral("DeviceTreePanelLayout"));
    treeLayout->setProperty("layoutRole", "bottom-panel");
    treeLayout->addWidget(_featuresWidget);

    mainLayout->addLayout(topBarLayout);
    mainLayout->addLayout(treeLayout);

    // Status Bar (Same style as QCameraWidget StatusBar)
    _statusBar = new QStatusBar(owner);
    _statusBar->setObjectName(QStringLiteral("GocatorStatusBar"));
    _statusBar->setSizeGripEnabled(false);

    _statusLabel = new QLabel(owner);
    _statusLabel->setObjectName(QStringLiteral("GocatorStatusLabel"));
    _statusLabel->setAlignment(Qt::AlignCenter);
    _statusBar->addWidget(_statusLabel);

    mainLayout->addWidget(_statusBar);


}
