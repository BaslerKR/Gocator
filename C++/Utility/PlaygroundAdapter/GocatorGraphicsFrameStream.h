#pragma once

/**
 * @file GocatorGraphicsFrameStream.h
 * @brief Owns Gocator grab callbacks and publishes owned GraphicsFrame values.
 */

#include "engine/GraphicsFrameAdapter.h"
#include "engine/GraphicsFrameContext.h"
#include "engine/GraphicsImageWriter.h"

#include <atomic>
#include <functional>
#include <memory>

class Gocator;
struct GocatorRangeRejection;
struct GocatorImageRejection;

/** Owns Gocator SDK callback registration and emits only owned GraphicsFrame values. */
class GocatorGraphicsFrameStream final
{
public:
    using RejectedRange = std::function<void(const GocatorRangeRejection&, std::size_t)>;
    using RejectedImage = std::function<void(const GocatorImageRejection&, std::size_t)>;
    using StorageOutcome = std::function<void(GraphicsFrameError)>;

    GocatorGraphicsFrameStream(
        Gocator* gocator,
        std::shared_ptr<GraphicsFrameContextSource> contextSource,
        GraphicsFrameCallback callback,
        RejectedRange rejectedRange = {},
        RejectedImage rejectedImage = {},
        StorageOutcome storageOutcome = {},
        std::shared_ptr<const std::atomic<bool>> cancelled = {});
    ~GocatorGraphicsFrameStream();

    GocatorGraphicsFrameStream(const GocatorGraphicsFrameStream&) = delete;
    GocatorGraphicsFrameStream& operator=(const GocatorGraphicsFrameStream&) = delete;

    /** @brief Cancels in-flight conversion before publication without waiting for drain. */
    void cancel() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};
