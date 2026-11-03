#pragma once
#include <stdint.h>
#include <string.h>
#include <helpers/ConfigSerializer.h>
#include <helpers/CommonRadioPrefs.h>
#include <helpers/DynamicConfigSerializer.h>

#define ADVERT_LOC_NONE       0
#define ADVERT_LOC_SHARE      1
#define ADVERT_LOC_PREFS      2

#define LOOP_DETECT_OFF       0
#define LOOP_DETECT_MINIMAL   1
#define LOOP_DETECT_MODERATE  2
#define LOOP_DETECT_STRICT    3

#define TELEM_MODE_DENY         0
#define TELEM_MODE_ALLOW_FLAGS  1
#define TELEM_MODE_ALLOW_ALL    2

// Companion power-saving profile (`pwr_mode`). Power saving is what lets the CPU
// of an ESP32 companion drop into light sleep between BLE connection events; the
// profile says how far to take it. AUTO is the default profile: on an ESP32 PS
// build it turns light sleep and DFS on, on other platforms it changes nothing.
#define POWER_MODE_AUTO         0   // default: sleep, with the USB guard
#define POWER_MODE_OFF          1   // no DFS, no sleep: maximum responsiveness, ~120 mA
#define POWER_MODE_CONSERVATIVE 2   // sleep only while the app is not connected
#define POWER_MODE_AGGRESSIVE   3   // longer idle yield, fewer wake-ups

#define POWER_MODE_MAX          POWER_MODE_AGGRESSIVE

// Default battery shutdown threshold (mV), overridable per build. 0 = disabled.
// A build flag outside 0 or 2000..4500 is sanitised back to 0, exactly like the
// runtime value, so a typo cannot lock a node out at an unusable voltage.
#ifndef SHUTDOWN_MV_DEFAULT
#define SHUTDOWN_MV_DEFAULT 0
#endif
#if SHUTDOWN_MV_DEFAULT != 0 && (SHUTDOWN_MV_DEFAULT < 2000 || SHUTDOWN_MV_DEFAULT > 4500)
#undef SHUTDOWN_MV_DEFAULT
#define SHUTDOWN_MV_DEFAULT 0
#endif

// One prefs object for every role (companion, repeater, room server, sensor) —
// upstream keeps two near-identical copies, we keep one. Persisted as JSON via
// ConfigSerializer (see DataStore::savePrefs / CommonCLI::savePrefs); the old
// binary layout is still *read* once, to migrate, by DataStore::loadPrefsInt().
//
// Serializer constraints, worth knowing before adding a field:
//   * keys are [A-Za-z0-9_], max 15 chars; unknown keys and groups are
//     skipped, so a file written by another firmware still loads;
//   * a renamed key keeps its old name as defLegacy(), read but never written;
//   * any single value, including a hex blob, must stay under 127 chars;
//   * group names mirror upstream's (radio/gps/repeat/room/power/bridge/comp)
//     so their future changes to those groups merge cleanly. Ours live in the
//     `ui` and `south` groups, which upstream will never touch.
class NodePrefs : public ConfigSerializer {
public:
  static uint16_t sanitizeShutdownMv(uint32_t mv) {
    return (mv == 0 || (mv >= 2000 && mv <= 4500)) ? (uint16_t)mv : 0;
  }
  // === Common fields ===
  float    airtime_factor = 0;
  char     node_name[32];
  float    freq = 0;
  float    bw = 0;
  uint8_t  sf = 0;
  uint8_t  cr = 0;
  int8_t   tx_power_dbm = 0;
  uint8_t  multi_acks = 0;
  float    rx_delay_base = 0;
  uint8_t  rx_boosted_gain = 0;
  uint8_t  radio_fem_rxgain = 0;  // FEM LNA (boards that can switch it)
  uint8_t  radio_fem_txgain = 0;  // FEM TX gain (low by default), upstream 1.17.1
  uint8_t  cad_enabled = 0;       // hardware Channel Activity Detection before TX
  uint8_t  path_hash_mode = 0;
  uint8_t  extra_sf[4];           // LR2021 side-detector SFs ('set extra.sf'); not persisted upstream either
  uint8_t  gps_enabled = 0;
  uint32_t gps_interval = 0;
  uint8_t  advert_loc_policy = 0;

