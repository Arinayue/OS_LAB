set -e
make clean
make
printf 'out1.txt\nout2.txt\nabc\nxyz\nhello\nworld\n' | ./parent
test "$(cat out1.txt)" = "cba
olleh"
test "$(cat out2.txt)" = "zyx
dlrow"
ls /dev/shm | grep lab3 && exit 1
printf '/no/such/dir/a.txt\nout2.txt\nabc\nxyz\n' | rc=0
timeout 10 ./parent 2>/dev/null || rc=$?
test $rc -ne 124 || { echo "FAIL: parent hangs"; exit 1; }
test $rc -ne 0 || { echo "FAIL: exit code is 0"; exit 1; }
if ls /dev/shm | grep lab3; then echo "FAIL: leftover objects"; exit 1; fi
echo PASS
