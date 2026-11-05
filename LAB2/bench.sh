N=${1:-4194304}
CORES=$(nproc)
for t in 1 2 3 4 $CORES 16 128 1024; do
    ./lab2 "$N" "$t" | grep threads=
done