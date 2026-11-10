#include "GocatorSourceController.h"
#include <QUuid>
#include "engine/GraphicsFrame.h"
#include "SessionFrame.h"
#include "Utility/PlaygroundAdapter/GocatorGraphicsFrameStream.h"

GocatorSourceController::GocatorSourceController(Gocator* gocator, QObject* parent)
    : AbstractSourceController(parent), _gocator(gocator) {
    _parameterIdentity = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    _readSystem = [gocator] { return gocator->systemState(); };
    _writeQuickEdit = [gocator](bool enabled, std::optional<Gocator::SystemState> expected) {
        gocator->setQuickEditEnabled(enabled, expected);
    };
    _systemConnected = [gocator] { return gocator && gocator->isOpened(); };
    registerCallbacks();
}

GocatorSourceController::~GocatorSourceController() {
    _parameterWork.waitForFinished();
    stop();
    deregisterCallbacks();
}

void GocatorSourceController::start() {
    if (_gocator) {
        _gocator->grab();
    }
}

void GocatorSourceController::stop() {
    if (_gocator) {
        _gocator->stop();
        _isGrabbing = false;
    }
}

bool GocatorSourceController::isGrabbing() const {
    return _isGrabbing;
}

void GocatorSourceController::setFrameConsumer(FrameConsumer consumer) {
    _frameConsumer = std::move(consumer);
}

void GocatorSourceController::registerCallbacks() {
    if (!_gocator) return;

    // 1. Status callback
    _statusCallbackId = _gocator->registerStatusCallback([this](Gocator::Status status, bool on) {
        if (status == Gocator::GrabbingStatus) {
            _isGrabbing = on;
            emit diagnosticAcquisitionChanged(on, QStringLiteral("gocator"));
        }
    });

    _graphicsStream = std::make_unique<GocatorGraphicsFrameStream>(
        _gocator,
        [this](GraphicsFrame&& payload, const unsigned int sourceIndex) {
            if (!_frameConsumer) return;
            SessionFrame frame;
            frame.payload = std::move(payload);
            frame.frameSeq = _frameSeq.fetch_add(1, std::memory_order_relaxed);
            _frameConsumer(std::move(frame), sourceIndex);
        });
}

void GocatorSourceController::deregisterCallbacks() {
    if (!_gocator) return;

    _graphicsStream.reset();

    if (_statusCallbackId != 0) {
        _gocator->deregisterStatusCallback(_statusCallbackId);
        _statusCallbackId = 0;
    }
}

std::optional<SourceRuntimeDiagnostics> GocatorSourceController::sourceRuntimeDiagnostics(unsigned sourceIndex) const
{
    if (sourceIndex != 0 || !_gocator) return std::nullopt;
    const auto value = _gocator->runtimeDiagnostics();
    return SourceRuntimeDiagnostics{value.grabbing, value.delivered, value.failed};
}
