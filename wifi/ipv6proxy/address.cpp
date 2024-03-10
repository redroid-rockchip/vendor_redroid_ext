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

#include "address.h"

#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netdb.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>

#include "socket.h"

std::string addrToStr(const struct in6_addr& addr) {
    char buf[INET6_ADDRSTRLEN];
    if (inet_ntop(AF_INET6, &addr, buf, sizeof(buf)) == nullptr) {
        return "[unknown]";
    }
    return buf;
}

Address::Address() {
    mStorage.reserve(sizeof(struct sockaddr_storage));
}

Address::Address(const struct sockaddr_nl& address)
        : mStorage(sizeof(address)) {
    memcpy(mStorage.data(), &address, sizeof(address));
}
Address::Address(const struct sockaddr_in6& address)
        : mStorage(sizeof(address)) {
    memcpy(mStorage.data(), &address, sizeof(address));
}

Address::Address(struct in6_addr address)
        : mStorage(sizeof(struct sockaddr_in6)) {
    auto sockaddr = reinterpret_cast<struct sockaddr_in6*>(mStorage.data());
    sockaddr->sin6_family = AF_INET6;
    sockaddr->sin6_addr = address;
}

void Address::reset() {
    mStorage.resize(sizeof(struct sockaddr_storage));
}

Result Address::resolveInet(const std::string& address) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_RAW;
    struct addrinfo* addrinfo;
    int res = ::getaddrinfo(address.c_str(), nullptr, &hints, &addrinfo);
    if (res != 0) {
        return Result::error(gai_strerror(res));
    }
    mStorage.resize(addrinfo->ai_addrlen);
    memcpy(mStorage.data(), addrinfo->ai_addr, mStorage.size());
    freeaddrinfo(addrinfo);

    return Result::success();
}

Result Address::resolveEth(const std::string& interfaceName) {
    mStorage.resize(sizeof(struct sockaddr_ll));
    memset(mStorage.data(), 0, mStorage.size());
    auto addr = reinterpret_cast<struct sockaddr_ll*>(mStorage.data());
    addr->sll_family = AF_PACKET;
    addr->sll_protocol = htons(ETH_P_IPV6);

    unsigned int index = if_nametoindex(interfaceName.c_str());
    if (index == 0) {
        return Result::error(strerror(errno));
    }
    addr->sll_ifindex = index;

    struct ifreq request;
    memset(&request, 0, sizeof(request));
    strncpy(request.ifr_name, interfaceName.c_str(), sizeof(request.ifr_name));
    request.ifr_name[sizeof(request.ifr_name) - 1] = '\0';

    Socket socket;
    Result res = socket.open(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (!res) {
        return res;
    }
    int status = ::ioctl(socket.get(), SIOCGIFHWADDR, &request);
    if (status != 0) {
        return Result::error(strerror(errno));
    }
    memcpy(addr->sll_addr, request.ifr_addr.sa_data, 6);

    return Result::success();
}
