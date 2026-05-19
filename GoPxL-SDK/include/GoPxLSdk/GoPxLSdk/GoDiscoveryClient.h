/**
 * @file    GoDiscoveryClient.h
 * @brief   Declares the GoPxLSdk.GoDiscoveryClient class.
 *
 * @internal
 * Copyright (C) 2022-2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#ifndef GO_PXL_SDK_GODISCOVERYCLIENT_H
#define GO_PXL_SDK_GODISCOVERYCLIENT_H

#include <kApi/kApi.h>
#include <GoPxLSdk/GoJson.h>
#include <GoPxLSdk/GoInstance.h>
#include <GoApi/Exception.h>

#include <GoPxLSdk/GoPxLDiscoveryProto.h>
#include <GoPxLSdk/ClassicDiscoveryProto.h>

class GoDiscoveryClientTests;

namespace GoPxLSdk 
{
    const int DISCOVERY_TIMEOUT_SWITCHING_TIME = 100;

class GoPxLSdkClass GoDiscoveryClient
{
    friend class ::GoDiscoveryClientTests;

public:
    /**
     * Constructs GoDiscoveryClient.
     * 
     * @public                          @memberof GoDiscoveryClient
     * @version                         Introduced in 0.2.1.53.
     */
    GoDiscoveryClient() = default;
    ~GoDiscoveryClient() = default;

    /**
     * Discovers all GoPxL instances on the network.
     * 
     * @public                          @memberof GoDiscoveryClient
     * @version                         Introduced in 0.2.1.53.
     * @param timeoutInMilliseconds     Time limit to listen for GoPxL instance in milliseconds.
     * @param classicDiscover           Set to true to discover classic sensors. Defaulted to false.
     * @throws Go::Exception            If failed to discover instances or if buffer size is larger than maximum message size.
     */
    void BlockingDiscover(k64u timeoutInMilliseconds, bool classicDiscover=false);

    /**
     * Returns a list of all instances found on the network.
     * 
     * @public                          @memberof GoDiscoveryClient
     * @version                         Introduced in 0.2.1.53.
     * @return                          List of GoPxL instances.
     */
    const std::vector<GoInstance>& InstanceList();
    
    /**
    * Returns a list of all GoPxL instances found on the network.
    *
    * @public                          @memberof GoDiscoveryClient
    * @version             Introduced in 1.4.43.22
    * @return                          List of GoPxL instances.
    */
    const std::vector<GoInstance>& GoPxLInstanceList();

    /**
    * Returns a list of all classic instances found on the network.
    *
    * @public                          @memberof GoDiscoveryClient
    * @version             Introduced in 1.4.43.22
    * @return                          List of classic instances.
    */
    const std::vector<GoInstance>& ClassicInstanceList();

    /**
     * Returns a single GoPxL or Classic instance that is found on the network and has the specified IP address and web port.
     * 
     * @public                          @memberof GoDiscoveryClient
     * @version             Introduced in 1.4.43.22
     * @param ipAddress                 IP address of the instance to search for.
     * @param webPort                   Web port of the instance to search for. 
     * @return                          Instance that matches IP address and web port.
     */
    const GoInstance* Instance(std::string ipAddress, k32u webPort);

    /**
    * Returns a single GoPxL instance that is found on the network and has the specified IP address and web port.
    * Both IP address and web port is required because multiple instances of GoPxl Service can share one IP Address.
    *
    * @public                          @memberof GoDiscoveryClient
    * @version             Introduced in 1.4.43.22
    * @param ipAddress                 IP address of the instance to search for.
    * @param webPort                   Web port of the instance to search for.
    * @return                          GoPxL instance that matches IP address and web port.
    */
    const GoInstance* GoPxLInstance(std::string ipAddress, k32u webPort);

    /**
     * Returns a single classic instance that was found on the network and has the serial number.
     *
     * @public                          @memberof GoDiscoveryClient
     * @version             Introduced in 1.4.43.22
     * @param serialNumber              The serial number of the instance to search for.
     * @return                          GoPxL instance that matches IP address and web port.
     */
    const GoInstance* ClassicInstance(k32u serialNumber);

    /**
    * Convert the message received from the GoPxL server into a GoInstance object and
    * save this object into the list of GoPxL instances found on the network.
    *
    * @public                          @memberof GoDiscoveryClient
    * @version             Introduced in 1.4.43.22
    * @param bytes                     Message received from sender.
    * @throws Go::Exception            If messageId or signature is incorrect.
    */
    void ParseReply(Byte bytes[]);

private:
    GoPxLDiscoveryProto goPxlProto;
    ClassicDiscoveryProto classicProto;

    std::vector<GoInstance> instances;
    
    void PopulateInstanceList();
};

}

#endif