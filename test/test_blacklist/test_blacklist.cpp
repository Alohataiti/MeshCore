#include <gtest/gtest.h>
#include <helpers/BlacklistMatch.h>
#include <random>
#include <string>
#include <algorithm>
#include <vector>

static BlacklistEntry entry(const char* hex) {
  BlacklistEntry e = {};
  EXPECT_TRUE(blacklistParseHex(hex, e)) << hex;
  return e;
}

// ---- file / CLI parsing ----

TEST(BlacklistParse, AcceptsHexPrefixes) {
  BlacklistEntry e = {};
  ASSERT_TRUE(blacklistParseHex("81ab", e));
  EXPECT_EQ(2, e.len);
  EXPECT_EQ(0x81, e.prefix[0]);
  EXPECT_EQ(0xAB, e.prefix[1]);
}

TEST(BlacklistParse, RejectsChannelNamesAndJunk) {
  BlacklistEntry e = {};
  EXPECT_FALSE(blacklistParseHex("#bot", e));     // used to load as 0b00
  EXPECT_FALSE(blacklistParseHex("#xxx", e));     // used to load as 0000
  EXPECT_FALSE(blacklistParseHex("#flood", e));   // used to load as 0f000d
  EXPECT_FALSE(blacklistParseHex("test", e));     // missing '#'
  EXPECT_FALSE(blacklistParseHex("8", e));        // odd length
  EXPECT_FALSE(blacklistParseHex("", e));
  EXPECT_FALSE(blacklistParseHex("0102030405", e));  // longer than MAX_PATH_PREFIX_LEN
  EXPECT_EQ(0, e.len);                            // untouched on failure
}

// ---- path ----

TEST(BlacklistPath, ShortEntryMatchesAnyHashSize) {
  BlacklistEntry list[] = { entry("81") };
  const uint8_t one[] = { 0x11, 0x81 };
  const uint8_t two[] = { 0x11, 0x22, 0x81, 0xAB };
  EXPECT_TRUE(blacklistPathMatches(one, 1, 2, list, 1));
  EXPECT_TRUE(blacklistPathMatches(two, 2, 2, list, 1));
}

TEST(BlacklistPath, LongerEntryIsNotTruncated) {
  BlacklistEntry list[] = { entry("81ab") };
  const uint8_t one_byte_hops[] = { 0x81 };            // some other 0x81.. node
  const uint8_t two_byte_hit[]  = { 0x81, 0xAB };
  const uint8_t two_byte_miss[] = { 0x81, 0xAC };
  EXPECT_FALSE(blacklistPathMatches(one_byte_hops, 1, 1, list, 1));
  EXPECT_TRUE(blacklistPathMatches(two_byte_hit, 2, 1, list, 1));
  EXPECT_FALSE(blacklistPathMatches(two_byte_miss, 2, 1, list, 1));
}

TEST(BlacklistPath, EmptyPathAndEmptySlots) {
  BlacklistEntry list[2] = {};                         // len 0 = empty
  const uint8_t hops[] = { 0x00 };
  EXPECT_FALSE(blacklistPathMatches(hops, 1, 1, list, 2));
  BlacklistEntry l2[] = { entry("81") };
  EXPECT_FALSE(blacklistPathMatches(hops, 1, 0, l2, 1));
}

// ---- advert sender ----

TEST(BlacklistKey, MatchesAdvertiserKeyPrefix) {
  BlacklistEntry list[] = { entry("81f00d42") };
  uint8_t key[32] = { 0x81, 0xF0, 0x0D, 0x42 };
  EXPECT_TRUE(blacklistKeyMatches(key, sizeof(key), list, 1));
  key[3] = 0x43;
  EXPECT_FALSE(blacklistKeyMatches(key, sizeof(key), list, 1));
}

// ---- channel hash ----

TEST(BlacklistChan, OnlyOneByteEntriesMatch) {
  BlacklistEntry list[] = { entry("b5"), entry("98aa") };   // second: legacy multi-byte
  EXPECT_TRUE(blacklistChanHashMatches(0xB5, list, 2));
  EXPECT_FALSE(blacklistChanHashMatches(0x98, list, 2));
  EXPECT_FALSE(blacklistChanHashMatches(0x11, list, 2));
}

