#pragma once

// Blacklist matching rules for the repeater, kept free of MyMesh so they can be unit tested.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <Utils.h>

#define MAX_PATH_PREFIX_LEN  4
#define MAX_PATH_HASH_LEN    3   // Mesh::sendFlood() allows path hashes of 1..3 bytes
#define BLACKLIST_REPLY_MAX  150 // serial reply is 160 bytes, remote CLI 158 with the 'xx|' prefix

struct BlacklistEntry {
  uint8_t len;                          // 0 = empty slot
  uint8_t prefix[MAX_PATH_PREFIX_LEN];
};

#define MAX_BLACKLIST_ENTRIES 16
#define MAX_CHAN_NAME_FILTERS 8

struct ChanNameFilter {
  uint8_t hash[PATH_HASH_SIZE];
  uint8_t secret[PUB_KEY_SIZE];
  char name[32];
};

inline bool blacklistDeriveNameFilter(ChanNameFilter& entry, const char* name) {
  size_t len = strlen(name);
  if (len < 2 || name[0] != '#' || len >= sizeof(entry.name)) return false;
  uint8_t hash[32];
  mesh::Utils::sha256(hash, sizeof(hash), (const uint8_t*)name, len);
  memset(&entry, 0, sizeof(entry));
  memcpy(entry.secret, hash, CIPHER_KEY_SIZE);
  mesh::Utils::sha256(entry.hash, sizeof(entry.hash), entry.secret, CIPHER_KEY_SIZE);
  memcpy(entry.name, name, len + 1);
  return true;
}

// Verify the MAC as well as the hash: different channels can share the same hash byte.
inline bool blacklistChannelNameMatches(const uint8_t* payload, size_t len,
                                        const ChanNameFilter* filters, int count) {
  if (len <= PATH_HASH_SIZE + CIPHER_MAC_SIZE || len > MAX_PACKET_PAYLOAD) return false;
  for (int i = 0; i < count; i++) {
    if (payload[0] != filters[i].hash[0]) continue;
    uint8_t plain[MAX_PACKET_PAYLOAD];
    if (mesh::Utils::MACThenDecrypt(filters[i].secret, plain, payload + PATH_HASH_SIZE,
                                   len - PATH_HASH_SIZE) > 0) return true;
  }
  return false;
}

// A path entry matches a hop when it is not longer than the hop hash (shorter entries match
// any hash size). Longer entries are skipped for packets with shorter hashes: truncating
// '81ab' to '81' would also drop traffic of every other node starting with 0x81.
// Returns the index of the matching entry, or -1.
inline int blacklistPathMatchIndex(const uint8_t* path, uint8_t hash_size, uint8_t hash_count,
                                   const BlacklistEntry* list, int n) {
  const uint8_t* ptr = path;
  for (uint8_t h = 0; h < hash_count; h++, ptr += hash_size) {
    for (int b = 0; b < n; b++) {
      if (list[b].len == 0 || list[b].len > hash_size) continue;
      if (memcmp(ptr, list[b].prefix, list[b].len) == 0) return b;
    }
  }
  return -1;
}

inline bool blacklistPathMatches(const uint8_t* path, uint8_t hash_size, uint8_t hash_count,
                                 const BlacklistEntry* list, int n) {
  return blacklistPathMatchIndex(path, hash_size, hash_count, list, n) >= 0;
}

// Key prefix match, e.g. the advertiser's public key at the start of an ADVERT payload.
inline int blacklistKeyMatchIndex(const uint8_t* key, size_t key_len, const BlacklistEntry* list, int n) {
  for (int b = 0; b < n; b++) {
    if (list[b].len == 0 || list[b].len > key_len) continue;
    if (memcmp(key, list[b].prefix, list[b].len) == 0) return b;
  }
  return -1;
}

inline bool blacklistKeyMatches(const uint8_t* key, size_t key_len, const BlacklistEntry* list, int n) {
  return blacklistKeyMatchIndex(key, key_len, list, n) >= 0;
}

// Group packets carry only a 1-byte channel hash, so only 1-byte entries can match.
// (Longer entries from older builds are kept in the list but never match.)
inline int blacklistChanHashMatchIndex(uint8_t channel_hash, const BlacklistEntry* list, int n) {
  for (int b = 0; b < n; b++) {
    if (list[b].len == 1 && list[b].prefix[0] == channel_hash) return b;
  }
  return -1;
}

inline bool blacklistChanHashMatches(uint8_t channel_hash, const BlacklistEntry* list, int n) {
  return blacklistChanHashMatchIndex(channel_hash, list, n) >= 0;
}

// Builds a one-entry-per-line reply that never exceeds 'max' bytes (incl. NUL);
// when entries do not fit, the reply ends with "..." instead.
struct BlacklistReply {
  char*  buf;
  size_t max;
  size_t len;
  int    count;
  bool   truncated;
};

inline void blacklistReplyInit(BlacklistReply& r, char* buf, size_t max) {
  r.buf = buf; r.max = max; r.len = 0; r.count = 0; r.truncated = false;
  if (max) buf[0] = 0;
}

