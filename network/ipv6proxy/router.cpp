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

#include "router.h"

#include <linux/rtnetlink.h>
#include <stddef.h>
#include <string.h>

#include "address.h"
#include "log.h"

template<class Request>
static void addRouterAttribute(Request& r,
                               int type,
                               const void* data,
                               size_t size) {
    // Calculate the offset into the character buffer where the RTA data lives
    // We use offsetof on the buffer to get it. This avoids undefined behavior
    // by casting the buffer (which is safe because it's char) instead of the
    // Request struct.(which is undefined because of aliasing)
    size_t offset = NLMSG_ALIGN(r.hdr.nlmsg_len) - offsetof(Request, buf);
    auto attr = reinterpret_cast<struct rtattr*>(r.buf + offset);
    attr->rta_type = type;
    attr->rta_len = RTA_LENGTH(size);
    memcpy(RTA_DATA(attr), data, size);

    // Update the message length to include the router attribute.
    r.hdr.nlmsg_len = NLMSG_ALIGN(r.hdr.nlmsg_len) + RTA_ALIGN(attr->rta_len);
}

bool Router::init() {
    // Create a netlink socket to the router
    Result res = mSocket.open(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (!res) {
        loge("Unable to open netlink socket: %s\n", res.c_str());
        return false;
    }
    return true;
}

bool Router::addNeighbor(const struct in6_addr& address,
                         unsigned int interfaceIndex) {
    struct Request {
        struct nlmsghdr hdr;
        struct ndmsg msg;
        char buf[256];
    } request;

    memset(&request, 0, sizeof(request));

    unsigned short msgLen = NLMSG_LENGTH(sizeof(request.msg));
    // Set up a request to create a new neighbor
    request.hdr.nlmsg_len = msgLen;
    request.hdr.nlmsg_type = RTM_NEWNEIGH;
    request.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE;

    // The neighbor is a permanent IPv6 proxy
    request.msg.ndm_family = AF_INET6;
    request.msg.ndm_state = NUD_PERMANENT;
    request.msg.ndm_flags = NTF_PROXY;
    request.msg.ndm_ifindex = interfaceIndex;

    addRouterAttribute(request, NDA_DST, &address, sizeof(address));

    return sendNetlinkMessage(&request, request.hdr.nlmsg_len);
}

bool Router::addRoute(const struct in6_addr& address,
                      uint8_t bits,
                      uint32_t ifaceIndex) {
    struct Request {
        struct nlmsghdr hdr;
        struct rtmsg msg;
        char buf[256];
    } request;

    memset(&request, 0, sizeof(request));

    // Set up a request to create a new route
    request.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(request.msg));
    request.hdr.nlmsg_type = RTM_NEWROUTE;
    request.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE;

    request.msg.rtm_family = AF_INET6;
    request.msg.rtm_dst_len = bits;
    request.msg.rtm_table = RT_TABLE_MAIN;
    request.msg.rtm_protocol = RTPROT_RA;
    request.msg.rtm_scope = RT_SCOPE_UNIVERSE;
    request.msg.rtm_type = RTN_UNICAST;

    addRouterAttribute(request, RTA_DST, &address, sizeof(address));
    addRouterAttribute(request, RTA_OIF, &ifaceIndex, sizeof(ifaceIndex));

    return sendNetlinkMessage(&request, request.hdr.nlmsg_len);
}

bool Router::setDefaultGateway(const struct in6_addr& address,
                               unsigned int ifaceIndex) {
    struct Request {
        struct nlmsghdr hdr;
        struct rtmsg msg;
        char buf[256];
    } request;

    memset(&request, 0, sizeof(request));

    // Set up a request to create a new route
    request.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(request.msg));
    request.hdr.nlmsg_type = RTM_NEWROUTE;
    request.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE;

    request.msg.rtm_family = AF_INET6;
    request.msg.rtm_dst_len = 0;
    request.msg.rtm_src_len = 0;
    request.msg.rtm_table = RT_TABLE_MAIN;
    request.msg.rtm_protocol = RTPROT_RA;
    request.msg.rtm_scope = RT_SCOPE_UNIVERSE;
    request.msg.rtm_type = RTN_UNICAST;

    struct in6_addr anyAddress;
    memset(&anyAddress, 0, sizeof(anyAddress));
    addRouterAttribute(request, RTA_GATEWAY, &address, sizeof(address));
    addRouterAttribute(request, RTA_OIF, &ifaceIndex, sizeof(ifaceIndex));
    addRouterAttribute(request, RTA_SRC, &anyAddress, sizeof(anyAddress));

    return sendNetlinkMessage(&request, request.hdr.nlmsg_len);
}

bool Router::sendNetlinkMessage(const void* data, size_t size) {
    struct sockaddr_nl netlinkAddress;
    memset(&netlinkAddress, 0, sizeof(netlinkAddress));
    netlinkAddress.nl_family = AF_NETLINK;
    Result res = mSocket.sendTo(netlinkAddress, data, size);
    if (!res) {
        loge("Unable to send on netlink socket: %s\n", res.c_str());
        return false;
    }
    return true;
}

