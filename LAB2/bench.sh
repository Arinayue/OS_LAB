#!/bin/sh
SIZE=${1:-1048576}
CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu)
echo "size=$SIZE cores=$CORES"
for t in 1 2 3 4 $CORES 16 128 1024; do
	./lab2 "$SIZE" "$t" | grep threads=
done