  // === Repeater / CommonCLI fields ===
  double   node_lat = 0, node_lon = 0;
  char     password[16];
  uint8_t  disable_fwd = 0;
  uint8_t  advert_interval = 0;       // minutes / 2
  uint8_t  flood_advert_interval = 0; // hours
  float    tx_delay_factor = 0;
  char     guest_password[16];
  float    direct_tx_delay_factor = 0;
  uint32_t guard = 0;
  uint8_t  allow_read_only = 0;
  uint8_t  flood_max = 0;
  uint8_t  flood_max_unscoped = 0;
  uint8_t  flood_max_advert = 0;
  uint8_t  interference_threshold = 0;
  uint8_t  agc_reset_interval = 0;    // secs / 4
  uint8_t  bridge_enabled = 0;
  uint16_t bridge_delay = 0;          // milliseconds
  uint8_t  bridge_pkt_src = 0;        // 0=logTx, 1=logRx
  uint32_t bridge_baud = 0;
  uint8_t  bridge_channel = 0;        // 1-14 (ESP-NOW only)
  char     bridge_secret[16];
  uint8_t  powersaving_enabled = 0;
  // Companion power-saving profile. Deliberately a profile, not a switch: on an
  // ESP32 PS build AUTO turns power saving on (light sleep + DFS) and OFF returns
  // to the pre-feature behaviour (no DFS, no sleep). Prefs are stored by key in a
  // JSON file, so an old file simply has no value here (the field keeps its
  // default) and an older firmware ignores a key it does not know.
  uint8_t  power_mode = POWER_MODE_AUTO;
  // One configurable battery threshold (mV), used both as the boot lockout and
  // the runtime low-voltage shutdown. 0 = disabled (the default: opt-in, so a
  // LiFePO4/LTO/Na-ion node is never bricked by a Li-ion voltage). Valid range
  // is 0 or 2000..4500 mV.
  uint16_t shutdown_mv = SHUTDOWN_MV_DEFAULT;
  uint32_t discovery_mod_timestamp = 0;
  float    adc_multiplier = 0;
  char     owner_info[120];
  uint8_t  loop_detect = 0;

