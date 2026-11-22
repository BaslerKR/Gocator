#pragma once

#include "Gocator.h"

#include <exception>
#include <utility>

namespace gocator {

/** @brief Runs the production output setup sequence, preserving its tolerated removal failure. */
template<class Prepare, class Enable, class Remove, class Register, class Report>
void prepareOutput(Prepare&& prepare, Enable&& enable, Remove&& remove,
                   Register&& registerOutput, Report&& report)
{
    auto stage = OutputPreparationFailure::Preparation;
    try {
        std::forward<Prepare>(prepare)();
        stage = OutputPreparationFailure::Enable;
        std::forward<Enable>(enable)();
        try { std::forward<Remove>(remove)(); } catch (...) {}
        stage = OutputPreparationFailure::Registration;
        std::forward<Register>(registerOutput)();
    } catch (const std::exception&) {
        std::forward<Report>(report)(stage);
    }
}

} // namespace gocator
