#pragma once

/**
 * @file GocatorGraphicsFrameStream.h
 * @brief Owns Gocator grab callbacks and publishes owned GraphicsFrame values.
 */

#include "engine/GraphicsFrameAdapter.h"

#include <memory>

class Gocator;

/** Owns Gocator SDK callback registration and emits only owned GraphicsFrame values. */
class GocatorGraphicsFrameStream final
{
public:
    GocatorGraphicsFrameStream(Gocator* gocator, GraphicsFrameCallback callback);
    ~GocatorGraphicsFrameStream();

    GocatorGraphicsFrameStream(const GocatorGraphicsFrameStream&) = delete;
    GocatorGraphicsFrameStream& operator=(const GocatorGraphicsFrameStream&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};
