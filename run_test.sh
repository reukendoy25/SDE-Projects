#!/bin/bash
# End-to-end reliability test: transfer a random file under simulated loss,
# then verify the received file is byte-for-byte identical.
set -e
PORT=${1:-9000}
LOSS=${2:-0.05}
SIZE=${3:-300000}

echo "Generating ${SIZE}-byte random test file..."
head -c "$SIZE" /dev/urandom > infile.bin

echo "Starting receiver (port $PORT)..."
./receiver "$PORT" outfile.bin &
RECV_PID=$!
sleep 0.4

echo "Sending with ${LOSS} packet loss..."
./sender 127.0.0.1 "$PORT" infile.bin "$LOSS"

wait $RECV_PID 2>/dev/null || true

echo ""
echo "--- Integrity check ---"
H1=$(sha256sum infile.bin | awk '{print $1}')
H2=$(sha256sum outfile.bin | awk '{print $1}')
echo "sent     sha256: $H1"
echo "received sha256: $H2"
if [ "$H1" = "$H2" ]; then
  echo "RESULT: ✅ IDENTICAL — reliable delivery verified under loss"
else
  echo "RESULT: ❌ MISMATCH"
  exit 1
fi