// ---- CLI reply ----

TEST(BlacklistReply, SuccessIsOk) {
  char reply[160];
  blacklistFormatResult(true, false, true, false, reply);
  EXPECT_STREQ("OK", reply);
  blacklistFormatResult(true, false, false, false, reply);   // rem
  EXPECT_STREQ("OK", reply);
}

TEST(BlacklistReply, PartialAndErrors) {
  char reply[160];
  blacklistFormatResult(true, true, true, false, reply);
  EXPECT_STREQ("OK (partial)", reply);
  blacklistFormatResult(false, true, true, false, reply);
  EXPECT_STREQ("Err - list full or bad input", reply);
  blacklistFormatResult(false, true, false, false, reply);
  EXPECT_STREQ("Err - not found or bad input", reply);
}

TEST(BlacklistReply, AdvertsOnlyNoteOnlyOnSuccess) {
  char reply[160];
  blacklistFormatResult(true, false, true, true, reply);
  EXPECT_EQ(0, strncmp(reply, "OK - 4-byte entry", 17));
  blacklistFormatResult(false, true, true, true, reply);
  EXPECT_STREQ("Err - list full or bad input", reply);
}

// ---- list reply (must fit the 160-byte CLI reply buffers) ----

TEST(BlacklistList, EmptyIsNone) {
  char reply[BLACKLIST_REPLY_MAX];
  BlacklistReply r;
  blacklistReplyInit(r, reply, sizeof(reply));
  blacklistReplyFinish(r);
  EXPECT_STREQ("-none-", reply);
}

TEST(BlacklistList, LinesJoinedWithNewline) {
  char reply[BLACKLIST_REPLY_MAX];
  BlacklistReply r;
  blacklistReplyInit(r, reply, sizeof(reply));
  blacklistReplyAddHex(r, entry("b5"));
  blacklistReplyAdd(r, "#flood");
  blacklistReplyFinish(r);
  EXPECT_STREQ("B5\n#flood", reply);   // toHex() prints upper case
}

TEST(BlacklistList, NeverExceedsMaxAndMarksTruncation) {
  // worst case: 16 hex entries + 8 names of 31 chars (used to reach ~178 bytes)
  char reply[BLACKLIST_REPLY_MAX + 32];
  memset(reply, 'Z', sizeof(reply));
  BlacklistReply r;
  blacklistReplyInit(r, reply, BLACKLIST_REPLY_MAX);
  for (int i = 0; i < 16; i++) blacklistReplyAddHex(r, entry("b5"));
  for (int i = 0; i < 8; i++) blacklistReplyAdd(r, "#abcdefghijklmnopqrstuvwxyz1234");
  blacklistReplyFinish(r);
  EXPECT_LT(strlen(reply), (size_t)BLACKLIST_REPLY_MAX);
  EXPECT_EQ('Z', reply[BLACKLIST_REPLY_MAX]);   // nothing written past the limit
  size_t n = strlen(reply);
  ASSERT_GE(n, 3u);
  EXPECT_STREQ("...", reply + n - 3);
}

TEST(BlacklistList, NamesOnlyNeverExceedMax) {
  // 8 names of 31 chars: the old MAX_PACKET_PAYLOAD-based bound let this reach 159+ bytes
  char reply[BLACKLIST_REPLY_MAX + 64];
  memset(reply, 'Z', sizeof(reply));
  BlacklistReply r;
  blacklistReplyInit(r, reply, BLACKLIST_REPLY_MAX);
  for (int i = 0; i < 8; i++) blacklistReplyAdd(r, "#abcdefghijklmnopqrstuvwxyz1234");
  blacklistReplyFinish(r);
  EXPECT_LT(strlen(reply), (size_t)BLACKLIST_REPLY_MAX);
  for (size_t i = BLACKLIST_REPLY_MAX; i < sizeof(reply); i++) EXPECT_EQ('Z', reply[i]) << i;
}

