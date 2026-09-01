# Reliable UDP Transport (Go-Back-N) — C++

A reliable, in-order file-transfer protocol built on top of UDP. UDP gives no
delivery or ordering guarantees; this rebuilds them in user space using a
sliding-window Go-Back-N ARQ.

## What it implements
- **Wire format** (`packet.h`): 14-byte header (type, length, seqno, ackno,
  checksum) in network byte order + payload. Integrity via a 16-bit Internet
  checksum.
- **Sender** (`sender.cpp`): sliding window of 8 packets, a single timer on the
  window base, **go-back-N** retransmission on timeout, FIN teardown. Includes a
  built-in **lossy channel** that drops/duplicates packets at a configurable
  rate for testing.
- **Receiver** (`receiver.cpp`): accepts strictly in-order packets, discards
  out-of-order/duplicate packets, replies with **cumulative ACKs** (next
  expected seqno), writes the output file.

## ACK convention
`ackno` = the next sequence number the receiver expects (it has everything with
`seqno < ackno`). The sender sets its window base to `ackno`.

## Build
```
g++ -std=c++17 -O2 sender.cpp   -o sender
g++ -std=c++17 -O2 receiver.cpp -o receiver
```

## Run manually
```
./receiver 9000 outfile.bin           # terminal 1
./sender 127.0.0.1 9000 infile.bin 0.05   # terminal 2 (5% loss)
```

## Automated reliability test
```
./run_test.sh <port> <loss_rate> <size_bytes>
./run_test.sh 9000 0.05 300000
```
Generates a random file, transfers it under simulated loss, and verifies the
received file is byte-for-byte identical (sha256). Verified passing at 5%, 15%,
and 30% loss.

## Notes / extensions
- Reordering is handled by the same in-order/discard logic (a dropped-then-
  retransmitted packet arrives out of order and is discarded until its turn).
- Multi-connection concurrency (one I/O thread + per-connection state) is the
  documented optional extension; this build is a clean single-transfer core.
