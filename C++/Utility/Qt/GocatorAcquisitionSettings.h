#pragma once

#include "Gocator.h"

#include <functional>
#include <string>
#include <vector>

namespace gocator {

/** @brief Copies the scan values the widget applies before one acquisition. */
struct AcquisitionSettings {
    double scanLengthMm = 20.0;
    Gocator::ScanMode mode = Gocator::ScanMode::ProfileMode;
    int exposureUs = 1000;
    bool intensityEnabled = true;
    bool uniformSpacingEnabled = true;
};

/** @brief One explicit parameter write the acquisition start performs. */
struct AcquisitionWrite {
    Gocator::ParameterTarget target = Gocator::ParameterTarget::Scanner;
    std::string path;
    std::string jsonValue;
};

/** @brief Copies the explicit writes for @p settings in the order the widget applies them. */
std::vector<AcquisitionWrite> acquisitionWrites(const AcquisitionSettings& settings);

/** @brief Applies every explicit write and returns the first SDK refusal reason, if any.
 *  @note A refused write does not stop the remaining writes; the caller reports the reason and
 *        keeps the same acquisition admission it had before the composite was removed.
 */
std::string applyAcquisitionSettings(const AcquisitionSettings& settings,
    const std::function<Gocator::ParameterWriteResult(Gocator::ParameterTarget,
        const std::string&, const std::string&)>& write);

} // namespace gocator