// ---- channel file loader pieces ----

struct LoadedChan { int hex = 0, names = 0; char first_name[40] = ""; BlacklistEntry list[16] = {}; };

// Same loader and limits as MyMesh::loadChanBlacklist (name[32], 16 hex, 8 names).
static LoadedChan loadChan(const char* text, int hex_max = 16, int names_max = 8) {
  LoadedChan out;
  auto on_name = [&out](const char* name) {
    if (out.names++ == 0) strcpy(out.first_name, name);
  };
  BlacklistChanLoader<33, decltype(on_name)> loader(out.list, hex_max, names_max, on_name);
  for (const char* p = text; ; p++) {
    int c = *p ? (unsigned char)*p : -1;
    loader.feed(c);
    if (c < 0) break;
  }
  out.hex = loader.n_hex;
  return out;
}

TEST(BlacklistLoad, NamesAndHexWithMixedLineEnds) {
  LoadedChan r = loadChan("#flood\r\nb5\n#xxx");    // last line without newline
  EXPECT_EQ(2, r.names);
  EXPECT_EQ(1, r.hex);
  EXPECT_STREQ("#flood", r.first_name);
}

TEST(BlacklistLoad, NameIsNeverReadAsHex) {
  LoadedChan r = loadChan("#bot\n#xxx\n");
  EXPECT_EQ(2, r.names);
  EXPECT_EQ(0, r.hex);
}

TEST(BlacklistLoad, OverlongNameSkipped) {
  // 31 chars fits name[32]; 32+ would be truncated on save and derive another key
  LoadedChan ok = loadChan("#abcdefghijklmnopqrstuvwxyz1234\n");
  LoadedChan bad = loadChan("#abcdefghijklmnopqrstuvwxyz12345678\nb5\n");
  EXPECT_EQ(1, ok.names);
  EXPECT_EQ(0, bad.names);
  EXPECT_EQ(1, bad.hex);                  // the next line is still read correctly
}

TEST(BlacklistLoad, EmptyAndJunkLines) {
  LoadedChan r = loadChan("\n\r\nzz\n8\n\n");
  EXPECT_EQ(0, r.names);
  EXPECT_EQ(0, r.hex);
}

TEST(BlacklistLoad, RespectsListLimits) {
  LoadedChan r = loadChan("#a1\n#a2\n#a3\nb5\nc6\nd7\n", 2, 2);
  EXPECT_EQ(2, r.names);
  EXPECT_EQ(2, r.hex);
  EXPECT_EQ(0xB5, r.list[0].prefix[0]);
  EXPECT_EQ(0xC6, r.list[1].prefix[0]);
}

TEST(BlacklistParse, FailureDoesNotPartiallyOverwriteEntry) {
  BlacklistEntry e = entry("abcd");
  EXPECT_FALSE(blacklistParseHex("1234zz", e));
  EXPECT_EQ(2, e.len);
  EXPECT_EQ(0xAB, e.prefix[0]);
  EXPECT_EQ(0xCD, e.prefix[1]);
}

TEST(BlacklistPath, MatchesOnlyWithinIndividualHops) {
  BlacklistEntry list[] = { entry("aabb") };
  uint8_t path[] = { 0x11, 0xAA, 0xBB, 0x22 };
  EXPECT_FALSE(blacklistPathMatches(path, 2, 2, list, 1));
  uint8_t three[] = { 0x11, 0x22, 0x33, 0xAA, 0xBB, 0x44 };
  EXPECT_TRUE(blacklistPathMatches(three, 3, 2, list, 1));
}

TEST(BlacklistPath, FourBytesMatchKeysButNotThreeByteHops) {
  BlacklistEntry list[] = { entry("aabbccdd") };
  uint8_t path[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0, 0 };
  EXPECT_FALSE(blacklistPathMatches(path, 3, 2, list, 1));
  EXPECT_TRUE(blacklistKeyMatches(path, sizeof(path), list, 1));
  EXPECT_FALSE(blacklistKeyMatches(path, 3, list, 1));
}