inline bool blacklistReplyAdd(BlacklistReply& r, const char* text) {
  if (r.truncated) return false;
  size_t sep = r.count ? 1 : 0;
  size_t tlen = strlen(text);
  if (r.len + sep + tlen + 5 > r.max) {     // keep room for "\n..." + NUL
    if (r.max > r.len) {
      const char* mark = r.count ? "\n..." : "...";
      size_t available = r.max - r.len - 1;
      size_t n = strlen(mark);
      if (n > available) n = available;
      memcpy(r.buf + r.len, mark, n);
      r.len += n;
      r.buf[r.len] = 0;
    }
    r.truncated = true;
    return false;
  }
  if (sep) r.buf[r.len++] = '\n';
  memcpy(r.buf + r.len, text, tlen + 1);
  r.len += tlen;
  r.count++;
  return true;
}

inline void blacklistReplyAddHex(BlacklistReply& r, const BlacklistEntry& e) {
  char hex[MAX_PATH_PREFIX_LEN * 2 + 1];
  mesh::Utils::toHex(hex, e.prefix, e.len);
  hex[e.len * 2] = 0;
  blacklistReplyAdd(r, hex);
}

inline void blacklistReplyFinish(BlacklistReply& r) {
  if (r.count == 0 && !r.truncated && r.max) snprintf(r.buf, r.max, "-none-");
}

// CLI reply for 'blacklist <path|chan> <add|rem>' from the per-token results.
inline void blacklistFormatResult(bool any_ok, bool any_err, bool is_add, bool adverts_only, char* reply) {
  if (any_ok && !any_err)      strcpy(reply, "OK");
  else if (any_ok && any_err)  strcpy(reply, "OK (partial)");
  else                         strcpy(reply, is_add ? "Err - list full or bad input" : "Err - not found or bad input");
  if (any_ok && adverts_only)  strcat(reply, " - 4-byte entry: matches adverts only, not relayed packets");
}

// Parses one line of a blacklist file as a hex prefix. Lines that are not hex
// (e.g. "#name" channel entries in the same file) are rejected.
inline bool blacklistParseHex(const char* line, BlacklistEntry& out) {
  int len = strlen(line);
  if (len < 2 || (len % 2) != 0 || len / 2 > MAX_PATH_PREFIX_LEN) return false;
  for (int i = 0; i < len; i++) {
    if (!mesh::Utils::isHexChar(line[i])) return false;
  }
  uint8_t prefix[MAX_PATH_PREFIX_LEN];
  if (!mesh::Utils::fromHex(prefix, len / 2, line)) return false;
  out.len = (uint8_t)(len / 2);
  memcpy(out.prefix, prefix, out.len);
  return true;
}

// Validate the entire comma-separated argument before changing any list.
inline int blacklistSplitTokens(char* text, const char* parts[], int max_parts) {
  if (!text[0] || max_parts < 1) return 0;
  int count = 1;
  for (const char* p = text; *p; p++) {
    if (*p != ',') continue;
    if (p == text || p[-1] == ',' || !p[1] || ++count > max_parts) return 0;
  }
  return mesh::Utils::parseTextParts(text, parts, max_parts, ',');
}

// Splits a byte stream into lines (CR, LF or CRLF). feed() returns true when 'line' holds a
// complete, NUL-terminated non-empty line; pass c < 0 at the end of input to flush the last
// line. Overlong lines and lines containing NUL bytes are discarded as a whole.
template <int N>
struct BlacklistLineReader {
  char line[N];
  int  length = 0;   // length of the completed line
  int  pos = 0;
  bool invalid = false;

  bool feed(int c) {
    if (c < 0 || c == '\n' || c == '\r') {
      line[pos] = 0;
      length = pos;
      pos = 0;
      bool ready = length > 0 && !invalid;
      invalid = false;
      return ready;
    }
    if (c == 0 || pos == N - 1) invalid = true;
    else line[pos++] = (char)c;
    return false;
  }
};

enum BlacklistLineKind { BL_LINE_SKIP, BL_LINE_HEX, BL_LINE_NAME };

// Channel file line: "#name" (must fit name_max incl. NUL) or a hex channel hash.
inline BlacklistLineKind blacklistClassifyLine(const char* line, int length, int name_max, BlacklistEntry& hex_out) {
  if (line[0] == '#') return length > 1 && length < name_max ? BL_LINE_NAME : BL_LINE_SKIP;
  return blacklistParseHex(line, hex_out) ? BL_LINE_HEX : BL_LINE_SKIP;
}

// Loads a channel blacklist file char by char (feed(-1) at the end): hex lines go to 'hex'
// (up to hex_max), "#name" lines to on_name(name) (up to names_max). N = name buffer size + 1,
// so names that would not fit the name buffer are detected and skipped.
template <int N, typename OnName>
struct BlacklistChanLoader {
  BlacklistLineReader<N> rd;
  BlacklistEntry* hex;
  int hex_max, names_max;
  OnName on_name;
  int n_hex = 0, n_names = 0;

  BlacklistChanLoader(BlacklistEntry* hex_list, int hex_max_, int names_max_, OnName cb)
    : hex(hex_list), hex_max(hex_max_), names_max(names_max_), on_name(cb) { }

  void feed(int c) {
    if (!rd.feed(c)) return;
    BlacklistEntry e;
    switch (blacklistClassifyLine(rd.line, rd.length, N - 1, e)) {
      case BL_LINE_NAME:
        if (n_names < names_max) { on_name(rd.line); n_names++; }
        break;
      case BL_LINE_HEX:
        if (n_hex < hex_max) hex[n_hex++] = e;
        break;
      default:
        break;
    }
  }
};
