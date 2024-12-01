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

#include "proxy.h"

#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_packet.h>
#include <poll.h>
#include <signal.h>

#include <cutils/properties.h>

#include "log.h"
#include "message.h"
#include "packet.h"
#include "result.h"

// The prefix length for an address of a single unique node
static const uint8_t kNodePrefixLength = 128;
static const size_t kLinkAddressSize = 6;
static const size_t kRecursiveDnsOptHeaderSize = 8;

// Rewrite the link address of a neighbor discovery option to the link address
// of |interface|. This can be either a source or target link address as
// specified by |optionType|. The valid values are ND_OPT_TARGET_LINKADDR and
// ND_OPT_SOURCE_LINKADDR. This will modify the message data inside |packet|.
static void rewriteLinkAddressOption(Packet& packet,
                                     const Interface& interface,
                                     int optionType) {
    for (nd_opt_hdr* opt = packet.firstOpt(); opt; opt = packet.nextOpt(opt)) {
        if (opt->nd_opt_type == optionType) {
            auto src = interface.linkAddr().get<sockaddr_ll>();
            auto dest = reinterpret_cast<char*>(opt) + sizeof(nd_opt_hdr);
            memcpy(dest, src->sll_addr, kLinkAddressSize);
        }
    }
}

static void extractRecursiveDnsServers(Packet& packet) {
    for (nd_opt_hdr* opt = packet.firstOpt(); opt; opt = packet.nextOpt(opt)) {
        if (opt->nd_opt_type != 25 || opt->nd_opt_len < 1) {
            // Not a RNDSS option, skip it
            continue;
        }
        size_t numEntries = (opt->nd_opt_len - 1) / 2;
        //Found number of entries, dump  each address
        const char* option = reinterpret_cast<const char*>(opt);
        option += kRecursiveDnsOptHeaderSize;
        auto dnsServers = reinterpret_cast<const struct in6_addr*>(option);

        std::vector<std::string> validServers;
        for (size_t i = 0; i < numEntries; ++i) {
            char buffer[INET6_ADDRSTRLEN];
            if (inet_ntop(AF_INET6, &dnsServers[i], buffer, sizeof(buffer))) {
                validServers.push_back(buffer);
            } else {
                loge("Failed to convert RDNSS to string\n");
            }
        }

        auto server = validServers.begin();
        char propName[PROP_NAME_MAX];
        char propValue[PROP_VALUE_MAX];
        for (int i = 1; i <= 4; ++i) {
            snprintf(propName, sizeof(propName), "net.eth0.ipv6dns%d", i);
            if (server != validServers.end()) {
                property_set(propName, server->c_str());
                ++server;
            } else {
                // Clear the property if it's no longer a valid server, don't
                // want to leave old servers around
                property_set(propName, "");
            }
        }
    }
}

int Proxy::run() {
    sigset_t blockMask, originalMask;
    int status = ::sigfillset(&blockMask);
    if (status != 0) {
        loge("Unable to fill signal set: %s\n", strerror(errno));
        return 1;
    }
    status = ::sigprocmask(SIG_SETMASK, &blockMask, &originalMask);
    if (status != 0) {
        loge("Unable to set signal mask: %s\n", strerror(errno));
        return 1;
    }
    // Init outer interface and router
    if (!mOuterIf.init() || !mRouter.init()) {
        return 1;
    }
    // Init all inner interfaces
    for (size_t i = 0; i < mInnerIfs.size(); ++i) {
        if (!mInnerIfs[i].init()) {
            return 1;
        }
    }

    // Create list of FDs to poll, we're only looking for input (POLLIN)
    std::vector<pollfd> fds(mInnerIfs.size() + 1);
    fds[0].fd = mOuterIf.ipSocket().get();
    fds[0].events = POLLIN;
    for (size_t i = 0; i < mInnerIfs.size(); ++i) {
        fds[i + 1].fd = mInnerIfs[i].ipSocket().get();
        fds[i + 1].events = POLLIN;
    }

    Message message;
    while (status >= 0) {
        status = ::ppoll(fds.data(), fds.size(), nullptr, &originalMask);
        if (status > 0) {
            // Something available to read
            for (const struct pollfd& fd : fds) {
                if (receiveIfPossible(fd, mOuterIf.ipSocket(), &message)) {
                    // Received a message on the outer interface
                    handleOuterMessage(message);
                } else {
                    for (auto& inner : mInnerIfs) {
                        if (receiveIfPossible(fd, inner.ipSocket(), &message)) {
                            // Received a message on the inner interface
                            handleInnerMessage(inner, message);
                        }
                    }
                }
            }
        }
    }
    loge("Polling failed: %s\n", strerror(errno));
    return 1;
}