TEST(BlacklistPath, RandomPathsAgreeWithIndependentModel) {
  std::mt19937 rng(42719);
  for (int iteration = 0; iteration < 5000; iteration++) {
    uint8_t path[64];
    for (auto& b : path) b = rng() % 8;
    BlacklistEntry entries[16] = {};
    for (auto& e : entries) {
      e.len = rng() % 5;
      for (auto& b : e.prefix) b = rng() % 8;
    }
    int size = 1 + rng() % 3;
    int count = rng() % (64 / size + 1);
    bool expected = false;
    for (int hop = 0; hop < count; hop++) {
      for (const auto& e : entries) {
        if (!e.len || e.len > size) continue;
        bool equal = true;
        for (int k = 0; k < e.len; k++) equal &= e.prefix[k] == path[hop * size + k];
        expected |= equal;
      }
    }
    EXPECT_EQ(expected, blacklistPathMatches(path, size, count, entries, 16)) << iteration;
  }
}

TEST(BlacklistList, TinyBuffersKeepCanariesIntact) {
  for (size_t size = 0; size < 12; size++) {
    char reply[32];
    memset(reply, 'Z', sizeof(reply));
    BlacklistReply r;
    blacklistReplyInit(r, reply, size);
    blacklistReplyAdd(r, "#long_channel");
    blacklistReplyFinish(r);
    if (size) EXPECT_LT(strlen(reply), size);
    for (size_t i = size; i < sizeof(reply); i++) EXPECT_EQ('Z', reply[i]);
    memset(reply, 'Z', sizeof(reply));
    blacklistReplyInit(r, reply, size);
    blacklistReplyFinish(r);
    if (size) EXPECT_LT(strlen(reply), size);
    for (size_t i = size; i < sizeof(reply); i++) EXPECT_EQ('Z', reply[i]);
  }
}

TEST(BlacklistTokens, RejectsEmptyAndExcessArgumentsBeforeSplitting) {
  const char* parts[16];
  for (const char* input : {"", ",81", "81,", "81,,82"}) {
    char text[160];
    strcpy(text, input);
    EXPECT_EQ(0, blacklistSplitTokens(text, parts, 16));
  }
  std::string valid = "00";
  for (int i = 1; i < 16; i++) valid += ",00";
  char text[160];
  strcpy(text, valid.c_str());
  EXPECT_EQ(16, blacklistSplitTokens(text, parts, 16));
  valid += ",01";
  strcpy(text, valid.c_str());
  EXPECT_EQ(0, blacklistSplitTokens(text, parts, 16));
  EXPECT_STREQ(valid.c_str(), text);
}

TEST(BlacklistLoad, OverlongHexAndEmbeddedNulAreDiscarded) {
  BlacklistEntry list[16] = {};
  int names = 0;
  auto name = [&](const char*) { names++; };
  BlacklistChanLoader<33, decltype(name)> loader(list, 16, 8, name);
  const char data[] = "81\0abcd\n#bad\0name\n#\n";
  for (size_t i = 0; i < sizeof(data) - 1; i++) loader.feed((unsigned char)data[i]);
  std::string overlong(100, '8');
  for (char c : overlong) loader.feed(c);
  loader.feed('\n');
  for (char c : std::string("82")) loader.feed(c);
  loader.feed(-1);
  EXPECT_EQ(0, names);
  EXPECT_EQ(1, loader.n_hex);
  EXPECT_EQ(0x82, list[0].prefix[0]);
}

TEST(BlacklistLoad, PathLineReaderDoesNotTruncateIntoValidPrefix) {
  BlacklistLineReader<9> reader;
  int accepted = 0;
  const char* data = "aabbccdd00\n81\r\n";
  for (; *data; data++) {
    if (reader.feed(*data)) {
      BlacklistEntry e;
      ASSERT_TRUE(blacklistParseHex(reader.line, e));
      EXPECT_EQ(1, e.len);
      accepted++;
    }
  }
  EXPECT_EQ(1, accepted);
}

