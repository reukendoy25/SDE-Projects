// ============================================================================
//  receiver.cpp -- Go-Back-N receiver over UDP.
//
//  Accepts packets strictly in order. Out-of-order or duplicate packets are
//  discarded. After every DATA packet it returns a cumulative ACK carrying the
//  NEXT expected sequence number (so the sender learns the highest in-order
//  byte it can slide past). A FIN ends the transfer.
//
//  Build: g++ -std=c++17 -O2 receiver.cpp -o receiver
//  Run:   ./receiver <port> <output_file>
// ============================================================================
#include "packet.h"
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <port> <output_file>\n";
        return 1;
    }
    int port = atoi(argv[1]);
    std::ofstream out(argv[2], std::ios::binary);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{}; addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons(port);
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "bind failed\n"; return 1;
    }
    std::cout << "Receiver listening on port " << port << "\n";

    uint32_t expected = 0;            // next in-order seqno we want
    long received = 0, discarded = 0; // stats
    uint8_t buf[MAX_PACKET];

    auto send_ack = [&](sockaddr_in& to, socklen_t tl, uint32_t ackno) {
        Header h{}; h.type = PT_ACK; h.seqno = 0; h.ackno = ackno;
        uint8_t a[MAX_PACKET];
        size_t n = serialize(h, nullptr, 0, a);
        sendto(sock, a, n, 0, (sockaddr*)&to, tl);
    };

    int idle = 0;
    while (true) {
        // Safety net so the process can't hang forever if the FIN is lost.
        fd_set rfds; FD_ZERO(&rfds); FD_SET(sock, &rfds);
        timeval tv{2, 0};
        int rv = select(sock + 1, &rfds, nullptr, nullptr, &tv);
        if (rv == 0) { if (++idle >= 3 && expected > 0) break; else continue; }
        idle = 0;

        sockaddr_in from{}; socklen_t fl = sizeof(from);
        ssize_t r = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&from, &fl);
        if (r <= 0) continue;

        Header h{}; const uint8_t* pl; size_t plen;
        if (!deserialize(buf, r, h, &pl, plen)) { continue; } // corrupt -> drop

        if (h.type == PT_FIN) {
            send_ack(from, fl, expected + 1);   // FIN-ACK
            break;
        }
        if (h.type == PT_DATA) {
            if (h.seqno == expected) {           // in order -> deliver
                out.write((const char*)pl, plen);
                expected++; received++;
            } else {
                discarded++;                     // out-of-order/duplicate -> drop
            }
            send_ack(from, fl, expected);        // cumulative ACK = next expected
        }
    }

    out.flush();
    std::cout << "Done. in_order_packets=" << expected
              << " delivered=" << received
              << " discarded(dup/out-of-order)=" << discarded << "\n";
    close(sock);
    return 0;
}
