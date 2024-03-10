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

#include "socket.h"

#include <errno.h>
#include <string.h>

#include <linux/in6.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "address.h"
#include "message.h"

Socket::Socket() : mState(State::New), mSocket(-1) {
}

Socket::Socket(Socket&& other) noexcept : mState(other.mState), mSocket(other.mSocket) {
    other.mSocket = -1;
    other.mState = State::Moved;
}

Socket::~Socket() {
    if (mSocket != -1) {
        close(mSocket);
        mSocket = -1;
    }
    mState = State::Destructed;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (mSocket != -1) {
        close(mSocket);
    }
    mSocket = other.mSocket;
    mState = other.mState;
    other.mSocket = -1;
    other.mState = State::Moved;

    return *this;
}

Result Socket::open(int domain, int type, int protocol) {
    if (mState != State::New) {
        return Result::error("open called on socket in invalid state");
    }
    mSocket = ::socket(domain, type | SOCK_CLOEXEC, protocol);
    if (mSocket == -1) {
        return Result::error(strerror(errno));
    }
    mState = State::Open;
    return Result::success();
}

Result Socket::setInterface(const std::string& interface) {
    if (mState != State::Open) {
        return Result::error("attempting to set option in invalid state");
    }
    int res = ::setsockopt(mSocket, SOL_SOCKET, SO_BINDTODEVICE,
                           interface.c_str(), interface.size());

    return res == -1 ? Result::error(strerror(errno)) : Result::success();
}

Result Socket::setMulticastHopLimit(int hopLimit) {
    if (mState != State::Open) {
        return Result::error("attempting to set option in invalid state");
    }
    int res = ::setsockopt(mSocket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                           &hopLimit, sizeof(hopLimit));

    return res == -1 ? Result::error(strerror(errno)) : Result::success();
}

Result Socket::setUnicastHopLimit(int hopLimit) {
    if (mState != State::Open) {
        return Result::error("attempting to set option in invalid state");
    }
    int res = ::setsockopt(mSocket, IPPROTO_IPV6, IPV6_UNICAST_HOPS,
                           &hopLimit, sizeof(hopLimit));

    return res == -1 ? Result::error(strerror(errno)) : Result::success();
}

Result Socket::setTransparent(bool transparent) {
    if (mState != State::Open) {
        return Result::error("attempting to set option in invalid state");
    }
    int v = transparent ? 1 : 0;
    int res = ::setsockopt(mSocket, IPPROTO_IPV6, IPV6_TRANSPARENT,
                           &v, sizeof(v));

    return res == -1 ? Result::error(strerror(errno)) : Result::success();
}

Result Socket::bind(const Address& address) {
    if (mState != State::Open) {
        return Result::error("bind called on socket in invalid state");
    }

    int res = ::bind(mSocket, address.get<sockaddr>(), address.size());
    if (res == -1) {
        return Result::error(strerror(errno));
    }

    mState = State::Bound;
    return Result::success();
}

Result Socket::receive(Message* receivingMessage) {
    if (receivingMessage == nullptr) {
        return Result::error("No receivingMessage provided");
    }
    if (mState != State::Bound) {
        return Result::error("Attempt to receive on a socket that isn't bound");
    }

    ssize_t rxBytes = ::recv(mSocket,
                             receivingMessage->data(),
                             receivingMessage->capacity(),
                             0);
    if (rxBytes < 0) {
        return Result::error(strerror(errno));
    }

    receivingMessage->setSize(static_cast<size_t>(rxBytes));
    return Result::success();
}

Result Socket::receiveFrom(Message* receivingMessage, Address* from) {
    if (receivingMessage == nullptr) {
        return Result::error("No receivingMessage provided");
    }
    if (from == nullptr) {
        return Result::error("No from address provided");
    }
    if (mState != State::Bound) {
        return Result::error("Attempt to receive on a socket that isn't bound");
    }

    from->reset();
    sockaddr* source = from->get<sockaddr>();
    socklen_t sourceLen = from->size();
    ssize_t rxBytes = ::recvfrom(mSocket,
                                 receivingMessage->data(),
                                 receivingMessage->capacity(),
                                 0,
                                 source,
                                 &sourceLen);
    if (rxBytes < 0) {
        return Result::error(strerror(errno));
    }

    receivingMessage->setSize(static_cast<size_t>(rxBytes));
    return Result::success();
}

Result Socket::send(const void* data, size_t size) {
    if (mState != State::Bound && mState != State::Open) {
        return Result::error("Attempt to send on a socket in invalid state");
    }

    int res = ::send(mSocket, data, size, 0);
    if (res == -1) {
        return Result::error(strerror(errno));
    }
    return Result::success();
}

Result Socket::sendTo(const sockaddr& destination,
                      size_t destinationSize,
                      const void* data,
                      size_t size) {
    if (mState != State::Bound && mState != State::Open) {
        return Result::error("Attempt to send on a socket in invalid state");
    }

    int res = ::sendto(mSocket, data, size, 0, &destination, destinationSize);
    if (res == -1) {
        return Result::error(strerror(errno));
    }
    return Result::success();
}

Result Socket::sendTo(const in6_addr& destination,
                      const void* data,
                      size_t size) {
    sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = destination;
    return sendTo(*reinterpret_cast<sockaddr*>(&addr),
                  sizeof(addr),
                  data,
                  size);
}

Result Socket::sendFrom(const struct in6_addr& fromAddress,
                        const sockaddr& destination,
                        size_t destinationSize,
                        const void* data,
                        size_t size) {
    struct msghdr messageHeader;
    memset(&messageHeader, 0, sizeof(messageHeader));
    // Even when sending this struct requires a non-const pointer, even when
    // it's only going to be read. Do a const_cast instead of creating a
    // method signature with illogical const-behavior.
    messageHeader.msg_name = const_cast<struct sockaddr*>(&destination);
    messageHeader.msg_namelen = destinationSize;

    struct iovec iov;
    messageHeader.msg_iov = &iov;
    messageHeader.msg_iovlen = 1;

    memset(&iov, 0, sizeof(iov));
    iov.iov_base = const_cast<void*>(data);
    iov.iov_len = size;

    char control[CMSG_SPACE(sizeof(struct in6_pktinfo))] = { 0 };
    messageHeader.msg_control = control;
    messageHeader.msg_controllen = sizeof(control);

    struct cmsghdr* controlHeader = CMSG_FIRSTHDR(&messageHeader);
    controlHeader->cmsg_level = IPPROTO_IPV6;
    controlHeader->cmsg_type = IPV6_PKTINFO;
    controlHeader->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));

    auto packetInfoData = CMSG_DATA(controlHeader);
    auto packetInfo = reinterpret_cast<struct in6_pktinfo*>(packetInfoData);
    packetInfo->ipi6_addr = fromAddress;

    int res = ::sendmsg(mSocket, &messageHeader, 0);
    if (res == -1) {
        int error = errno;
        printf("sendmsg failed: %d\n", error);
        return Result::error(strerror(error));
    }
    return Result::success();
}

Result Socket::sendFrom(const in6_addr& fromAddress,
                        const in6_addr& destination,
                        const void* data,
                        size_t size) {
    sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = destination;

    return sendFrom(fromAddress,
                    *reinterpret_cast<sockaddr*>(&addr),
                    sizeof(addr),
                    data,
                    size);
}
