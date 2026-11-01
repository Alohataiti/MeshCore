# Blacklist tests

The normal native tests cover prefix matching, strict parsing, bounded CLI replies,
and the file line reader. The crypto tests additionally use the same AES and SHA256
implementation as firmware, rather than the native crypto mocks. They cover key
vectors, invalid MACs and two channel names with the same hash byte.

With g++, GoogleTest 1.17.0 and Arduino Crypto 0.4.0 installed, run from the repository:

```sh
bash test/test_blacklist/run_tests.sh /path/to/googletest /path/to/Crypto
```

The script enables address and undefined-behaviour sanitizers. The GoogleTest path
must contain `src/gtest-all.cc` and `include/gtest/gtest.h`. The Crypto path must
contain `AES128.cpp` and `SHA256.cpp`. Existing PlatformIO library installations can
be used. Crypto tests are enabled with `BLACKLIST_CRYPTO_TESTS` so the ordinary
native target continues to work with its crypto mocks.
