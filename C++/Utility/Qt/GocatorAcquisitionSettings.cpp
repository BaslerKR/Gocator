#include "GocatorAcquisitionSettings.h"

#include <cstdio>

namespace gocator {
namespace {

std::string numberText(double value)
{
    // A round-trip exact decimal JSON number for GoPxL.
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return buffer;
}

} // namespace

std::vector<AcquisitionWrite> acquisitionWrites(const AcquisitionSettings& settings)
{
    const auto scanner = Gocator::ParameterTarget::Scanner;
    const auto sensor = Gocator::ParameterTarget::Sensor;
    std::vector<AcquisitionWrite> writes;
    writes.push_back({scanner, "/parameters/scanModeSettings/scanMode",
        std::to_string(settings.mode == Gocator::ScanMode::SurfaceMode ? 3 : 2)});
    writes.push_back({scanner, "/parameters/scanModeSettings/intensityEnabled",
        settings.intensityEnabled ? "true" : "false"});
    writes.push_back({scanner, "/parameters/scanModeSettings/uniformSpacingEnabled",
        settings.uniformSpacingEnabled ? "true" : "false"});
    writes.push_back({scanner, "/parameters/scanModeSettings/scanLengthMm",
        numberText(settings.scanLengthMm)});
    // The former composite forced this value; it is now an explicit write.
    writes.push_back({sensor, "/parameters/exposureSettings/exposureMode", "0"});
    writes.push_back({sensor, "/parameters/exposureSettings/singleExposure",
        std::to_string(settings.exposureUs)});
    return writes;
}

std::string applyAcquisitionSettings(const AcquisitionSettings& settings,
    const std::function<Gocator::ParameterWriteResult(Gocator::ParameterTarget,
        const std::string&, const std::string&)>& write)
{
    if (!write) return "The Gocator parameter write route is unavailable.";
    std::string firstRefusal;
    for (const auto& request : acquisitionWrites(settings)) {
        try {
            const auto result = write(request.target, request.path, request.jsonValue);
            if (!result.applied && firstRefusal.empty())
                firstRefusal = result.error.empty() ? "GoPxL refused the scan setting." : result.error;
        } catch (const std::exception& error) {
            if (firstRefusal.empty()) firstRefusal = error.what();
        } catch (...) {
            if (firstRefusal.empty()) firstRefusal = "Unexpected GoPxL write failure.";
        }
    }
    return firstRefusal;
}

} // namespace gocator
