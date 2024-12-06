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

#include "result.h"

#include <netinet/in.h>

#include <stdint.h>
#include <string>

class Address;
class Message;

class Socket {
public:
    enum class Domain {
        IpV4,
        IpV6,
        Packet,
    };

    enum class Type {
        Stream,   // A streaming protocol, use this with Infer for TCP
        Datagram, // A datagram protocol, use this with Infer for UDP
        Raw,      // A raw socket
    };
    enum class Protocol {
        Infer,    // Infer the protocol from the type, such as TCP for Stream
        Ip,       // Internet Protocol for raw sockets
        IcmpV6,   // ICMPv6 control protocol for Raw sockets
        EthIpV6,  // Ethernet packets containing IPV6, for packet sockets
    };

    // Construct an empty socket object, next use open() to start using it
    Socket();
    // Move construct a socket, The constructed socket will be in the same state
    // that |other| is. After this |other| will be in an undefined state and
    // should no longer be used.
    Socket(Socket&& other) noexcept;
    ~Socket();

    // Move the |other| socket object into this one. If this object has an open
    // socket it will be closed first. After that this object will have the
    // same state that |other| did. |other| will be left in an undefined state
    // and should not be used.
    Socket& operator=(Socket&& other) noexcept;

    int get() const { return mSocket; }

    Result open(int domain, int type, int protocol);

    /** Options, these must be called between open and bind **/

    // Bind to a specific interface regardless of the address that the socket
    // is going to bind to.
    Result setInterface(const std::string& interface);

    // Set the hop limit for multicast traffic on the socket. Each router hop
    // decreases this value by one, when it reaches zero the packet is
    // discarded.
    Result setMulticastHopLimit(int hopLimit);

    // Set the hop limit for unicast traffic on the socket. Each router hop
    // decreases this value by one, when it reaches zero the packet is
    // discarded.
    Result setUnicastHopLimit(int hopLimit);

    // Configure the socket to be transparent. This allows packets sent to have
    // a source address that is different from the network interface's source
    // address.
    Result setTransparent(bool transparent);

    /** Binding **/

    Result bind(const Address& address);

    /** Sending and receiving **/

    Result receive(Message* receivingMessage);
    Result receiveFrom(Message* receivingMessage, Address* from);

    Result send(const void* data, size_t size);

    // Send a packet to a specific |destination| of any address type.
    Result sendTo(const sockaddr& destination,
                  size_t destinationSize,
                  const void* data,
                  size_t size);
    // Convenience function to send to a specific IPv6 address.
    Result sendTo(const in6_addr& destination, const void* data, size_t size);
    // Convenience method to use sendTo with a more specific sockaddr struct
    // without having to specify the size or do the casting.
    template<typename T>
    Result sendTo(const T& destination, const void* data, size_t size) {
        return sendTo(*reinterpret_cast<const sockaddr*>(&destination),
                      sizeof(destination),
                      data,
                      size);
    }

    // Send a packet with a specific source IPv6 address to a given
    // |destination|. Rewriting the source in this manner usually requires root.
    Result sendFrom(const in6_addr& fromAddress,
                    const sockaddr& destination,
                    size_t destinationSize,
                    const void* data,
                    size_t size);
    Result sendFrom(const in6_addr& fromAddress,
                    const in6_addr& destination,
                    const void* data,
                    size_t size);
    // Convenience method to use sendFrom with a more specific sockaddr struct
    // without having to specify the size or do the casting.
    template<typename T>
    Result sendFrom(const in6_addr& fromAddress,
                    const T& destination,
                    const void* data,
                    size_t size) {
        return sendFrom(fromAddress,
                        *reinterpret_cast<const sockaddr*>(&destination),
                        sizeof(destination),
                        data,
                        size);
    }

private:
    // No copy construction or assignment allowed, support move semantics only
    Socket(const Socket&);
    Socket& operator=(const Socket&);

    enum class State {
        New,
        Open,
        Bound,
        Moved,
        Destructed,
    };

    State mState;
    int mSocket;
};

