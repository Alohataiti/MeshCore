#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
G=${1:?Pass the GoogleTest source directory}
C=${2:?Pass the Arduino Crypto library directory}
G=$(realpath "$G")
C=$(realpath "$C")
O=$(mktemp -d)
CRYPT=("$C/AES128.cpp" "$C/AESCommon.cpp" "$C/BlockCipher.cpp" "$C/Cipher.cpp" "$C/Crypto.cpp" "$C/Hash.cpp" "$C/SHA256.cpp")
g++ -std=c++17 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DBLACKLIST_CRYPTO_TESTS=1 -I "$C" -I src -I test/mocks -I "$G/include" -I "$G" \
  src/Utils.cpp test/test_blacklist/test_blacklist.cpp "${CRYPT[@]}" \
  "$G/src/gtest-all.cc" "$G/src/gtest_main.cc" -pthread -o "$O/test_blacklist"
"$O/test_blacklist"
printf 'Test binary: %s\n' "$O/test_blacklist"