  // === Companion-only fields ===
  uint32_t ble_pin = 0;
  uint8_t  telemetry_mode_base = 0;
  uint8_t  telemetry_mode_loc = 0;
  uint8_t  telemetry_mode_env = 0;
  uint8_t  manual_add_contacts = 0;
  uint8_t  buzzer_quiet = 0;
  uint8_t  vibe_quiet = 0;
  uint8_t  autoadd_config = 0;
  uint8_t  client_repeat = 0;   // DEPRECATED, legacy binary only -> use isRepeatEn()
  uint8_t  autoadd_max_hops = 0;
  char     default_scope_name[31];
  uint8_t  default_scope_key[16];
  uint8_t  cyr2lat_channels = 0;
  uint8_t  cyr2lat_contacts = 0;
  uint8_t  ui_pm_clock_mode = 0;   // message popup mode: 0=Off, 1=PM/room, 2=All
  uint8_t  ui_clock_dim_mode = 0;
  uint8_t  ui_msg_hold = 1;   // MSG HOLD: 0 off, 1 5 s (default), 2 15 s, 3 30 s
  uint8_t  ui_display_rotation = 0; // 0-3 (GxEPD2 rotation, 3=default landscape)
  uint8_t  ui_max_unread_idx = 0;  // 0=16, 1=32, 2=64 messages in unread buffer
  uint8_t  ui_max_log_idx = 0;     // 0=16, 1=32, 2=64 messages in history log
  uint32_t ui_charge_uptime_base = 0; // accumulated seconds across soft-reboots
  uint8_t  ui_mcotxt_disabled = 0;   // 0 = decode/display/convert MCOtxt
  uint8_t  ui_mcmp_detect_off = 0;   // 0 = show/forward MCMP placeholders
  uint8_t  ui_aeic_detect_off = 0;   // 0 = show/forward AEIC placeholders
  uint8_t  ui_mcoimg_detect_off = 0; // 0 = show/forward MCOimg placeholders
  uint8_t  framed_packets_enabled = 1; // 0 = never fragment long companion frames
  int8_t   tz_offset = 0;
  uint8_t  ui_lang = 0;   // 0 = EN (default), 1 = RU
  uint8_t  ui_ble_prefix = 0;   // BLE_PREFIX_*: whether the BLE name starts with BLE_NAME_PREFIX
  char     cli_pin[9];          // remote CLI PIN, 8 hex; generated once (MyMesh::ensureCliPin)
  uint8_t  rcli_mode = 0;       // REMOTE_CLI_*: who may run CLI commands over the mesh
  uint8_t  rcli_fav = 0;        // 0 = a sender with the right PIN becomes a favourite, 1 = no
#ifdef WITH_COMMS_SWITCHING
  // App transport chosen at runtime (COMMS_MODE_* in WifiPrefs.h). Persisted on
  // every platform that can switch, including nRF52 where no WiFi exists.
  uint8_t  comms_mode = 0;
#endif
#if defined(WITH_COMMS_SWITCHING) && defined(ENABLE_WIFI_INTERFACE)
  // WiFi side of the uni transport. Network 1 is upstream's wifi.ssid /
  // wifi.pwd; the saved networks 2..5, the static IP and the TCP port have no
  // upstream equivalent and live here.
  char     wifi_ssid_x[4][33];
  char     wifi_pwd_x[4][64];
  uint8_t  wifi_ip_mode = 0;
  char     wifi_ip[16];
  char     wifi_gw[16];
  char     wifi_mask[16];
  uint16_t wifi_port = 0;
#endif
#ifdef ENABLE_WIFI_INTERFACE
  #ifndef WIFI_SSID
    #define WIFI_SSID ""
  #endif
  char wifi_ssid[33] = {0};   // if empty, the compile-time WIFI_SSID is used
  char wifi_pwd[64] = {0};
  uint8_t wifi_enabled = 1; // enabled by default to allow wifi only builds to work. wifi won't be started if ssid is empty
  // use ssid from prefs, or fallback to ssid from build flags
  const char* getWifiSSID() const { return wifi_ssid[0] ? wifi_ssid : WIFI_SSID; }
#endif
#ifdef UI_HAS_STATUS_LEDS
  uint8_t  ui_led_enabled = 1;       // status LEDs on/off (boards with UI_HAS_STATUS_LEDS)
  uint8_t  ui_led_msg_enabled = 1;   // unread-message breathing on the status LEDs
  uint8_t  ui_led_level = 1;         // status LED brightness step, 0 = dimmest
#endif

private:
  class RadioPrefs : public CommonRadioPrefs {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("freq", _parent->freq);
      def("bw", _parent->bw);
      def("sf", _parent->sf);
      def("cr", _parent->cr);
      def("cad", _parent->cad_enabled);
      def("int_thr", _parent->interference_threshold);
      def("rxgain", _parent->rx_boosted_gain);
      // upstream maps this back onto rx_boosted_gain, which leaves the FEM
      // setting unpersisted; the separate field is what CommonCLI actually uses.
      def("fem_rxgain", _parent->radio_fem_rxgain);
      def("fem_txgain", _parent->radio_fem_txgain);
      def("tx", _parent->tx_power_dbm);
      def("af", _parent->airtime_factor);
      def("rxdelay", _parent->rx_delay_base);
      def("f_txdelay", _parent->tx_delay_factor);
      def("d_txdelay", _parent->direct_tx_delay_factor);
      def("agc_int", _parent->agc_reset_interval);
      def("hash_mode", _parent->path_hash_mode);
      def("multi_ack", _parent->multi_acks);
    }
  public:
    RadioPrefs(NodePrefs* parent) : _parent(parent) { }

