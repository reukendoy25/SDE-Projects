// ============================================================================
//  sender.cpp -- Go-Back-N sliding-window sender over UDP.
//
//  Reads a file, splits it into DATA packets, and reliably delivers them using
//  a sliding window of WINDOW packets with a single timer on the base packet.
//  On timeout it retransmits the whole outstanding window (go-back-N). A FIN
//  marks end-of-stream.
//
//  A built-in "lossy channel" (lossy_send) drops/duplicates outgoing DATA
//  packets at a configurable rate so reliability can be tested without a real
//  flaky network. ACKs are sent back over the same UDP socket by the receiver.
//
//  Build: g++ -std=c++17 -O2 sender.cpp -o sender
//  Run:   ./sender <receiver_ip> <port> <file> <loss_rate 0..1>
// ============================================================================
#include "packet.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>

static const int      WINDOW     = 8;       // max packets in flight
static const long     TIMEOUT_US = 100000;  // 100 ms retransmission timeout

static double g_loss = 0.0;                  // drop probability for DATA
static long   g_drops = 0, g_dupes = 0, g_retx = 0, g_sent = 0;

// Unreliable channel: with prob g_loss drop the packet; with small prob send a
// duplicate; otherwise send once. Reordering is exercised too, since a drop +
// later retransmit makes packets arrive out of order at the receiver.
void lossy_send(int sock, sockaddr_in& to, const uint8_t* buf, size_t len) {
    double x = (double)rand() / RAND_MAX;
    if (x < g_loss) { g_drops++; return; }                 // dropped
    sendto(sock, buf, len, 0, (sockaddr*)&to, sizeof(to));
    g_sent++;
    if (((double)rand() / RAND_MAX) < 0.02) {              // 2% duplicate
        sendto(sock, buf, len, 0, (sockaddr*)&to, sizeof(to));
        g_dupes++;
    }
}

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: " << argv[0]
                  << " <receiver_ip> <port> <file> <loss_rate>\n";
        return 1;
    }
    const char* ip   = argv[1];
    int         port = atoi(argv[2]);
    const char* path = argv[3];
    g_loss = atof(argv[4]);
    srand((unsigned)time(nullptr) ^ getpid());

    // Read entire file, split into chunks of MAX_PAYLOAD.
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "cannot open " << path << "\n"; return 1; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    size_t total_bytes = data.size();
    uint32_t M = (uint32_t)((total_bytes + MAX_PAYLOAD - 1) / MAX_PAYLOAD); // #packets

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in dst{}; dst.sin_family = AF_INET; dst.sin_port = htons(port);
    inet_pton(AF_INET, ip, &dst.sin_addr);

    auto build_data = [&](uint32_t seq, uint8_t* out) -> size_t {
        size_t off = (size_t)seq * MAX_PAYLOAD;
        size_t plen = std::min(MAX_PAYLOAD, total_bytes - off);
        Header h{}; h.type = PT_DATA; h.seqno = seq; h.ackno = 0;
        return serialize(h, data.data() + off, plen, out);
    };

    std::cout << "Sender: " << total_bytes << " bytes -> " << M
              << " packets, window=" << WINDOW
              << ", loss=" << g_loss << "\n";

    uint32_t base = 0, next = 0;
    uint8_t  pkt[MAX_PACKET], rbuf[MAX_PACKET];

    while (base < M) {
        // Fill the window with not-yet-sent packets.
        while (next < base + WINDOW && next < M) {
            size_t n = build_data(next, pkt);
            lossy_send(sock, dst, pkt, n);
            next++;
        }
        // Wait for an ACK (cumulative) up to the timeout.
        fd_set rfds; FD_ZERO(&rfds); FD_SET(sock, &rfds);
        timeval tv{0, TIMEOUT_US};
        int rv = select(sock + 1, &rfds, nullptr, nullptr, &tv);
        if (rv > 0) {
            sockaddr_in from{}; socklen_t fl = sizeof(from);
            ssize_t r = recvfrom(sock, rbuf, sizeof(rbuf), 0,
                                 (sockaddr*)&from, &fl);
            Header h{}; const uint8_t* pl; size_t plen;
            if (r > 0 && deserialize(rbuf, r, h, &pl, plen) && h.type == PT_ACK) {
                if (h.ackno > base) base = h.ackno;        // slide the window
            }
        } else {
            // Timeout: GO BACK N -- resend the whole outstanding window.
            for (uint32_t s = base; s < next; ++s) {
                size_t n = build_data(s, pkt);
                lossy_send(sock, dst, pkt, n);
                g_retx++;
            }
        }
    }

    // Tear down: send FIN until the receiver acknowledges it (or we give up).
    Header fin{}; fin.type = PT_FIN; fin.seqno = M; fin.ackno = 0;
    size_t fn = serialize(fin, nullptr, 0, pkt);
    for (int attempt = 0; attempt < 20; ++attempt) {
        sendto(sock, pkt, fn, 0, (sockaddr*)&dst, sizeof(dst));
        fd_set rfds; FD_ZERO(&rfds); FD_SET(sock, &rfds);
        timeval tv{0, TIMEOUT_US};
        if (select(sock + 1, &rfds, nullptr, nullptr, &tv) > 0) {
            sockaddr_in from{}; socklen_t fl = sizeof(from);
            ssize_t r = recvfrom(sock, rbuf, sizeof(rbuf), 0,
                                 (sockaddr*)&from, &fl);
            Header h{}; const uint8_t* pl; size_t plen;
            if (r > 0 && deserialize(rbuf, r, h, &pl, plen) &&
                h.type == PT_ACK && h.ackno >= M + 1) break;   // FIN acked
        }
    }

    std::cout << "Done. data_sent=" << g_sent << " retransmissions=" << g_retx
              << " simulated_drops=" << g_drops << " duplicates=" << g_dupes
              << "\n";
    close(sock);
    return 0;
}