bool Proxy::receiveIfPossible(const pollfd& fd,
                              Socket& socket,
                              Message* message) {
    // Check if it's actually the socket we're interested in
    if (fd.fd != socket.get()) {
        return false;
    }
    // Check if there is something to read on this socket
    if ((fd.revents & POLLIN) == 0) {
        return false;
    }

    // Receive the message and place the data in the message parameter
    Result res = socket.receive(message);
    if (!res) {
        loge("Error receiving on socket: %s\n", res.c_str());
        return false;
    }
    return true;
}

void Proxy::handleOuterMessage(Message& message) {
    Packet packet(message);
    uint32_t options = kForwardOnly;
    switch (packet.type()) {
        case Packet::Type::RouterAdvertisement:
            extractRecursiveDnsServers(packet);
            options = kRewriteSourceLink | kSetDefaultGateway;
            break;
        case Packet::Type::NeighborSolicitation:
            options = kSpoofSource;
            break;
        case Packet::Type::NeighborAdvertisement:
            options = kRewriteTargetLink;
            break;
        default:
            return;
    }
    for (auto& inner : mInnerIfs) {
        forward(mOuterIf, inner, packet, options);
    }
}

void Proxy::handleInnerMessage(const Interface& inner, Message& message) {
    Packet packet(message);
    uint32_t options = kForwardOnly;
    switch (packet.type()) {
        case Packet::Type::RouterSolicitation:
            options = kSpoofSource;
            break;
        case Packet::Type::NeighborSolicitation:
            options = kSpoofSource | kAddRoute;
            break;
        case Packet::Type::NeighborAdvertisement:
            options = kRewriteTargetLink | kSpoofSource | kAddRoute;
            break;
        default:
            return;
    }
    forward(inner, mOuterIf, packet, options);
}

void Proxy::forward(const Interface& from,
                    Interface& to,
                    Packet& packet,
                    uint32_t options) {
    if (mLogDebug) {
        logd("Forwarding %s from %s/%s to %s/%s\n",
             packet.description().c_str(),
             from.name().c_str(), addrToStr(packet.ip()->ip6_src).c_str(),
             to.name().c_str(), addrToStr(packet.ip()->ip6_dst).c_str());
    }

    if (options & kRewriteTargetLink) {
        rewriteLinkAddressOption(packet, to, ND_OPT_TARGET_LINKADDR);
    }
    if (options & kRewriteSourceLink) {
        rewriteLinkAddressOption(packet, to, ND_OPT_SOURCE_LINKADDR);
    }

    Result res = Result::success();
    if (options & kSpoofSource) {
        // Spoof the source of the packet so that it appears to originate from
        // the same source that we see.
        res = to.icmpSocket().sendFrom(packet.ip()->ip6_src,
                                       packet.ip()->ip6_dst,
                                       packet.icmp(),
                                       packet.icmpSize());
    } else {
        res = to.icmpSocket().sendTo(packet.ip()->ip6_dst,
                                     packet.icmp(),
                                     packet.icmpSize());
    }
    if (!res) {
        loge("Failed to forward %s from %s to %s: %s\n",
             packet.description().c_str(),
             from.name().c_str(), to.name().c_str(),
             res.c_str());
    }

    if (options & kAddRoute) {
        mRouter.addRoute(packet.ip()->ip6_src, kNodePrefixLength, from.index());
    }
    if (packet.type() == Packet::Type::RouterAdvertisement &&
        options & kSetDefaultGateway) {
        // Set the default gateway from this router advertisement. This is
        // needed so that packets that are forwarded as a result of proxying
        // actually have somewhere to go.
        if (!mRouter.setDefaultGateway(packet.ip()->ip6_src, from.index())) {
            loge("Failed to set default gateway %s\n",
                 addrToStr(packet.ip()->ip6_src).c_str());
        }
    }
}

