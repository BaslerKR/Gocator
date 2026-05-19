/**
 * @file    GoGdpString.h
 * @brief   Declares the GoPxLSdk.GoGdpString class.
 * 
 * @internal
 * Copyright (C) 2024-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#pragma once

#include <GoPxLSdk/GoGdpMsg/GoGdpMsg.h>

class GoGdpMsgTests;

namespace GoPxLSdk
{

    class GoPxLSdkClass GoGdpString : public GoGdpMsg
    {
    public:
        /**
         * Constructs GoGdpString.
         * 
         * @public                @memberof GoGdpString
         * @version               Introduced in 1.3.7.26
         */
        GoGdpString();
        ~GoGdpString() = default;

        /**
         * Deserializes string message.
         * 
         * @public                @memberof GoGdpString
         * @version               Introduced in 1.3.7.26
         * @param serializer      The serializer to read.
         * @throws Go::Exception  If failed to deserialize string message.
         */
        void Deserialize(kSerializer serializer) override;

        /**
         * Gets the string contained in the message.
         *
         * @public                @memberof GoGdpString
         * @version               Introduced in 1.3.7.26
         * @return                The string.
         */
        const std::string& String() const;

        /**
         * Gets the decision value stored in the string message.
         *
         * @public                @memberof GoGdpString
         * @version               Introduced in 1.5.
         * @return                The decision value.
         */
        const k8u Decision() const;

        /**
         * Get position coordinates.
         *
         * @public                @memberof GoGdpString
         * @version               Introduced in 1.5.
         * @return                The position coordinates.
         */
        const kPoint3d64f LabelPosition() const;

    private:
        std::string str;
        k8u decision = 0;
        kPoint3d64f labelPosition;

        friend class ::GoGdpMsgTests;
    };
}
