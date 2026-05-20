#ifndef QGOCATORWIDGET_H
#define QGOCATORWIDGET_H

#ifdef GOCATOR_HAS_UI
#include <QWidget>
#include <QString>
#include "gocator/Gocator.h"

class QLineEdit;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QStatusBar;

class QGocatorWidget : public QWidget
{
    Q_OBJECT
public:
    explicit QGocatorWidget(QWidget *parent = nullptr, Gocator *gocator = nullptr);
    ~QGocatorWidget() override;

    void setStatus(const QString& status);
    void setRunningState(bool running);
    
    QString ipAddress() const;
    double scanLengthMm() const;
    bool configureProfileOutput() const;

    void setIpAddress(const QString& ip);
    void setScanLengthMm(double length);
    void setConfigureProfileOutput(bool enable);

private slots:
    void onStartClicked();
    void onStopClicked();
    void handleStatusChanged(Gocator::Status status, bool on);

private:
    Gocator *_gocator = nullptr;
    Gocator::CallbackId _statusCallbackId = 0;

    QLineEdit *_ipEdit = nullptr;
    QDoubleSpinBox *_scanLengthSpin = nullptr;
    QCheckBox *_profileOutputCheck = nullptr;
    QLabel *_statusLabel = nullptr;
    QPushButton *_startButton = nullptr;
    QPushButton *_stopButton = nullptr;
    QStatusBar *_statusBar = nullptr;
};
#endif

#endif // QGOCATORWIDGET_H
