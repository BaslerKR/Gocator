/**
 * @file    GoGdpSurfaceBase.h
 * @brief   Declares the GoPxLSdk.GoGdpSurfaceBase class.
 * 
 * @internal
 * Copyright (C) 2022-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef GO_PXL_SDK_GOGDPSURFACEBASE_H
#define GO_PXL_SDK_GOGDPSURFACEBASE_H

#include <GoPxLSdk/GoGdpMsg/GoGdpMsg.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpPixelFormat.h>

class GoGdpMsgTests;

namespace GoPxLSdk
{
    class GoPxLSdkClass GoGdpSurfaceBase : public GoGdpMsg
    {
    public:
       /**
        * Gets surface length (rows).
        *
        * @public                @memberof GoGdpSurfaceBase
        * @version               Introduced in 0.2.1.53.
        * @return                The surface length.
        */
        const k32u Length() const;

       /**
        * Gets surface width (columns).
        *
        * @public                @memberof GoGdpSurfaceBase
        * @version               Introduced in 0.2.1.53.
        * @return                The surface width.
        */
        const k32u Width() const;

       /**
        * Gets surface intensity length (rows).
        *
        * @public                @memberof GoGdpSurfaceBase
        * @version               Introduced in 0.2.1.53.
        * @return                The surface intensity length.
        */
        const k32u IntensityLength() const;

       /**
        * Gets surface intensity width (columns).
        *
        * @public                @memberof GoGdpSurfaceBase
        * @version               Introduced in 0.2.1.53.
        * @return                The surface intensity width.
        */
        const k32u IntensityWidth() const;

       /**
        * Gets surface intensity data.
        *
        * @public                @memberof GoGdpSurfaceBase
        * @version               Introduced in 1.5
        * @return                The surface intensity data.
        */
        const kArray2 Intensities() const;

       /**
        * Gets surface intensity pixel format.
        *
        * @public                @memberof GoGdpSurfaceBase
        * @version               Introduced in 1.5
        * @return                The intensity pixel format.
        */
        const GoGdpPixelFormat::FormatType IntensityPixelFormat() const;

       /**
        * Gets the number of bytes per row of surface intensity data.
        * 
        * @public                @memberof GoGdpSurfaceBase
        * @version               Introduced in 1.5
        * @return                The number of bytes.
        */
        const kSize IntensityRowSize() const;

       /**
        * Gets surface scale point.
        *
        * @public                @memberof GoGdpSurfaceBase
        * @version               Introduced in 0.2.1.53.
        * @return                The surface resolution.
        */
        const kPoint3d64f Resolution() const;

       /**
        * Gets surface offset point.
        *
        * @public                @memberof GoGdpSurfaceBase
        * @version               Introduced in 0.2.1.53.
        * @return                The surface offset.
        */
        const kPoint3d64f Offset() const;

       /**
        * Gets surface id value which is changed whenever surface data is changed.
        *
        * @public                @memberof GoGdpSurfaceBase
        * @version               Introduced in 0.2.1.53.
        * @return                The surface id value.
        */
        const k32u SurfaceId() const;

       /**
        * Gets surface exposure(ns).
        *
        * @public                @memberof GoGdpSurfaceBase
        * @version               Introduced in 0.2.1.53.
        * @return                The surface exposure value in nanoseconds.
        */
        const k32f Exposure() const;

    protected:
       /**
        * Constructs GoGdpSurfaceBase which forms the base class of the uniform surface and
        * surface point cloud messages.
        *
        * @public                @memberof GoGdpSurfaceBase
        * @version               Introduced in 0.2.1.53.
        * @param type            The message type.
        */
        GoGdpSurfaceBase(MessageType type);

        ~GoGdpSurfaceBase() = default;

       /**
        * Deserializes surface base message.
        *
        * @public                @memberof GoGdpSurfaceBase
        * @version               Introduced in 0.2.1.53.
        * @param serializer      The serializer to read.
        * @throws Go::Exception  If failed to deserialize surface base message.
        */
        void Deserialize(kSerializer serializer) override;

       /**
        * Deserializes optional surface intensity pixel format from message.
        *
        * @public                @memberof GoGdpSurfaceBase
        * @version               Introduced in 1.5
        * @param serializer      The serializer to read.
        * @throws Go::Exception  If failed to deserialize surface intensity pixel format from message.
        */
        void DeserializeIntensityPixelFormat(kSerializer serializer);

    protected:
        Go::Object<kArray2> intensities;

    private:
        const k64f OffsetScaled = 1000.0;
        const k64f ResolutionScaled = 1000000.0;

        k32u length = 0;
        k32u width = 0;

        k32u intensityLength = 0;
        k32u intensityWidth = 0;

        kPoint3d64f resolution = { 0.0 };
        kPoint3d64f offset = { 0.0 };

        k32u surfaceId = 0;
        k32f exposure = 0.0;

        GoGdpPixelFormat::FormatType intensityPixelFormat = GoGdpPixelFormat::FormatType::Null_Format;

        friend class ::GoGdpMsgTests;
    };
}

#endif
