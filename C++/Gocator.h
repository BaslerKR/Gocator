#ifndef GOCATOR_H
#define GOCATOR_H

#include <string>
#include <memory>
#include <functional>
#include <cstdint>
#include <vector>
#include <optional>
#include <stdexcept>

namespace GoPxLSdk
{
class GoDataSet;
}

/** @brief Live producer counters for the Gocator device statusbar. */
struct GocatorRuntimeDiagnostics
{
    bool grabbing = false;
    std::uint64_t delivered = 0;
    std::uint64_t failed = 0;
};

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

    struct DeviceInfo {
        std::string address;
        std::string model;
        std::string serial;
        bool isVirtual = false;
    };

    enum ScanMode {
        ProfileMode = 0,
        SurfaceMode = 1
    };

    enum class ParameterTarget {
        System,
        Scanner,
        Sensor
    };

    /** @brief Copies the official GoSystem state without confusing GDP activity with sensor state. */
    struct SystemState {
        std::uint64_t connectionGeneration = 0;
        enum class Running { Ready, Running, Conflict } running = Running::Ready;
        bool quickEditEnabled = false;
    };

    /** @brief Distinguishes pre-command observation failure from uncertain SDK mutation. */
    class SystemControlError : public std::runtime_error {
    public:
        SystemControlError(const std::string& message, bool mayHaveApplied)
            : std::runtime_error(message), mayHaveApplied(mayHaveApplied) {}
        bool mayHaveApplied;
    };

    /** @brief Calls GoSystem::RunningState and QuickEditEnabled; throws on disconnection/SDK failure. */
    SystemState systemState() const;
    /** @brief Calls EnableQuickEdit/DisableQuickEdit; query systemState() for actual readback.
     *  @note Optional expected state is checked under the same control lock before effects.
     */
    void setQuickEditEnabled(bool enabled, std::optional<SystemState> expected = {});

    using StatusCallback = std::function<void(Status status, bool on)>;
    CallbackId registerStatusCallback(StatusCallback cb);
    bool deregisterStatusCallback(CallbackId id);
    void clearStatusCallbacks();

    using GrabCallback = std::function<void(const GoPxLSdk::GoDataSet& dataSet, size_t frameSeq)>;
    CallbackId registerGrabCallback(GrabCallback cb);
    bool deregisterGrabCallback(CallbackId id);
    void clearGrabCallbacks();

    std::vector<DeviceInfo> discoverDevices();

    bool open(const std::string& ipAddress);
    bool isOpened() const;
    void close();

    void configure(double scanLengthMm, ScanMode mode, bool intensityEnabled, bool uniformSpacingEnabled, int exposureUs);

    void setScanLengthMm(double length);
    void setScanMode(ScanMode mode);
    void setExposureUs(int exposure);
    void setIntensityEnabled(bool enable);
    void setUniformSpacingEnabled(bool enable);

    /**
     * Reads the JSON schema exposed by a scanner or sensor resource.
     *
     * @param target Resource family to inspect.
     * @return JSON schema, or an empty object when the resource is unavailable.
     */
    std::string getParametersSchema(ParameterTarget target) const;

    /**
     * Reads current values exposed by a scanner or sensor resource.
     *
     * @param target Resource family to inspect.
     * @return Resource data as JSON, or an empty object when unavailable.
     */
    std::string getParametersData(ParameterTarget target) const;

    /**
     * Applies one JSON value to a scanner or sensor resource path.
     *
     * @param target Resource family receiving the patch.
     * @param path JSON pointer relative to that resource.
     * @param jsonValue Serialized JSON value.
     */
    void setParameterValue(ParameterTarget target, const std::string& path, const std::string& jsonValue);

    std::string getParametersSchema(const std::string& type) const;
    std::string getParametersData(const std::string& type) const;
    void setParameterValue(const std::string& type, const std::string& path, const std::string& jsonValue);

    void grab(size_t frames = 0);
    void stop();
    bool isGrabbing() const;
    /** @brief Returns live producer counters for the device statusbar. */
    [[nodiscard]] GocatorRuntimeDiagnostics runtimeDiagnostics() const noexcept;

    std::string getConnectedAddress() const;
    DeviceInfo getConnectedDeviceInfo() const;

    static void syslog(const std::string& message, bool warning = false);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

#endif // GOCATOR_H
