#pragma once

#include <linux/rtnetlink.h>

template<class Request>
inline void addRouterAttribute(Request& r,
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
