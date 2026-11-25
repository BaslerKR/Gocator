#pragma once

/**
 * @file GocatorGraphicsFrameStream.h
 * @brief Owns Gocator grab callbacks and publishes owned GraphicsFrame values.
 */

#include "engine/GraphicsFrameAdapter.h"
#include "engine/GraphicsImageWriter.h"

#include <functional>
#include <memory>

class Gocator;
struct GocatorRangeRejection;
struct GocatorImageRejection;

/** Owns Gocator SDK callback registration and emits only owned GraphicsFrame values. */
class IFrameStorageProvider;

class GocatorGraphicsFrameStream final
{
public:
    using RejectedRange = std::function<void(const GocatorRangeRejection&, std::size_t)>;
    using RejectedImage = std::function<void(const GocatorImageRejection&, std::size_t)>;
    GocatorGraphicsFrameStream(Gocator* gocator, GraphicsFrameCallback callback,
                               RejectedRange rejectedRange = {}, RejectedImage rejectedImage = {});
    ~GocatorGraphicsFrameStream();

    GocatorGraphicsFrameStream(const GocatorGraphicsFrameStream&) = delete;
    GocatorGraphicsFrameStream& operator=(const GocatorGraphicsFrameStream&) = delete;

    /** Forwards the storage range samples are written into; callable while grabbing. */
    void setFrameStorageProvider(std::shared_ptr<IFrameStorageProvider> provider);

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};
