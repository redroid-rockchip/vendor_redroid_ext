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

#include "interface.h"

#include "netlink.h"

#include <errno.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/route.h>
#include <linux/rtnetlink.h>
#include <string.h>
#include <unistd.h>

in_addr_t broadcastFromNetmask(in_addr_t address, in_addr_t netmask) {
    // The broadcast address is the address with the bits excluded in the
    // netmask set to 1. For example if address = 10.0.2.15 and netmask is
    // 255.255.255.0 then the broadcast is 10.0.2.255. If instead netmask was
    // 255.0.0.0.0 then the broadcast would be 10.255.255.255
    //
    // Simply set all the lower bits to 1 and that should do it.
    return address | (~netmask);
}

Interface::Interface() : mSocketFd(-1) {
}

Interface::~Interface() {
    if (mSocketFd != -1) {
        close(mSocketFd);
        mSocketFd = -1;
    }
}

Result Interface::init(const char* interfaceName) {
    mInterfaceName = interfaceName;

    if (mSocketFd != -1) {
        return Result::error("Interface initialized more than once");
    }

    mSocketFd = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (mSocketFd == -1) {
        return Result::error("Failed to create interface socket for '%s': %s",
                             interfaceName, strerror(errno));
    }

    Result res = populateIndex();
    if (!res) {
        return res;
    }

    res = populateMacAddress();
    if (!res) {
        return res;
    }

    res = bringUp();
    if (!res) {
        return res;
    }

    res = setAddress(0, 0);
    if (!res) {
        return res;
    }

    return Result::success();
}

Result Interface::bringUp() {
    return setInterfaceUp(true);
}

Result Interface::bringDown() {
    return setInterfaceUp(false);
}

Result Interface::setMtu(uint16_t mtu) {
    struct ifreq request = createRequest();

    strncpy(request.ifr_name, mInterfaceName.c_str(), sizeof(request.ifr_name));
    request.ifr_mtu = mtu;
    int status = ::ioctl(mSocketFd, SIOCSIFMTU, &request);
    if (status != 0) {
        return Result::error("Failed to set interface MTU %u for '%s': %s",
                             static_cast<unsigned int>(mtu),
                             mInterfaceName.c_str(),
                             strerror(errno));
    }

    return Result::success();
}

Result Interface::setAddress(in_addr_t address, in_addr_t subnetMask) {
    struct Request {
        struct nlmsghdr hdr;
        struct ifaddrmsg msg;
        char buf[256];
    } request;

    memset(&request, 0, sizeof(request));

    request.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(request.msg));
    request.hdr.nlmsg_type = RTM_NEWADDR;
    request.hdr.nlmsg_flags = NLM_F_REQUEST |
                              NLM_F_ACK |
                              NLM_F_CREATE |
                              NLM_F_REPLACE;

    request.msg.ifa_family = AF_INET;
    // Count the number of bits in the subnet mask, this is the length.
    request.msg.ifa_prefixlen = __builtin_popcount(subnetMask);
    request.msg.ifa_index = mIndex;

    addRouterAttribute(request, IFA_ADDRESS, &address, sizeof(address));
    addRouterAttribute(request, IFA_LOCAL, &address, sizeof(address));
    in_addr_t broadcast = broadcastFromNetmask(address, subnetMask);
    addRouterAttribute(request, IFA_BROADCAST, &broadcast, sizeof(broadcast));

    struct sockaddr_nl nlAddr;
    memset(&nlAddr, 0, sizeof(nlAddr));
    nlAddr.nl_family = AF_NETLINK;

    int status = ::sendto(mSocketFd, &request, request.hdr.nlmsg_len, 0,
                          reinterpret_cast<sockaddr*>(&nlAddr),
                          sizeof(nlAddr));
    if (status == -1) {
        return Result::error("Unable to set interface address: %s",
                             strerror(errno));
    }
    char buffer[8192];
    status = ::recv(mSocketFd, buffer, sizeof(buffer), 0);
    if (status < 0) {
        return Result::error("Unable to read netlink response: %s",
                             strerror(errno));
    }
    size_t responseSize = static_cast<size_t>(status);
    if (responseSize < sizeof(nlmsghdr)) {
        return Result::error("Received incomplete response from netlink");
    }
    auto response = reinterpret_cast<const nlmsghdr*>(buffer);
    if (response->nlmsg_type == NLMSG_ERROR) {
        if (responseSize < NLMSG_HDRLEN + sizeof(nlmsgerr)) {
            return Result::error("Recieved an error from netlink but the "
                                 "response was incomplete");
        }
        auto err = reinterpret_cast<const nlmsgerr*>(NLMSG_DATA(response));
        if (err->error) {
            return Result::error("Could not set interface address: %s",
                                 strerror(-err->error));
        }
    }
    return Result::success();
}

struct ifreq Interface::createRequest() const {
    struct ifreq request;
    memset(&request, 0, sizeof(request));
    strncpy(request.ifr_name, mInterfaceName.c_str(), sizeof(request.ifr_name));
    request.ifr_name[sizeof(request.ifr_name) - 1] = '\0';

    return request;
}

Result Interface::populateIndex() {
    struct ifreq request = createRequest();

    int status = ::ioctl(mSocketFd, SIOCGIFINDEX, &request);
    if (status != 0) {
        return Result::error("Failed to get interface index for '%s': %s",
                             mInterfaceName.c_str(), strerror(errno));
    }
    mIndex = request.ifr_ifindex;
    return Result::success();
}

Result Interface::populateMacAddress() {
    struct ifreq request = createRequest();

    int status = ::ioctl(mSocketFd, SIOCGIFHWADDR, &request);
    if (status != 0) {
        return Result::error("Failed to get MAC address for '%s': %s",
                             mInterfaceName.c_str(), strerror(errno));
    }
    memcpy(mMacAddress, &request.ifr_hwaddr.sa_data, ETH_ALEN);
    return Result::success();
}

Result Interface::setInterfaceUp(bool shouldBeUp) {
    struct ifreq request = createRequest();

    int status = ::ioctl(mSocketFd, SIOCGIFFLAGS, &request);
    if (status != 0) {
        return Result::error("Failed to get interface flags for '%s': %s",
                             mInterfaceName.c_str(), strerror(errno));
    }

    bool isUp = (request.ifr_flags & IFF_UP) != 0;
    if (isUp != shouldBeUp) {
        // Toggle the up flag
        request.ifr_flags ^= IFF_UP;
    } else {
        // Interface is already in desired state, do nothing
        return Result::success();
    }

    status = ::ioctl(mSocketFd, SIOCSIFFLAGS, &request);
    if (status != 0) {
        return Result::error("Failed to set interface flags for '%s': %s",
                             mInterfaceName.c_str(), strerror(errno));
    }

    return Result::success();
}

