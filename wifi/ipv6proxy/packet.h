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
#pragma once

#include <netinet/icmp6.h>
#include <netinet/ip6.h>

#include <string>
#include <vector>

#include "message.h"

class Packet {
public:
    enum class Type {
        NeighborSolicitation,
        NeighborAdvertisement,
        RouterSolicitation,
        RouterAdvertisement,
        Other
    };

    explicit Packet(Message& message);

    // Create a string that can be used for debug purposes to describe
    // what type of packet and potentially its target for certain types.
    std::string description() const;

    // Full size including IP header
    size_t fullSize() const {
        return mMessage.size();
    }
    // Remaining size including ICMPv6 header but excluding IP header
    size_t icmpSize() const {
        return mMessage.size() - sizeof(ip6_hdr);
    }

    Type type() const {
        return mType;
    }
    const ip6_hdr* ip() const {
        return mIp;
    }
    const icmp6_hdr* icmp() const {
        return mIcmp;
    }

    nd_opt_hdr* firstOpt();
    nd_opt_hdr* nextOpt(nd_opt_hdr* currentHeader);

private:
    Message& mMessage;
    Type mType;

    const ip6_hdr* mIp;
    const icmp6_hdr* mIcmp;
    nd_opt_hdr* mFirstOpt;
};

