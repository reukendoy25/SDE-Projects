// ============================================================================
//  packet.h  -- on-the-wire format shared by sender and receiver.
//
//  Wire layout (14-byte header, all multi-byte fields in network byte order):
//     offset 0  : uint16 type     (DATA / ACK / SYN / FIN)
//     offset 2  : uint16 length   (payload bytes)
//     offset 4  : uint32 seqno    (sequence number of this packet)
//     offset 8  : uint32 ackno    (receiver's NEXT expected seqno)
//     offset 12 : uint16 checksum (16-bit one's-complement over header+payload)
//     offset 14 : payload bytes
//
//  ACK convention: ackno = "next expected sequence number", i.e. the receiver
//  has every packet with seqno < ackno. The sender sets window base = ackno.
// ============================================================================
#ifndef PACKET_H
#define PACKET_H

#include <cstdint>
#include <cstring>
#include <arpa/inet.h>

enum PktType : uint16_t { PT_DATA = 0, PT_ACK = 1, PT_SYN = 2, PT_FIN = 3 };

static const size_t HEADER_SIZE  = 14;
static const size_t MAX_PAYLOAD  = 1024;
static const size_t MAX_PACKET   = HEADER_SIZE + MAX_PAYLOAD;

struct Header {
    uint16_t type;
    uint16_t length;
    uint32_t seqno;
    uint32_t ackno;
    uint16_t checksum;   // filled in by serialize()
};

// 16-bit one's-complement checksum (the classic Internet checksum).
inline uint16_t checksum16(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < len; i += 2)
        sum += (uint32_t(data[i]) << 8) | data[i + 1];
    if (len & 1) sum += uint32_t(data[len - 1]) << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return uint16_t(~sum);
}

// Serialize header + payload into `out` (>= MAX_PACKET). Returns total bytes.
inline size_t serialize(const Header& h, const uint8_t* payload,
                        size_t plen, uint8_t* out) {
    uint16_t t  = htons(h.type);
    uint16_t l  = htons(uint16_t(plen));
    uint32_t s  = htonl(h.seqno);
    uint32_t a  = htonl(h.ackno);
    std::memcpy(out + 0,  &t, 2);
    std::memcpy(out + 2,  &l, 2);
    std::memcpy(out + 4,  &s, 4);
    std::memcpy(out + 8,  &a, 4);
    uint16_t zero = 0;
    std::memcpy(out + 12, &zero, 2);          // checksum placeholder = 0
    if (payload && plen) std::memcpy(out + HEADER_SIZE, payload, plen);

    size_t total = HEADER_SIZE + plen;
    uint16_t cs  = htons(checksum16(out, total));
    std::memcpy(out + 12, &cs, 2);            // patch in real checksum
    return total;
}

// Parse a received buffer. Returns false if too short or checksum fails.
inline bool deserialize(const uint8_t* in, size_t total, Header& h,
                        const uint8_t** payload, size_t& plen) {
    if (total < HEADER_SIZE) return false;

    uint16_t t, l, cs; uint32_t s, a;
    std::memcpy(&t, in + 0,  2); h.type     = ntohs(t);
    std::memcpy(&l, in + 2,  2); h.length   = ntohs(l);
    std::memcpy(&s, in + 4,  4); h.seqno    = ntohl(s);
    std::memcpy(&a, in + 8,  4); h.ackno    = ntohl(a);
    std::memcpy(&cs,in + 12, 2); h.checksum = ntohs(cs);

    // Verify: recompute over a copy with the checksum field zeroed.
    uint8_t tmp[MAX_PACKET];
    if (total > MAX_PACKET) return false;
    std::memcpy(tmp, in, total);
    uint16_t z = 0; std::memcpy(tmp + 12, &z, 2);
    if (checksum16(tmp, total) != h.checksum) return false;   // corrupted

    plen = h.length;
    if (HEADER_SIZE + plen > total) return false;
    *payload = in + HEADER_SIZE;
    return true;
}

#endif // PACKET_H