    // CommonRadioPrefs interface
    float getFreq() const override { return _parent->freq; }
    void setFreq(float f) override { _parent->freq = f; markDirty(); }
    float getBandwidth() const override { return _parent->bw; }
    void setBandwidth(float bw) override { _parent->bw = bw; markDirty(); }
    uint8_t getSpreadFactor() const override { return _parent->sf; }
    void setSpreadFactor(uint8_t sf) override { _parent->sf = sf; markDirty(); }
    uint8_t getCodingRate() const override { return _parent->cr; }
    void setCodingRate(uint8_t cr) override { _parent->cr = cr; markDirty(); }
    float getAirtimeFactor() const override { return _parent->airtime_factor; }
    void setAirtimeFactor(float af) override { _parent->airtime_factor = af; markDirty(); }
    bool isCadEnabled() const override { return _parent->cad_enabled; }
    void setCadEnabled(bool en) override { _parent->cad_enabled = en; markDirty(); }
    uint8_t getIntThresh() const override { return _parent->interference_threshold; }
    void setIntThresh(uint8_t t) override { _parent->interference_threshold = t; markDirty(); }
    uint8_t getRxGain() const override { return _parent->rx_boosted_gain; }
    void setRxGain(uint8_t g) override { _parent->rx_boosted_gain = g; markDirty(); }
    uint8_t getTxPower() const override { return _parent->tx_power_dbm; }
    void setTxPower(uint8_t dbm) override { _parent->tx_power_dbm = dbm; markDirty(); }
    float getRxDelay() const override { return _parent->rx_delay_base; }
    void setRxDelay(float d) override { _parent->rx_delay_base = d; markDirty(); }
    uint16_t getAgcResetInt() const override { return _parent->agc_reset_interval * 4; }
    void setAgcResetInt(uint16_t secs) override { _parent->agc_reset_interval = secs / 4; markDirty(); }
    uint8_t getHashMode() const override { return _parent->path_hash_mode; }
    void setHashMode(uint8_t m) override { _parent->path_hash_mode = m; markDirty(); }
    uint8_t getMultiAcks() const override { return _parent->multi_acks; }
    void setMultiAcks(uint8_t m) override { _parent->multi_acks = m; markDirty(); }
    float getFloodTxDelay() const override { return _parent->tx_delay_factor; }
    void setFloodTxDelay(float d) override { _parent->tx_delay_factor = d; markDirty(); }
    float getDirectTxDelay() const override { return _parent->direct_tx_delay_factor; }
    void setDirectTxDelay(float d) override { _parent->direct_tx_delay_factor = d; markDirty(); }
    uint8_t getFEMRxGain() const override { return _parent->radio_fem_rxgain; }
    void setFEMRxGain(uint8_t g) override { _parent->radio_fem_rxgain = g; markDirty(); }
    uint8_t getFEMTxGain() const override { return _parent->radio_fem_txgain; }
    void setFEMTxGain(uint8_t g) override { _parent->radio_fem_txgain = g; markDirty(); }
  };
  RadioPrefs radio;

  class BridgePrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("en", _parent->bridge_enabled);
      def("delay", _parent->bridge_delay);
      def("src", _parent->bridge_pkt_src);
      def("baud", _parent->bridge_baud);
      def("ch", _parent->bridge_channel);
      def("secret", _parent->bridge_secret, sizeof(_parent->bridge_secret));
    }
  public:
    BridgePrefs(NodePrefs* parent) : _parent(parent) { }
  };
  BridgePrefs bridge;

  class GPSPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("en", _parent->gps_enabled);
      def("int", _parent->gps_interval);
      def("adv_loc", _parent->advert_loc_policy);
    }
  public:
    GPSPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  GPSPrefs gps;

  class PowerPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("adc_mult", _parent->adc_multiplier);
      def("pwr_sav_en", _parent->powersaving_enabled);
      def("pwr_mode", _parent->power_mode);
      def("shutdown_mv", _parent->shutdown_mv);
    }
  public:
    PowerPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  PowerPrefs power;

  class RepeatPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("disable", _parent->disable_fwd);
      def("f_max", _parent->flood_max);
      def("f_max_uns", _parent->flood_max_unscoped);
      def("f_max_adv", _parent->flood_max_advert);
      def("loop", _parent->loop_detect);
    }
  public:
    RepeatPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  RepeatPrefs repeat;

  class RoomPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("rd_only", _parent->allow_read_only);
    }
  public:
    RoomPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  RoomPrefs room;

  class CompanionPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("auto_max", _parent->autoadd_max_hops);
      def("defs_nm", _parent->default_scope_name, sizeof(_parent->default_scope_name));
      def("defs_key", (void *) _parent->default_scope_key, sizeof(_parent->default_scope_key));
      def("pin", _parent->ble_pin);
      def("buzz_q", _parent->buzzer_quiet);
      def("vibe_q", _parent->vibe_quiet);
      def("auto_add", _parent->autoadd_config);
      def("man_add", _parent->manual_add_contacts);
      def("tel_base", _parent->telemetry_mode_base);
      def("tel_loc", _parent->telemetry_mode_loc);
      def("tel_env", _parent->telemetry_mode_env);
      def("tz_offset", _parent->tz_offset);
    }
  public:
    CompanionPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  CompanionPrefs companion;

  // UI settings of this fork. Upstream has no equivalent, so this whole group
  // is ours and will never collide on merge.
  class UIPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("rot", _parent->ui_display_rotation);
      def("pm_mode", _parent->ui_pm_clock_mode);
      def("dim_mode", _parent->ui_clock_dim_mode);
      def("msg_hold", _parent->ui_msg_hold);
      def("max_unread", _parent->ui_max_unread_idx);
      def("max_log", _parent->ui_max_log_idx);
      def("chg_uptime", _parent->ui_charge_uptime_base);
      def("mcotxt_off", _parent->ui_mcotxt_disabled);
      def("mcmp_det_off", _parent->ui_mcmp_detect_off);
      def("aeic_det_off", _parent->ui_aeic_detect_off);
      def("mcoimg_det_off", _parent->ui_mcoimg_detect_off);
      def("framed_pkt", _parent->framed_packets_enabled);
      def("lang", _parent->ui_lang);
      def("ble_prefix", _parent->ui_ble_prefix);
