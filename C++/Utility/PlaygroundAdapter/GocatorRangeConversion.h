#pragma once

/**
 * @file GocatorRangeConversion.h
 * @brief Gocator GDP range/point sample conversion used by the frame adapter.
 *
 * The functions transform Gocator's null-terminated 16-bit sensor codes into
 * GraphicsEngine range samples with the adapter's resolution/offset math. They
 * write through the caller's destination spans so the frame writer stores each
 * channel exactly once.
 */

#include <cstddef>
#include <cstdint>
#include <limits>

namespace gocator_range
{
inline constexpr std::int16_t kNullSample = (std::numeric_limits<std::int16_t>::min)();

[[nodiscard]] inline bool scalarHasInvalid(
    const std::int16_t* const samples,
    const std::size_t count) noexcept
{
    for (std::size_t index = 0U; index < count; ++index)
    {
        if (samples[index] == kNullSample)
        {
            return true;
        }
    }
    return false;
}

inline void writeScalarZ(
    const std::int16_t* const samples,
    const std::size_t count,
    const double resolution,
    const double offset,
    float* const zValues,
    std::uint8_t* const mask) noexcept
{
    for (std::size_t index = 0U; index < count; ++index)
    {
        const std::int16_t raw = samples[index];
        if (raw == kNullSample)
        {
            zValues[index] = 0.0F;
            if (mask != nullptr)
            {
                mask[index] = 0U;
            }
            continue;
        }
        zValues[index] = static_cast<float>(offset + resolution * static_cast<double>(raw));
        if (mask != nullptr)
        {
            mask[index] = 1U;
        }
    }
}

[[nodiscard]] inline bool surfacePointsHaveInvalid(
    const std::int16_t* const points,
    const std::size_t count) noexcept
{
    for (std::size_t index = 0U; index < count; ++index)
    {
        if (points[index * 3U] == kNullSample
            || points[index * 3U + 1U] == kNullSample
            || points[index * 3U + 2U] == kNullSample)
        {
            return true;
        }
    }
    return false;
}

inline void writeSurfacePoints(
    const std::int16_t* const points,
    const std::size_t count,
    const double resolutionX,
    const double resolutionY,
    const double resolutionZ,
    const double offsetX,
    const double offsetY,
    const double offsetZ,
    float* const xValues,
    float* const yValues,
    float* const zValues,
    std::uint8_t* const mask) noexcept
{
    for (std::size_t index = 0U; index < count; ++index)
    {
        const std::int16_t x = points[index * 3U];
        const std::int16_t y = points[index * 3U + 1U];
        const std::int16_t z = points[index * 3U + 2U];
        if (x == kNullSample || y == kNullSample || z == kNullSample)
        {
            xValues[index] = 0.0F;
            yValues[index] = 0.0F;
            zValues[index] = 0.0F;
            if (mask != nullptr)
            {
                mask[index] = 0U;
            }
            continue;
        }
        xValues[index] = static_cast<float>(offsetX + resolutionX * static_cast<double>(x));
        yValues[index] = static_cast<float>(offsetY + resolutionY * static_cast<double>(y));
        zValues[index] = static_cast<float>(offsetZ + resolutionZ * static_cast<double>(z));
        if (mask != nullptr)
        {
            mask[index] = 1U;
        }
    }
}

[[nodiscard]] inline bool profilePointsHaveInvalid(
    const std::int16_t* const points,
    const std::size_t count) noexcept
{
    for (std::size_t index = 0U; index < count; ++index)
    {
        if (points[index * 2U] == kNullSample || points[index * 2U + 1U] == kNullSample)
        {
            return true;
        }
    }
    return false;
}

inline void writeProfilePoints(
    const std::int16_t* const points,
    const std::size_t count,
    const double resolutionX,
    const double resolutionZ,
    const double offsetX,
    const double offsetZ,
    float* const xValues,
    float* const zValues,
    std::uint8_t* const mask) noexcept
{
    for (std::size_t index = 0U; index < count; ++index)
    {
        const std::int16_t x = points[index * 2U];
        const std::int16_t z = points[index * 2U + 1U];
        if (x == kNullSample || z == kNullSample)
        {
            xValues[index] = 0.0F;
            zValues[index] = 0.0F;
            if (mask != nullptr)
            {
                mask[index] = 0U;
            }
            continue;
        }
        xValues[index] = static_cast<float>(offsetX + resolutionX * static_cast<double>(x));
        zValues[index] = static_cast<float>(offsetZ + resolutionZ * static_cast<double>(z));
        if (mask != nullptr)
        {
            mask[index] = 1U;
        }
    }
}
}
