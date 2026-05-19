/**
 * @file    GoGdpSurfacePointCloud.h
 * @brief   Declares the GoPxLSdk.GoGdpSurfacePointCloud class.
 * 
 * @internal
 * Copyright (C) 2022-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef GO_PXL_SDK_GOGDPSURFACEPOINTCLOUD_H
#define GO_PXL_SDK_GOGDPSURFACEPOINTCLOUD_H

#include <GoPxLSdk/GoGdpMsg/GoGdpMsg.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpSurfaceBase.h>

class GoGdpMsgTests;

namespace GoPxLSdk
{
    class GoPxLSdkClass GoGdpSurfacePointCloud : public GoGdpSurfaceBase
    {
    public:
       /**
        * Constructs GoGdpSurfacePointCloud.
        * @public                @memberof GoGdpSurfacePointCloud
        * @version               Introduced in 0.2.1.53.
        */
        GoGdpSurfacePointCloud();
        ~GoGdpSurfacePointCloud() = default;

       /**
        * Deserializes surface point cloud message.
        *
        * @public                @memberof GoGdpSurfacePointCloud
        * @version               Introduced in 0.2.1.53.
        * @param serializer      The serializer to read.
        * @throws Go::Exception  If failed to deserialize surface point cloud message.
        */
        void Deserialize(kSerializer serializer) override;

       /**
        * Checks if the data is adjacent/sorted.
        *
        * @public                @memberof GoGdpSurfacePointCloud
        * @version               Introduced in 0.2.1.53.
        * @return                True if is adjacent.
        */
        const bool IsAdjacent() const;

       /**
        * Gets surface range data.
        *
        * @public                @memberof GoGdpSurfacePointCloud
        * @version               Introduced in 0.2.1.53.
        * @return                The surface range data.
        */
        const kArray2 Ranges() const;

    private:
        Go::Object<kArray2> ranges;
        kBool isAdjacent = 0;

        friend class ::GoGdpMsgTests;
    };
}

#endif
