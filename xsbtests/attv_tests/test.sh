#! /bin/sh

#============================================================================
echo "-------------------------------------------------------"
echo "--- Running attv_tests/test.sh                      ---"
echo "-------------------------------------------------------"

XEMU=$1
opts=$2

../gentest.sh "$XEMU $opts" attv_test "test."
../gentest.sh "$XEMU $opts" tabled_attv "test."
../gentest.sh "$XEMU $opts" interrupt1 "test."
../gentest.sh "$XEMU $opts" interrupt2 "test."
../gentest.sh "$XEMU $opts" pre_image "test."
../gentest.sh "$XEMU $opts" copyterm_attv "test."
../gentest.sh "$XEMU $opts" findall_attv "test."
../gentest.sh "$XEMU $opts" trie_intern_attv "test."
../gentest.sh "$XEMU $opts" trie_assert_attv "test."