#ifdef UI_HAS_STATUS_LEDS
      def("led", _parent->ui_led_enabled);
      def("led_msg", _parent->ui_led_msg_enabled);
      def("led_lvl", _parent->ui_led_level);
#endif
    }
  public:
    UIPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  UIPrefs ui;

  // Non-UI fork settings. Also ours alone.
  class SouthPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("cyr_ch", _parent->cyr2lat_channels);
      def("cyr_ct", _parent->cyr2lat_contacts);
      // upstream reads this from the legacy binary but never writes it back
      def("disc_mod", _parent->discovery_mod_timestamp);
      def("cli_pin", _parent->cli_pin, sizeof(_parent->cli_pin));
      def("rcli_mode", _parent->rcli_mode);
      def("rcli_fav", _parent->rcli_fav);
#ifdef WITH_COMMS_SWITCHING
      def("comms", _parent->comms_mode);
#endif
#if defined(WITH_COMMS_SWITCHING) && defined(ENABLE_WIFI_INTERFACE)
      def("wifi2_ssid", _parent->wifi_ssid_x[0], sizeof(_parent->wifi_ssid_x[0]));
      def("wifi2_pwd",  _parent->wifi_pwd_x[0],  sizeof(_parent->wifi_pwd_x[0]));
      def("wifi3_ssid", _parent->wifi_ssid_x[1], sizeof(_parent->wifi_ssid_x[1]));
      def("wifi3_pwd",  _parent->wifi_pwd_x[1],  sizeof(_parent->wifi_pwd_x[1]));
      def("wifi4_ssid", _parent->wifi_ssid_x[2], sizeof(_parent->wifi_ssid_x[2]));
      def("wifi4_pwd",  _parent->wifi_pwd_x[2],  sizeof(_parent->wifi_pwd_x[2]));
      def("wifi5_ssid", _parent->wifi_ssid_x[3], sizeof(_parent->wifi_ssid_x[3]));
      def("wifi5_pwd",  _parent->wifi_pwd_x[3],  sizeof(_parent->wifi_pwd_x[3]));
      def("ip_mode", _parent->wifi_ip_mode);
      def("ip_addr", _parent->wifi_ip, sizeof(_parent->wifi_ip));
      def("ip_gw",   _parent->wifi_gw, sizeof(_parent->wifi_gw));
      def("ip_mask", _parent->wifi_mask, sizeof(_parent->wifi_mask));
      def("tcp_port", _parent->wifi_port);
