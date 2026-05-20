#ifndef GOCATOR_H
#define GOCATOR_H

#include <string>
#include <memory>
#include <functional>

namespace GoPxLSdk
{
class GoDataSet;
}

class Gocator
{
public:
    using CallbackId = size_t;

    Gocator();
    ~Gocator();

    enum Status {
        GrabbingStatus,
        ConnectionStatus
    };

    using StatusCallback = std::function<void(Status status, bool on)>;
    CallbackId registerStatusCallback(StatusCallback cb);
    bool deregisterStatusCallback(CallbackId id);
    void clearStatusCallbacks();

    using GrabCallback = std::function<void(const GoPxLSdk::GoDataSet& dataSet, size_t frameSeq)>;
    CallbackId registerGrabCallback(GrabCallback cb);
    bool deregisterGrabCallback(CallbackId id);
    void clearGrabCallbacks();

    bool open(const std::string& ipAddress);
    bool isOpened() const;
    void close();

    void configure(double scanLengthMm, bool configureProfileOutput);

    void grab(size_t frames = 0);
    void stop();
    bool isGrabbing() const;

    std::string getConnectedAddress() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

#endif // GOCATOR_H
