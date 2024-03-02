/*
 * Copyright 2017, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "lease.h"
#include "result.h"
#include "socket.h"

#include <netinet/in.h>
#include <stdint.h>

#include <unordered_map>
#include <vector>

class Message;

class DhcpServer {
public:
    // Construct a DHCP server. Ignore any requests and discoveries coming on
    // the network interface identified by |excludeInterface|.
    explicit DhcpServer(unsigned int excludeInterface);

    Result init();
    Result run();

private:
    Result sendMessage(unsigned int interfaceIndex,
                       in_addr_t sourceAddress,
                       const Message& message);

    void sendDhcpOffer(const Message& message, unsigned int interfaceIndex);
    void sendAck(const Message& message, unsigned int interfaceIndex);
    void sendNack(const Message& message, unsigned int interfaceIndex);

    bool isValidDhcpRequest(const Message& message,
                            unsigned int interfaceIndex);
    void updateDnsServers();
    Result getInterfaceData(unsigned int interfaceIndex,
                            unsigned long type,
                            struct ifreq* response);
    Result getInterfaceAddress(unsigned int interfaceIndex,
                               in_addr_t* address);
    Result getInterfaceNetmask(unsigned int interfaceIndex,
                               in_addr_t* netmask);
    Result getOfferAddress(unsigned int interfaceIndex,
                           const uint8_t* macAddress,
                           in_addr_t* address,
                           in_addr_t* netmask,
                           in_addr_t* gateway);

    Socket mSocket;
    // This is the next address offset. This will be added to whatever the base
    // address of the DHCP address range is. For each new MAC address seen this
    // value will increase by one.
    std::vector<in_addr_t> mDnsServers;
    // Map a lease to an IP address for that lease
    std::unordered_map<Lease, in_addr_t> mLeases;
    std::unordered_map<unsigned int, in_addr_t> mNextAddressOffsets;
    unsigned int mExcludeInterface;
};