#endif
    }
  public:
    SouthPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  SouthPrefs south;

  DynamicConfigSerializer custom;   // board/variant settings (upstream 1.18)

#ifdef ENABLE_WIFI_INTERFACE
  class WiFiPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("ssid", _parent->wifi_ssid, sizeof(_parent->wifi_ssid));
      def("pwd", _parent->wifi_pwd, sizeof(_parent->wifi_pwd));
      def("enabled", _parent->wifi_enabled);
    }
  public:
    WiFiPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  WiFiPrefs wifi;
#endif

protected:
  void structure() override {
    def("name", node_name, sizeof(node_name));
    def("pass", password, sizeof(password));
    def("guest", guest_password, sizeof(guest_password));
    def("owner", owner_info, sizeof(owner_info));
    def("adv_int", advert_interval);
    def("f_adv_int", flood_advert_interval);
    def("lat", node_lat);
    def("lon", node_lon);
    def("radio", radio);
    def("bridge", bridge);
    def("gps", gps);
    def("repeat", repeat);
    def("room", room);
    def("power", power);
    def("comp", companion);
    def("ui", ui);
    def("south", south);
    def("custom", custom);
#ifdef ENABLE_WIFI_INTERFACE
    def("wifi", wifi);
#endif
  }

public:
  NodePrefs() : ConfigSerializer(),
      radio(this), bridge(this), gps(this), power(this), repeat(this),
      room(this), companion(this), ui(this), south(this), custom(&radio)
#ifdef ENABLE_WIFI_INTERFACE
      , wifi(this)
#endif
  {
    node_name[0] = 0;
    password[0] = 0;
    guest_password[0] = 0;
    bridge_secret[0] = 0;
    owner_info[0] = 0;
    cli_pin[0] = 0;
#if defined(WITH_COMMS_SWITCHING) && defined(ENABLE_WIFI_INTERFACE)
    memset(wifi_ssid_x, 0, sizeof(wifi_ssid_x));
    memset(wifi_pwd_x, 0, sizeof(wifi_pwd_x));
    wifi_ip[0] = wifi_gw[0] = wifi_mask[0] = 0;
#endif
    default_scope_name[0] = 0;
    memset(default_scope_key, 0, sizeof(default_scope_key));
    memset(extra_sf, 0, sizeof(extra_sf));
  }

  // Companion "act as repeater" toggle. Upstream renamed the old client_repeat
  // field into repeat.disable_fwd (inverted); we already had disable_fwd for the
  // repeater role, so both roles share it and client_repeat survives only as the
  // legacy binary field the migration reads.
  bool isRepeatEn() const { return disable_fwd == 0; }
  void setRepeatEn(bool en) { disable_fwd = en ? 0 : 1; }

  CommonRadioPrefs* getRadioPrefs() { return &radio; }
  KeyValueStore* getCustom() { return &custom; }

  bool isDirty() const override { return ConfigSerializer::isDirty() || radio.isDirty() || custom.isDirty(); }
  void clearDirty() override { ConfigSerializer::clearDirty(); radio.clearDirty(); custom.clearDirty(); }
};

// BLE name: "MeshCore-<node name>" or bare "<node name>". Several nodes in one
// place otherwise show up as identical "MeshCore-" entries. Keeping the prefix
// stays the default (a prefs file without the key reads 0 = unset = on). Takes
// effect at boot: the name is handed to the BLE stack once in begin().
enum { BLE_PREFIX_UNSET = 0, BLE_PREFIX_ON = 1, BLE_PREFIX_OFF = 2 };

// Remote CLI over the mesh (`rcli_mode`). PIN: the command starts with the CLI
// PIN, from any contact. FLAG: upstream's per-contact "Remote CLI" permission,
// TXT_TYPE_CLI_COMMAND only. BOTH: either. A plain text message is never a
// command without the PIN, whatever the mode.
enum { REMOTE_CLI_PIN = 0, REMOTE_CLI_FLAG = 1, REMOTE_CLI_BOTH = 2, REMOTE_CLI_OFF = 3 };

static inline bool blePrefixEnabled(const NodePrefs* prefs) {
  return prefs == NULL || prefs->ui_ble_prefix != BLE_PREFIX_OFF;
}
