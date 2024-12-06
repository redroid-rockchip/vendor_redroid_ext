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

#include "packet.h"

#include "address.h"

Packet::Packet(Message& message)
    : mMessage(message),
      mType(Type::Other),
      mIp(nullptr),
      mIcmp(nullptr),
      mFirstOpt(nullptr) {
    if (message.size() < sizeof(ip6_hdr) + sizeof(icmp6_hdr)) {
        mType = Type::Other;
        return;
    }
    mIp = reinterpret_cast<const ip6_hdr*>(message.data());
    uint8_t version = (mIp->ip6_vfc & 0xF0) >> 4;
    if (version != 6 || mIp->ip6_nxt != IPPROTO_ICMPV6) {
        mType = Type::Other;
        return;
    }

    size_t size = message.size() - sizeof(ip6_hdr);
    char* data = message.data() + sizeof(ip6_hdr);
    mIcmp = reinterpret_cast<const icmp6_hdr*>(data);
    if (mIcmp->icmp6_code != 0) {
        // All messages we care about have a code of zero
        mType = Type::Other;
        return;
    }

    size_t headerSize = 0;
    switch (mIcmp->icmp6_type) {
    case ND_ROUTER_SOLICIT:
        headerSize = sizeof(nd_router_solicit);
        mType = Type::RouterSolicitation;
        break;
    case ND_ROUTER_ADVERT:
        headerSize = sizeof(nd_router_advert);
        mType = Type::RouterAdvertisement;
        break;
    case ND_NEIGHBOR_SOLICIT:
        headerSize = sizeof(nd_neighbor_solicit);
        mType = Type::NeighborSolicitation;
        break;
    case ND_NEIGHBOR_ADVERT:
        headerSize = sizeof(nd_neighbor_advert);
        mType = Type::NeighborAdvertisement;
        break;
    default:
        mType = Type::Other;
        return;
    }
    if (size < headerSize) {
        mType = Type::Other;
        return;
    }

    // We might have options
    char* options = data + headerSize;
    if (options + sizeof(nd_opt_hdr) < data + size) {
        nd_opt_hdr* option = reinterpret_cast<nd_opt_hdr*>(options);
        // Option length is in units of 8 bytes, multiply by 8 to get bytes
        if (options + option->nd_opt_len * 8u <= data + size) {
            mFirstOpt = option;
        }
    }
}

std::string Packet::description() const {
    char buffer[256];
    switch (mType) {
        case Type::NeighborSolicitation: {
                auto ns = reinterpret_cast<const nd_neighbor_solicit*>(icmp());
                snprintf(buffer, sizeof(buffer), "Neighbor Solicitation for %s",
                         addrToStr(ns->nd_ns_target).c_str());
                return buffer;
            }
        case Type::NeighborAdvertisement: {
                auto na = reinterpret_cast<const nd_neighbor_advert*>(icmp());
                snprintf(buffer, sizeof(buffer),
                         "Neighbor Advertisement for %s",
                         addrToStr(na->nd_na_target).c_str());
                return buffer;
            }
        case Type::RouterSolicitation:
            return "Router Solicitation";
        case Type::RouterAdvertisement:
            return "Router Advertisement";
        default:
            break;
    }
    return "[unknown]";
}

nd_opt_hdr* Packet::firstOpt() {
    return mFirstOpt;
}

nd_opt_hdr* Packet::nextOpt(nd_opt_hdr* currentHeader) {
    char* end = mMessage.data() + mMessage.size();
    char* current = reinterpret_cast<char*>(currentHeader);
    if (currentHeader < mFirstOpt || current >= end) {
        // The provided header does not belong to this packet info.
        return nullptr;
    }
    char* next = current + currentHeader->nd_opt_len * 8u;
    if (next >= end) {
        // The next header points passed the message data
        return nullptr;
    }
    nd_opt_hdr* nextHeader = reinterpret_cast<nd_opt_hdr*>(next);
    if (next + nextHeader->nd_opt_len * 8u > end) {
        // The next option extends beyond the message data
        return nullptr;
    }
    return nextHeader;
}

