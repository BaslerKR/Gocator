/**
 * @file    ClassicDiscoveryProto.h
 * @brief   Declares the GoPxLSdk.ClassicDiscoveryProto class for Classic Gocator discovery.
 *
 * @internal
 * Copyright (C) 2025-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef GO_PXL_SDK_CLASSICDISCOVERYPROTO_H
#define GO_PXL_SDK_CLASSICDISCOVERYPROTO_H

#include <kApi/kApi.h>
#include <GoPxLSdk/GoInstance.h>
#include <GoApi/Exception.h>

class GoDiscoveryClientTests;

namespace GoPxLSdk
{
    const kSize CLASSIC_MAX_MESSAGE_SIZE = 1536;

    /**
     * @struct  ClassicDiscoveryGetIPCommand
     * @brief   Represents the GetIP command sent to a Classic discovery server.
     */
    struct ClassicDiscoveryGetIPCommand
    {
        k64s length;
        k64s messageId;
        k64s signature;
        k64s serial;
    };

    /**
     * @struct  ClassicDiscoveryGetIPReply
     * @brief   Represents the GetIP reply received from a Classic discovery server.
     */
    struct ClassicDiscoveryGetIPReply
    {
        k64s length;
        k64s messageId;
        k64s status;
        k64s signature;
        k64s serial;
        k64s isDhcp;
        kByte reserved0[4];
        kByte address[4];
        kByte reserved1[4];
        kByte mask[4];
        kByte reserved2[4];
        kByte gateway[4];
        kByte reserved3[4];
        kByte reserved4[4];
    };

    /**
     * @struct  ClassicDiscoveryGetInfoReply
     * @brief   Represents the GetInfo reply received from a Classic discovery server broadcast.
     */
    struct ClassicDiscoveryGetInfoReply
    {
        k64s length;
        k64s messageId;
        k64s status;
        k64s signature;
        k16u attr_size;
        k32u serial;
        k32u version;
        k64u uptime;
        kByte dhcp;
        kByte addressVersion;
        kByte address[16];
        k32u prefix;
        kByte gatewayVersion;
        kByte gateway[16];
        k16u controlPort;
        k16u upgradePort;
        k16u healthPort;
        k16u dataPort;
        k16u webPort;
    };

    /**
     * @class   ClassicDiscoveryProto
     * @brief   Implements the discovery logic for the Classic Gocator protocol.
     */
    class GoPxLSdkClass ClassicDiscoveryProto
    {
        friend class ::GoDiscoveryClientTests;
    public:
        ClassicDiscoveryProto() = default;
        ~ClassicDiscoveryProto() = default;

        /**
        * Constructs the UDP client receiver for discovery messages.
        * 
        * @public   @memberof ClassicDiscoveryProto
        * @version             Introduced in 1.4.43.22
        * @param    receiver Pointer to the kUdpClient object to be used for receiving.
        *
        * @throws   If failed to construct receiver kUdpClient.
        */
        void ConstructReceiver(kUdpClient* receiver);

        /**
        * Constructs the UDP client sender for discovery messages.
        * 
        * @public  @memberof ClassicDiscoveryProto
        * @version             Introduced in 1.4.43.22
        * @param   sender  Pointer to the kUdpClient object to be used for sending.
        * @param   address The broadcast IP address to send messages to.
        *
        * @throws   If failed to construct sender kUdpClient.
        */
        void ConstructSender(kUdpClient* sender, kIpAddress address);

        /**
        * Broadcasts a GetIP command to discover Classic sensors on the network.
        * 
        * @public  @memberof ClassicDiscoveryProto
        * @version             Introduced in 1.4.43.22
        */
        void BroadcastGetIP();

        /**
        * Parses a raw byte array as a Classic GetIP reply.
        * 
        * @public  @memberof ClassicDiscoveryProto
        * @version             Introduced in 1.4.43.22
        * @param   bytes   The byte array containing the reply message.
        * @throws  If the message is invalid.
        */
        void ParseClassicGetIPReply(Byte bytes[]);

        /**
        * Parses a raw byte array as a Classic GetInfo reply.
        *
        * @public  @memberof ClassicDiscoveryProto
        * @version             Introduced in 1.4.43.22
        * @param   bytes   The byte array containing the reply message.
        * @throws  If the serializer fails. 
        */
        void ParseClassicGetInfoReply(Byte bytes[]);

        /**
        * Retrieves a pointer to a discovered classic instance by its serial number.
        * 
        * @public  @memberof ClassicDiscoveryProto
        * @version             Introduced in 1.4.43.22
        * @param   serialNumber The serial number of the classic instance to find.
        * @return  A pointer to the GoInstance object, or nullptr if not found.
        */
        GoInstance* Instance(k32u serialNumber);

        /**
        * Gets a reference to the list of discovered Classic instances.
        * 
        * @public  @memberof ClassicDiscoveryProto
        * @version             Introduced in 1.4.43.22
        * @return  A reference to a vector of classic GoInstance objects.
        */
        std::vector<GoInstance>& ClassicInstances();

    private:
        std::vector<GoInstance> classicInstances;

        void Broadcast(const void* data, k32u length);
        void SendGetInfo(const k32u serialNumber);
    };
}

#endif // GO_PXL_SDK_CLASSICDISCOVERYPROTO_H