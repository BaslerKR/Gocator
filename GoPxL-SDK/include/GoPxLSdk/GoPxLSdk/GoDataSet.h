/**
 * @file    GoDataSet.h
 * @brief   Declares the GoPxLSdk.GoDataSet class.
 *
 * @internal
 * Copyright (C) 2022-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef GO_PXL_SDK_GOGDPDATASET_H
#define GO_PXL_SDK_GOGDPDATASET_H

#include <GoPxLSdk/GoGdpMsg/GoGdpMsgDef.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpMsg.h>

namespace GoPxLSdk
{
    class GoGdpClient;

    class GoPxLSdkClass GoDataSet
    {
    public:
        /**
        * Clear the vector of msgs.
        *
        * @public                @memberof GoDataSet
        * @version               Introduced in 0.2.1.53.
        */
        void Clear();

        /**
        * Add a msg to vector.
        *
        * @public                @memberof GoDataSet
        * @version               Introduced in 0.2.1.53.
        * @param pGoGdpMsg       The pointer to concrete msg.
        */
        void Add(std::shared_ptr<GoGdpMsg> pGoGdpMsg);

        /**
        * Get a msg from vector.
        *
        * @public                @memberof GoDataSet
        * @version               Introduced in 0.2.1.53.
        * @param index           The index of vector.
        * @return                A msg pointer of content.
        */
        const GoGdpMsg& GdpMsgAt(size_t index) const;

        /**
        * Get a msg size of vector.
        *
        * @public                @memberof GoDataSet
        * @version               Introduced in 0.2.1.53.
        * @return                The size of content.
        */
        const size_t Count() const;

        /**
        * Get the GoGdpClient that sent this dataset.
        *
        * This allows identification of which sensor/connection a dataset originated from
        * when receiving data from multiple sensors simultaneously.
        *
        * @public                @memberof GoDataSet
        * @version             Introduced in 1.4.43.22
        * @return                The GoGdpClient that sent this dataset.
        */
        const GoGdpClient* Sender() const;

        /**
        * Set the GoGdpClient that sent this dataset.
        *
        * @public                @memberof GoDataSet
        * @version             Introduced in 1.4.43.22
        * @param sender          The GoGdpClient that sent this dataset.
        */
        void SetSender(GoGdpClient* sender);

    private:
        std::vector<std::shared_ptr<GoGdpMsg>> content;
        GoGdpClient* sender = nullptr;
    };
}

#endif




