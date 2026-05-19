/**
 * @file    GoPxLDiscoveryProto.h
 * @brief   Declares the GoPxLSdk.GoPxLDiscoveryProto class for GoPxL discovery.
 *
 * @internal
 * Copyright (C) 2025-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef GO_PXL_SDK_GOPXLDISCOVERYPROTO_H
#define GO_PXL_SDK_GOPXLDISCOVERYPROTO_H

#include <kApi/kApi.h>
#include <GoPxLSdk/GoJson.h>
#include <GoPxLSdk/GoInstance.h>
#include <GoApi/Exception.h>

class GoDiscoveryClientTests;

namespace GoPxLSdk
{    
    const kSize MAX_MESSAGE_SIZE = 1536;

    /**
     * @struct  DiscoveryBroadcastHeader
     * @brief   Defines the header for a GoPxL discovery broadcast request.
     */
    struct DiscoveryBroadcastHeader
    {
        k64u length;
        k64u messageId;
        k64u signature;
    };

    /**
     * @struct  DiscoveryServerHeader
     * @brief   Defines the header for a reply from a GoPxL discovery server.
     */
    struct DiscoveryServerHeader
    {
        k64u length;
        k64u messageId;
        k64u signature;
        k64u messageStatus;
    };

    /**
     * @class   GoPxLDiscoveryProto
     * @brief   Implements the discovery logic for the GoPxL protocol.
     */
    class GoPxLSdkClass GoPxLDiscoveryProto
    {
        friend class ::GoDiscoveryClientTests;

    public:
        GoPxLDiscoveryProto() = default;
        ~GoPxLDiscoveryProto() = default;

        /**
        * Constructs the UDP client receiver for discovery messages.
        * 
        * @public  @memberof    GoPxLDiscoveryProto
        * @version             Introduced in 1.4.43.22
        * @param   receiver     Pointer to the kUdpClient object to be used for receiving.
        * @throws  If failed to construct receiver kUdpClient.
        */
        void ConstructReceiver(kUdpClient* receiver);

        /**
        * Constructs the UDP client sender for discovery messages.
        * 
        * @public  @memberof    GoPxLDiscoveryProto
        * @version             Introduced in 1.4.43.22
        * @param   sender       Pointer to the kUdpClient object to be used for sending.
        * @param   address      The broadcast IP address to send messages to.
        * @param   port         The destination port for the discovery message.
        * @throws  If failed to construct sender kUdpClient.
        */
        void ConstructSender(kUdpClient* sender, kIpAddress address, k32u port);

        /**
        * Broadcasts a command to discover GoPxL instances on the network.
        *
        * @public  @memberof    GoPxLDiscoveryProto
        * @version             Introduced in 1.4.43.22
        * @throws  If failed to send broadcast message.
        */
        void Broadcast();

        /**
         * Parses a raw byte array as a GoPxL discovery server reply.
         * 
         * @public  @memberof    GoPxLDiscoveryProto
         * @version             Introduced in 1.4.43.22
         * @param   bytes   The byte array containing the reply message.
         */
        void ParseReply(Byte bytes[]);

        /**
        * Retrieves a pointer to a discovered GoPxL instance by its IP address and web port.
        * 
        * @public  @memberof    GoPxLDiscoveryProto
        * @version             Introduced in 1.4.43.22
        * @param   ipAddress    The IP address of the GoPxL instance to find.
        * @param   webPort      The web port of the GoPxL instance to find.
        * 
        * @return  A const pointer to a GoPxL GoInstance object, or nullptr if not found.
        */
        const GoInstance* Instance(std::string ipAddress, k32u webPort);

        /**
        * Gets a reference to the list of discovered GoPxL instances.
        * 
        * @public  @memberof    GoPxLDiscoveryProto
        * @version             Introduced in 1.4.43.22
        * @return  A reference to a vector of GoInstance objects.
         */
        std::vector<GoInstance>& GoPxlInstances();

    private:
        std::vector<GoInstance> goPxlInstances;
    };
}

#endif // GO_PXL_SDK_GOPXLDISCOVERYPROTO_H