#ifdef BLACKLIST_CRYPTO_TESTS
TEST(BlacklistNames, KeyDerivationMatchesKnownSha256Vector) {
  ChanNameFilter filter;
  ASSERT_TRUE(blacklistDeriveNameFilter(filter, "#blacklist-test"));
  const uint8_t expected[16] = {0x4c, 0xf7, 0x89, 0x2, 0xac, 0xae, 0x45, 0xc7, 0x3b, 0xee, 0xc3, 0x8c, 0xd7, 0x7c, 0xe0, 0x89};
  EXPECT_EQ(0, memcmp(filter.secret, expected, sizeof(expected)));
  for (int i = 16; i < PUB_KEY_SIZE; i++) EXPECT_EQ(0, filter.secret[i]);
  EXPECT_EQ(0x99, filter.hash[0]);
  EXPECT_STREQ("#blacklist-test", filter.name);
}

static std::vector<uint8_t> groupPayload(const ChanNameFilter& filter) {
  uint8_t plain[] = {0x11, 0x22, 0x33, 0x44, 0x00, 't', 'e', 's', 't'};
  std::vector<uint8_t> out(MAX_PACKET_PAYLOAD);
  out[0] = filter.hash[0];
  int n = mesh::Utils::encryptThenMAC(filter.secret, out.data() + PATH_HASH_SIZE,
                                    plain, sizeof(plain));
  out.resize(PATH_HASH_SIZE + n);
  return out;
}

TEST(BlacklistNames, BothGroupPayloadsRequireTheCorrectMac) {
  ChanNameFilter filter;
  ASSERT_TRUE(blacklistDeriveNameFilter(filter, "#blacklist-test"));
  auto payload = groupPayload(filter);
  EXPECT_TRUE(blacklistChannelNameMatches(payload.data(), payload.size(), &filter, 1));
  payload[PATH_HASH_SIZE] ^= 1;
  EXPECT_FALSE(blacklistChannelNameMatches(payload.data(), payload.size(), &filter, 1));
}

TEST(BlacklistNames, CollidingHashDoesNotBlockAnotherChannel) {
  ChanNameFilter blocked, other;
  ASSERT_TRUE(blacklistDeriveNameFilter(blocked, "#blacklist-test"));
  bool found = false;
  for (int i = 0; i < 20000; i++) {
    std::string name = "#collision" + std::to_string(i);
    ASSERT_TRUE(blacklistDeriveNameFilter(other, name.c_str()));
    if (other.hash[0] == blocked.hash[0]) { found = true; break; }
  }
  ASSERT_TRUE(found);
  auto payload = groupPayload(other);
  EXPECT_FALSE(blacklistChannelNameMatches(payload.data(), payload.size(), &blocked, 1));
  EXPECT_TRUE(blacklistChannelNameMatches(payload.data(), payload.size(), &other, 1));
}

TEST(BlacklistNames, RejectsMalformedLengthsAndInvalidNames) {
  ChanNameFilter filter;
  EXPECT_FALSE(blacklistDeriveNameFilter(filter, "#"));
  EXPECT_FALSE(blacklistDeriveNameFilter(filter, "public"));
  EXPECT_FALSE(blacklistDeriveNameFilter(filter, "#abcdefghijklmnopqrstuvwxyz12345"));
  ASSERT_TRUE(blacklistDeriveNameFilter(filter, "#blacklist-test"));
  auto payload = groupPayload(filter);
  EXPECT_FALSE(blacklistChannelNameMatches(payload.data(), 0, &filter, 1));
  EXPECT_FALSE(blacklistChannelNameMatches(payload.data(), PATH_HASH_SIZE + CIPHER_MAC_SIZE, &filter, 1));
  EXPECT_FALSE(blacklistChannelNameMatches(payload.data(), payload.size() - 1, &filter, 1));
  EXPECT_FALSE(blacklistChannelNameMatches(payload.data(), MAX_PACKET_PAYLOAD + 1, &filter, 1));
  EXPECT_FALSE(blacklistChannelNameMatches(payload.data(), payload.size(), &filter, 0));
}
#endif
