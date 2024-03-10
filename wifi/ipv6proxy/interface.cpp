/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "interface.h"

#include <errno.h>
#include <linux/if_ether.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>

#include "log.h"

Interface::Interface(const std::string& name) : mName(name) {
}

bool Interface::init() {
    // setAllMulti will set the ALLMULTI flag for the interface, this allows us
    // to capture all the traffic needed to perform proxying.
    return setAllMulti() &&
           resolveAddresses() &&
           configureIpSocket() &&
           configureIcmpSocket();
}

bool Interface::setAllMulti() {
    struct ifreq request;
    memset(&request, 0, sizeof(request));
    strncpy(request.ifr_name, mName.c_str(), sizeof(request.ifr_name));
    request.ifr_name[sizeof(request.ifr_name) - 1] = '\0';

    Socket socket;
    Result res = socket.open(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (!res) {
        loge("Failed to open IP socket for interface %s: %s\n",
             mName.c_str(), strerror(errno));
        return false;
    }
    int status = ::ioctl(socket.get(), SIOCGIFFLAGS, &request);
    if (status != 0) {
        loge("Failed to get interface flags for %s: %s\n",
             mName.c_str(), strerror(errno));
        return false;
    }

    if ((request.ifr_flags & IFF_ALLMULTI) != 0) {
        // AllMulti is already enabled, nothing to do
        return true;
    }

    request.ifr_flags |= IFF_ALLMULTI;

    status = ::ioctl(socket.get(), SIOCSIFFLAGS, &request);
    if (status != 0) {
        loge("Failed to enable AllMulti flag for %s: %s\n",
             mName.c_str(), strerror(errno));
        return false;
    }
    return true;
}

bool Interface::resolveAddresses() {
    Result res = mLinkAddr.resolveEth(mName);
    if (!res) {
        loge("Unable to resolve interface %s: %s\n",
             mName.c_str(), res.c_str());
        return false;
    }
    mIndex = if_nametoindex(mName.c_str());
    if (mIndex == 0) {
        loge("Unable to get interface index for '%s': %s\n",
             mName.c_str(), strerror(errno));
        return false;
    }
    return true;
}

bool Interface::configureIcmpSocket() {
    Result res = mIcmpSocket.open(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
    if (!res) {
        loge("Error opening socket: %s\n", res.c_str());
        return false;
    }

    // The ICMP messages we are going to send need a hop limit of 255 to be
    // accepted.
    res = mIcmpSocket.setMulticastHopLimit(255);
    if (!res) {
        loge("Error setting socket hop limit: %s\n", res.c_str());
        return false;
    }
    res = mIcmpSocket.setUnicastHopLimit(255);
    if (!res) {
        loge("Error setting socket hop limit: %s\n", res.c_str());
        return false;
    }

    // We only care about one specific interface
    res = mIcmpSocket.setInterface(mName);
    if (!res) {
        loge("Error socket interface: %s\n", res.c_str());
        return false;
    }

    // Make sure the socket allows transparent proxying, this allows sending of
    // packets with a source address that is different from the interface.
    res = mIcmpSocket.setTransparent(true);
    if (!res) {
        loge("Error socket interface: %s\n", res.c_str());
        return false;
    }

    return true;
}

bool Interface::configureIpSocket() {
    Result res = mIpSocket.open(AF_PACKET, SOCK_DGRAM, ETH_P_IPV6);
    if (!res) {
        loge("Error opening socket: %s\n", res.c_str());
        return false;
    }

    res = mIpSocket.bind(mLinkAddr);
    if (!res) {
        loge("Error binding socket: %s\n", res.c_str());
        return false;
    }
    return true;
}

