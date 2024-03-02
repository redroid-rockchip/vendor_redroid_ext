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

#include "dhcpserver.h"

#include "dhcp.h"
#include "log.h"
#include "message.h"

#include <arpa/inet.h>
#include <errno.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cutils/properties.h>

static const int kMaxDnsServers = 4;

DhcpServer::DhcpServer(unsigned int excludeInterface) :
    mExcludeInterface(excludeInterface)
{
}

Result DhcpServer::init() {
    Result res = mSocket.open(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (!res) {
        return res;
    }
    res = mSocket.enableOption(SOL_IP, IP_PKTINFO);
    if (!res) {
        return res;
    }
    res = mSocket.enableOption(SOL_SOCKET, SO_BROADCAST);
    if (!res) {
        return res;
    }

    res = mSocket.bindIp(INADDR_ANY, PORT_BOOTP_SERVER);
    if (!res) {
        return res;
    }

    return Result::success();
}

Result DhcpServer::run() {
    // Block all signals while we're running. This way we don't have to deal
    // with things like EINTR. We then uses ppoll to set the original mask while
    // polling. This way polling can be interrupted but socket writing, reading
    // and ioctl remain interrupt free. If a signal arrives while we're blocking
    // it it will be placed in the signal queue and handled once ppoll sets the
    // original mask. This way no signals are lost.
    sigset_t blockMask, originalMask;
    int status = ::sigfillset(&blockMask);
    if (status != 0) {
        return Result::error("Unable to fill signal set: %s", strerror(errno));
    }
    status = ::sigprocmask(SIG_SETMASK, &blockMask, &originalMask);
    if (status != 0) {
        return Result::error("Unable to set signal mask: %s", strerror(errno));
    }

    struct pollfd fds;
    fds.fd = mSocket.get();
    fds.events = POLLIN;
    Message message;
    while ((status = ::ppoll(&fds, 1, nullptr, &originalMask)) >= 0) {
        if (status == 0) {
            // Timeout
            continue;
        }

        unsigned int interfaceIndex = 0;
        Result res = mSocket.receiveFromInterface(&message,
                                                  &interfaceIndex);
        if (!res) {
            ALOGE("Failed to recieve on socket: %s", res.c_str());
            continue;
        }
        if (interfaceIndex == 0 || mExcludeInterface == interfaceIndex) {
            // Received packet on unknown or unwanted interface, drop it
            continue;
        }
        if (!message.isValidDhcpMessage(OP_BOOTREQUEST)) {
            // Not a DHCP request, drop it
            continue;
        }
        switch (message.type()) {
            case DHCPDISCOVER:
                // Someone is trying to find us, let them know we exist
                sendDhcpOffer(message, interfaceIndex);
                break;
            case DHCPREQUEST:
                // Someone wants a lease based on an offer
                if (isValidDhcpRequest(message, interfaceIndex)) {
                    // The request matches our offer, acknowledge it
                    sendAck(message, interfaceIndex);
                } else {
                    // Request for something other than we offered, denied
                    sendNack(message, interfaceIndex);
                }
                break;
        }
    }
    // Polling failed, exit
    return Result::error("Polling failed: %s", strerror(errno));
}

Result DhcpServer::sendMessage(unsigned int interfaceIndex,
                               in_addr_t /*sourceAddress*/,
                               const Message& message) {
    return mSocket.sendOnInterface(interfaceIndex,
                                   INADDR_BROADCAST,
                                   PORT_BOOTP_CLIENT,
                                   message);
}

void DhcpServer::sendDhcpOffer(const Message& message,
                               unsigned int interfaceIndex ) {
    updateDnsServers();
    in_addr_t offerAddress;
    in_addr_t netmask;
    in_addr_t gateway;
    Result res = getOfferAddress(interfaceIndex,
                                 message.dhcpData.chaddr,
                                 &offerAddress,
                                 &netmask,
                                 &gateway);
    if (!res) {
        ALOGE("Failed to get address for offer: %s", res.c_str());
        return;
    }
    in_addr_t serverAddress;
    res = getInterfaceAddress(interfaceIndex, &serverAddress);
    if (!res) {
        ALOGE("Failed to get address for interface %u: %s",
              interfaceIndex, res.c_str());
        return;
    }

    Message offer = Message::offer(message,
                                   serverAddress,
                                   offerAddress,
                                   netmask,
                                   gateway,
                                   mDnsServers.data(),
                                   mDnsServers.size());
    res = sendMessage(interfaceIndex, serverAddress, offer);
    if (!res) {
        ALOGE("Failed to send DHCP offer: %s", res.c_str());
    }
}

void DhcpServer::sendAck(const Message& message, unsigned int interfaceIndex) {
    updateDnsServers();
    in_addr_t offerAddress;
    in_addr_t netmask;
    in_addr_t gateway;
    in_addr_t serverAddress;
    Result res = getOfferAddress(interfaceIndex,
                                 message.dhcpData.chaddr,
                                 &offerAddress,
                                 &netmask,
                                 &gateway);
    if (!res) {
        ALOGE("Failed to get address for offer: %s", res.c_str());
        return;
    }
    res = getInterfaceAddress(interfaceIndex, &serverAddress);
    if (!res) {
        ALOGE("Failed to get address for interface %u: %s",
              interfaceIndex, res.c_str());
        return;
    }
    Message ack = Message::ack(message,
                               serverAddress,
                               offerAddress,
                               netmask,
                               gateway,
                               mDnsServers.data(),
                               mDnsServers.size());
    res = sendMessage(interfaceIndex, serverAddress, ack);
    if (!res) {
        ALOGE("Failed to send DHCP ack: %s", res.c_str());
    }
}

void DhcpServer::sendNack(const Message& message, unsigned int interfaceIndex) {
    in_addr_t serverAddress;
    Result res = getInterfaceAddress(interfaceIndex, &serverAddress);
    if (!res) {
        ALOGE("Failed to get address for interface %u: %s",
              interfaceIndex, res.c_str());
        return;
    }
    Message nack = Message::nack(message, serverAddress);
    res = sendMessage(interfaceIndex, serverAddress, nack);
    if (!res) {
        ALOGE("Failed to send DHCP nack: %s", res.c_str());
    }
}

bool DhcpServer::isValidDhcpRequest(const Message& message,
                                    unsigned int interfaceIndex) {
    in_addr_t offerAddress;
    in_addr_t netmask;
    in_addr_t gateway;
    Result res = getOfferAddress(interfaceIndex,
                                 message.dhcpData.chaddr,
                                 &offerAddress,
                                 &netmask,
                                 &gateway);
    if (!res) {
        ALOGE("Failed to get address for offer: %s", res.c_str());
        return false;
    }
    if (message.requestedIp() != offerAddress) {
        ALOGE("Client requested a different IP address from the offered one");
        return false;
    }
    return true;
}

void DhcpServer::updateDnsServers() {
    char key[64];
    char value[PROPERTY_VALUE_MAX];
    mDnsServers.clear();
    for (int i = 1; i <= kMaxDnsServers; ++i) {
        snprintf(key, sizeof(key), "net.eth0.dns%d", i);
        if (property_get(key, value, nullptr) > 0) {
            struct in_addr address;
            if (::inet_pton(AF_INET, value, &address) > 0) {
                mDnsServers.push_back(address.s_addr);
            }
        }
    }
}

Result DhcpServer::getInterfaceData(unsigned int interfaceIndex,
                                    unsigned long type,
                                    struct ifreq* response) {
    char interfaceName[IF_NAMESIZE + 1];
    if (if_indextoname(interfaceIndex, interfaceName) == nullptr) {
        return Result::error("Failed to get interface name for index %u: %s",
                             interfaceIndex, strerror(errno));
    }
    memset(response, 0, sizeof(*response));
    response->ifr_addr.sa_family = AF_INET;
    strncpy(response->ifr_name, interfaceName, IFNAMSIZ - 1);

    if (::ioctl(mSocket.get(), type, response) == -1) {
        return Result::error("Failed to get data for interface %s: %s",
                             interfaceName, strerror(errno));
    }

    return Result::success();
}

Result DhcpServer::getInterfaceAddress(unsigned int interfaceIndex,
                                       in_addr_t* address) {
    struct ifreq data;
    Result res = getInterfaceData(interfaceIndex, SIOCGIFADDR, &data);
    if (res.isSuccess()) {
        auto inAddr = reinterpret_cast<struct sockaddr_in*>(&data.ifr_addr);
        *address = inAddr->sin_addr.s_addr;
    }
    return res;
}

Result DhcpServer::getInterfaceNetmask(unsigned int interfaceIndex,
                                       in_addr_t* address) {
    struct ifreq data;
    Result res = getInterfaceData(interfaceIndex, SIOCGIFNETMASK, &data);
    if (res.isSuccess()) {
        auto inAddr = reinterpret_cast<struct sockaddr_in*>(&data.ifr_addr);
        *address = inAddr->sin_addr.s_addr;
    }
    return res;
}

static bool isValidHost(const in_addr_t address,
                        const in_addr_t interfaceAddress,
                        const in_addr_t netmask) {
    // If the bits outside of the netmask are all zero it's a network address,
    // don't use this.
    bool isNetworkAddress = (address & ~netmask) == 0;
    // If all bits outside of the netmask are set then it's a broadcast address,
    // don't use this either.
    bool isBroadcastAddress = (address & ~netmask) == ~netmask;
    // Don't assign the interface address to a host
    bool isInterfaceAddress = address == interfaceAddress;

    return !isNetworkAddress && !isBroadcastAddress && !isInterfaceAddress;
}

static bool addressInRange(const in_addr_t address,
                           const in_addr_t interfaceAddress,
                           const in_addr_t netmask) {
    if (address <= (interfaceAddress & netmask)) {
        return false;
    }
    if (address >= (interfaceAddress | ~netmask)) {
        return false;
    }
    return true;
}

Result DhcpServer::getOfferAddress(unsigned int interfaceIndex,
                                   const uint8_t* macAddress,
                                   in_addr_t* address,
                                   in_addr_t* netmask,
                                   in_addr_t* gateway) {
    // The interface address will be the gateway and will be used to determine
    // the range of valid addresses (along with the netmask) for the client.
    in_addr_t interfaceAddress = 0;
    Result res = getInterfaceAddress(interfaceIndex, &interfaceAddress);
    if (!res) {
        return res;
    }
    // The netmask of the interface will be the netmask for the client as well
    // as used to determine network range.
    in_addr_t mask = 0;
    res = getInterfaceNetmask(interfaceIndex, &mask);
    if (!res) {
        return res;
    }

    // Assign these values now before they are modified below
    *gateway = interfaceAddress;
    *netmask = mask;

    Lease key(interfaceIndex, macAddress);

    // Find or create entry, if it's created it will be zero and we update it
    in_addr_t& value = mLeases[key];
    if (value == 0) {
        // Addresses are stored in network byte order so when doing math on them
        // they have to be converted to host byte order
        interfaceAddress = ntohl(interfaceAddress);
        mask = ntohl(mask);
        // Get a reference to the offset so we can use it and increase it at the
        // same time. If the entry does not exist it will be created with a
        // value of zero.
        in_addr_t& offset = mNextAddressOffsets[interfaceIndex];
        if (offset == 0) {
            // Increase if zero to avoid assigning network address
            ++offset;
        }
        // Start out at the first address in the range as determined by netmask
        in_addr_t nextAddress = (interfaceAddress & mask) + offset;

        // Ensure the address is valid
        while (!isValidHost(nextAddress, interfaceAddress, mask) &&
               addressInRange(nextAddress, interfaceAddress, mask)) {
            ++nextAddress;
            ++offset;
        }

        if (addressInRange(nextAddress, interfaceAddress, mask)) {
            // Convert back to network byte order
            value = htonl(nextAddress);
            ++offset;
        } else {
            // Ran out of addresses
            return Result::error("DHCP server is out of addresses");
        }
    }
    *address = value;
    return Result::success();
}

