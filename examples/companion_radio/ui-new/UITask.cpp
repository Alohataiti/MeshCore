#include "UITask.h"
#include "UIExtensions.h"
#include <helpers/TxtDataHelpers.h>
#include <helpers/BatteryPack.h>
#include <helpers/LowBatteryGuard.h>
#include "../MyMesh.h"
#include "../AdcCalibration.h"
#include "../PowerScreens.h"
#include "../UILang.h"
#include "target.h"
#include <time.h>
#if defined(WIFI_SSID) || defined(ENABLE_WIFI_INTERFACE)
  #include <WiFi.h>
#endif

// How long a message that just came in stays on screen when the connected app
// fetches it at once (Settings → MSG HOLD): reading it in the app is no reason
// to pull it off the device's display a second after it appeared. Off = return
// home as soon as the queue is empty, as before.
static unsigned long msgHoldMillis(const NodePrefs* prefs) {
  static const uint8_t secs[4] = { 0, 5, 15, 30 };
  const uint8_t v = prefs ? prefs->ui_msg_hold : 1;
  return secs[v < 4 ? v : 1] * 1000UL;
}

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif

// Truncate UTF-8 string to at most max_bytes, never cutting in the middle of a multibyte sequence
static void utf8_truncate(char* s, int max_bytes) {
  if (!s || max_bytes < 0) return;
  int len = strlen(s);
  if (len <= max_bytes) return;
  int i = max_bytes;
  while (i > 0 && (s[i] & 0xC0) == 0x80) i--;
  s[i] = '\0';
}

#define BOOT_SCREEN_MILLIS   3000   // 3 seconds

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

#ifndef LONG_PRESS_MILLIS
#define LONG_PRESS_MILLIS   1200
#endif

#ifndef UI_RECENT_LIST_SIZE
  #define UI_RECENT_LIST_SIZE 4
#endif

#if UI_HAS_JOYSTICK || UI_HAS_ROTARY_INPUT
  #define PRESS_LABEL "press Enter"
  // On joystick boards a short SELECT click = KEY_ENTER; hibernation must require
  // a deliberate press-and-hold, which the SELECT long press maps to (KEY_CANCEL).
  #define HIBERNATE_KEY      KEY_CANCEL
  #define HIBERNATE_LABEL    "hold Select"
  #define UI_PRESS_LABEL     _TR("press Enter", "нажмите Enter")
  #define UI_HIBERNATE_LABEL _TR("hold Select", "удерживайте Select")
  #define UI_ENTER_HINT      _TR("press Enter to enter", "нажмите для входа")
  #define UI_TOGGLE_HINT     _TR("toggle: press Enter", "перекл: Enter")
  #define UI_ADVERT_HINT     _TR("advert: press Enter", "маяк: Enter")
  #define UI_DISCOVER_HINT   _TR("discover: press Enter", "поиск: Enter")
#else
  #define PRESS_LABEL "long press"
  // Single-button boards already map the long press to KEY_ENTER.
  #define HIBERNATE_KEY      KEY_ENTER
  #define HIBERNATE_LABEL    "long press"
  #define UI_PRESS_LABEL     _TR("long press", "удержание")
  #define UI_HIBERNATE_LABEL _TR("long press", "удержание")
  #define UI_ENTER_HINT      _TR("long press to enter", "удержание для входа")
  #define UI_TOGGLE_HINT     _TR("toggle: long press", "перекл: удержание")
  #define UI_ADVERT_HINT     _TR("advert: long press", "маяк: удержание")
  #define UI_DISCOVER_HINT   _TR("discover: long press", "поиск: удержание")
#endif

#include "icons.h"

static void formatDuration(char* buf, size_t len, uint32_t secs) {
  uint32_t m = secs / 60;
  uint32_t h = m / 60;
  uint32_t d = h / 24;
  uint32_t y = d / 365;
  if (secs < 60)
    snprintf(buf, len, _TR("%us", "%uс"), secs);
  else if (h == 0)
    snprintf(buf, len, _TR("%um", "%uм"), m);
  else if (d == 0)
    snprintf(buf, len, _TR("%uh %um", "%uч %uм"), h, m % 60);
  else if (y == 0)
    snprintf(buf, len, _TR("%ud %uh %um", "%uд %uч %uм"), d, h % 24, m % 60);
  else
    snprintf(buf, len, _TR("%uy %ud %uh %um", "%uг %uд %uч %uм"), y, d - y * 365, h % 24, m % 60);
}

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;
  char _version_info[12];   // e.g. "v1.14.1"
  char _commit_info[12];    // e.g. "#abc1234" or "" if no GIT_COMMIT

public:
  SplashScreen(UITask* task) : _task(task) {
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');

    // version part: "v1.14.1"
    int len = dash ? dash - ver : strlen(ver);
    if (len >= (int)sizeof(_version_info)) len = sizeof(_version_info) - 1;
    memcpy(_version_info, ver, len);
    _version_info[len] = 0;

    // commit part: "#abc1234" (from the part after '-')
    if (dash) {
      _commit_info[0] = '#';
      strncpy(_commit_info + 1, dash + 1, sizeof(_commit_info) - 2);
      _commit_info[sizeof(_commit_info) - 1] = 0;
    } else {
      _commit_info[0] = 0;
    }

    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    // Orientation-aware layout: stack logo + text lines and centre the whole
    // block vertically, so it looks balanced in both landscape and portrait.
    const int W = display.width();
    const int H = display.height();
    const int logoW = 128, logoH = 13;
    const int lh = display.getLineH(1);   // small-text line height

    display.setTextSize(1);
    int lines = 3 + (_commit_info[0] ? 1 : 0);   // website, version, date (+commit)
    int blockH = logoH + 6 + lines * lh;
    int y = (H - blockH) / 2;
    if (y < 2) y = 2;

    // meshcore logo (centered; clamp x so it never goes off-screen in portrait)
    display.setColor(UIColor::corp_blue);
    int logoX = (W - logoW) / 2;
    if (logoX < 0) logoX = 0;
    display.drawXbm(logoX, y, meshcore_logo, logoW, logoH);
    y += logoH + 6;

    display.setColor(UIColor::primary_txt);
    display.drawTextCentered(W / 2, y, "https://meshcore.io");  y += lh;
    display.drawTextCentered(W / 2, y, _version_info);          y += lh;
    if (_commit_info[0]) {
      display.drawTextCentered(W / 2, y, _commit_info);         y += lh;
    }
    display.drawTextCentered(W / 2, y, FIRMWARE_BUILD_DATE);

    return 1000;
  }

  void poll() override {
    if (millis() >= dismiss_after) {
      _task->gotoHomeScreen();
    }
  }
};

class HomeScreen : public UIScreen {
  enum HomePage {
    CLOCK,     // clock + PM preview inline
    FIRST,
    RECENT,
    RADIO,
#ifndef WITH_COMMS_SWITCHING
    BLUETOOTH,
#endif
    ADVERT,
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
#if UI_SENSORS_PAGE == 1
    SENSORS,
#endif
#if !(defined(UI_NO_DISCOVER_SCREEN) && (UI_NO_DISCOVER_SCREEN + 0 != 0))
    DISCOVERY,
#endif
    SETTINGS,
#ifndef UI_NO_HIBERNATE
    SHUTDOWN,
#endif
    Count    // keep as last
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  bool _shutdown_init;
  int _clock_pm_pending;   // PMs queued for inline display on CLOCK page
  AdvertPath recent[ADVERT_PATH_TABLE_SIZE];
#if !(defined(UI_NO_DISCOVER_SCREEN) && (UI_NO_DISCOVER_SCREEN + 0 != 0))
  DiscoveredNode discovered[UI_RECENT_LIST_SIZE]; // not circular, latest discovered nodes are not kept
  uint32_t disc_node_req_tag = 0;
  uint32_t disc_nodes_count = 0;
  uint32_t discovery_req_time = 0;
  bool discovery_disp_names = true; // by default display names if available (removes SNR_O)
#endif

  bool          _in_settings    = false;
  int           _settings_sel   = 0;
  int           _settings_scroll = 0;   // first visible item index
  int           _settings_visible = 4;  // updated in render(), used in handleInput()
  unsigned long _pin_show_until = 0;
  int           _pm_clock_mode  = 1;   // 0=all msgs switch screen, 1=PM inline only

  // Cal ADC sub-mode (Settings → Cal ADC): confirm screen, one action at a time.
  bool           _in_adccal = false;
  AdcCalibration _adccal;

  enum class SettingsPage : uint8_t {
    ROOT,
    CHAT,
    DISP,
    COMMS,
    SYSTEM,
    CODECS
  };
  SettingsPage _settings_page = SettingsPage::ROOT;


#if defined(WITH_COMMS_SWITCHING) && defined(ENABLE_WIFI_INTERFACE)
  bool _in_wifi_select  = false;
  int  _wifi_sel        = 0;
  bool _wifi_wait       = false;
  bool _wifi_scanning   = false;
  int  _wifi_scan_n     = -1;   // number of scan results, -1 = not done
  int  _wifi_scan_scroll = 0;
  bool _wifi_cmd_shown  = false;
  char _wifi_cmd[72];           // full-screen command overlay text
#endif


  void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {
    int batteryPercentage = (int)BatteryPack::getPercent(batteryMilliVolts);

    // battery icon
    int iconWidth = 24;
    int iconHeight = 10;
    int iconX = display.width() - iconWidth - 5; // Position the icon near the top-right corner
    int iconY = 0;
    display.setColor(UIColor::title_txt);

    // battery outline
    display.drawRect(iconX, iconY, iconWidth, iconHeight);

    // battery "cap"
    display.fillRect(iconX + iconWidth, iconY + (iconHeight / 4), 3, iconHeight / 2);

    // fill the battery based on the percentage
    int fillWidth = (batteryPercentage * (iconWidth - 4)) / 100;
    display.fillRect(iconX + 2, iconY + 2, fillWidth, iconHeight - 4);

    // while charging, show a bolt (or a plug once full) just left of the battery,
    // keeping the fill bar itself clean and uninterrupted
    bool charging = board.isExternalPowered();
    if (charging) {
      const uint8_t* symbol = (batteryPercentage < 95) ? charging_icon : plug_icon;
      display.setColor(UIColor::title_txt);
      display.drawXbm(iconX - 9, iconY + 1, symbol, 8, 8);
    }

    // show muted icon if buzzer is muted (shifted further left when the charging
    // icon already occupies the slot immediately left of the battery)
#ifdef PIN_BUZZER
    if (_task->isBuzzerQuiet()) {
      display.setColor(UIColor::warning_txt);
      display.drawXbm(iconX - (charging ? 18 : 9), iconY + 1, muted_icon, 8, 8);
    }
#endif
  }

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
  int sensors_scroll_offset = 0;
  int _sensors_visible_rows = UI_RECENT_LIST_SIZE;  // updated each render()
  int next_sensors_refresh = 0;

  void refresh_sensors() {
    if (millis() > next_sensors_refresh) {
      sensors_lpp.reset();
      sensors_nb = 0;
      sensors_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattAdcMilliVolts() / 1000.0f);
      sensors.querySensors(0xFF, sensors_lpp);

      LPPReader reader (sensors_lpp.getBuffer(), sensors_lpp.getSize());
      uint8_t channel, type;
      while(reader.readHeader(channel, type)) {
        reader.skipData(type);
        sensors_nb ++;
      }
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = millis() + 5000; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = millis() + 60000; // refresh sensor values every 1 min
#endif
    }
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs), _page(0),
       _shutdown_init(false), _clock_pm_pending(0), sensors_lpp(200) {
    if (_node_prefs != nullptr) {
      _pm_clock_mode = constrain(_node_prefs->ui_pm_clock_mode, 0, 1);
    }
  }

  bool isOnClockPage() const { return _page == CLOCK; }
  void incrementClockPM() { _clock_pm_pending++; }
  int  getPmClockMode()  const { return _pm_clock_mode; }

#if !(defined(UI_NO_DISCOVER_SCREEN) && (UI_NO_DISCOVER_SCREEN + 0 != 0))
  bool sendDiscoverRequest() {
    uint8_t cmd_bytes[6];
    cmd_bytes[0] = CTL_TYPE_NODE_DISCOVER_REQ | 1; // DISCOVER_REQ | prefix only
    cmd_bytes[1] = 0xFF;     // Repeaters
    the_mesh.getRNG()->random((uint8_t *) &disc_node_req_tag, 4);  // generate random tag
    memcpy(&cmd_bytes[2], &disc_node_req_tag, 4);
    disc_nodes_count = 0;
    mesh::Packet* req = the_mesh.createControlData(cmd_bytes, sizeof(cmd_bytes));
    if (req) {
      the_mesh.sendZeroHop(req);
      return true;
    }
    return false;
  }

  void handleDiscoverResponse(const mesh::Packet* packet) {
    if (disc_nodes_count < UI_RECENT_LIST_SIZE && memcmp(&packet->payload[2], &disc_node_req_tag, 4) == 0) {
      auto d = &discovered[disc_nodes_count++];
      memcpy(d->pubkey_prefix, &packet->payload[6], 8);
      d->pubkey_prefix[8] = 0;
      d->type = packet->payload[0] & 0xF;
      d->snr_out = ((int8_t)packet->payload[1]) / 4.0f;
      d->snr_in = radio_driver.getLastSNR();
      ContactInfo* c = the_mesh.lookupContactByPubKey(&packet->payload[6], 8);
      if (c != NULL) {
        strncpy(d->name, c->name, sizeof(d->name) - 1);
        d->name[sizeof(d->name) - 1] = 0;
      } else {
        d->name[0] = 0;
      }
    }
  }
#endif

  void poll() override {
    if (_shutdown_init && !_task->isButtonPressed()) {  // must wait for USR button to be released
      _task->shutdown();
    }
    if (_pin_show_until && millis() >= _pin_show_until) {
      _pin_show_until = 0;  // render() will show **** on next cycle
    }
#if defined(WITH_COMMS_SWITCHING) && defined(ENABLE_WIFI_INTERFACE)
    if (_in_wifi_select && _wifi_wait && !the_mesh.isWifiConnecting()) {
      _in_wifi_select = false;
      _wifi_wait      = false;
      WiFi.scanDelete();
      _wifi_scan_n    = -1;
    }
#endif
  }

  // Extra settings the build lists in a folder (UIExtensions.h), in its order.
  static int extraCount(uint8_t folder) {
    const UIExtraSetting* extra = nullptr;
    int n = uiExtraSettings(&extra), c = 0;
    for (int k = 0; k < n && extra; k++) if (extra[k].folder == folder) c++;
    return c;
  }
  static const UIExtraSetting* extraAt(uint8_t folder, int idx) {
    const UIExtraSetting* extra = nullptr;
    int n = uiExtraSettings(&extra);
    for (int k = 0; k < n && extra; k++) {
      if (extra[k].folder != folder) continue;
      if (idx-- == 0) return &extra[k];
    }
    return nullptr;
  }
  static bool extraHidden(uint8_t folder, int idx, DisplayDriver* d) {
    const UIExtraSetting* e = extraAt(folder, idx);
    return e && e->visible && d && !e->visible(*d);
  }

  int settingsPageItemCount(SettingsPage page) const {
    switch (page) {
      case SettingsPage::ROOT:    return 5;
      case SettingsPage::CHAT:    return 6;
      case SettingsPage::DISP:    return 4 + extraCount(UI_FOLDER_DISPLAY);
      case SettingsPage::COMMS:   return 5 + extraCount(UI_FOLDER_CONNECTION);
      case SettingsPage::SYSTEM:  return 5;
      case SettingsPage::CODECS:  return 5;
    }
    return 0;
  }

  bool isSettingHidden(SettingsPage page, int i) const {
    DisplayDriver* d = _task->getDisplay();
    if (page == SettingsPage::ROOT) {
      if (i == 2) {   // Connection: shown when anything in it is
        for (int j = 1; j < settingsPageItemCount(SettingsPage::COMMS); j++) {
          if (!isSettingHidden(SettingsPage::COMMS, j)) return false;
        }
        return true;
      }
    } else if (page == SettingsPage::CHAT) {
#ifndef WITH_COMPANION_CLI
      if (i == 3 || i == 4) return true;
#endif
    } else if (page == SettingsPage::DISP) {
#ifndef CYRILLIC_SUPPORT
      if (i == 1) return true;
#endif
#if AUTO_OFF_MILLIS == 0
      if (i == 2) return true;
#endif
      if (i == 3) return !(d && d->isEink());
      if (i >= 4) return extraHidden(UI_FOLDER_DISPLAY, i - 4, d);
    } else if (page == SettingsPage::COMMS) {
#ifndef WITH_COMMS_SWITCHING
      if (i == 1) return true;
#endif
#ifndef WITH_COMPANION_CLI
      if (i == 2 || i == 4) return true;
#endif
#if !defined(BLE_PIN_CODE) && !defined(WITH_COMMS_SWITCHING)
      if (i == 3) return true;   // no BLE in this build: nothing to name
#endif
      if (i >= 5) return extraHidden(UI_FOLDER_CONNECTION, i - 5, d);
    } else if (page == SettingsPage::SYSTEM) {
      if (i == 1) {
#if !defined(WITH_ESP32_POWER_SAVING) || (defined(ENABLE_WIFI_INTERFACE) && !defined(WITH_COMMS_SWITCHING)) || (defined(ENABLE_USB_INTERFACE) && !defined(WITH_COMMS_SWITCHING))
        return true;
#endif
      }
      if (i == 2) return !AdcCalibration::supported(board);
    }
    return false;
  }

  const char* getSettingLabel(SettingsPage page, int i) const {
    if (page == SettingsPage::ROOT) {
      if (i == 0) return _TR("[ Chat ]", "[ Сообщения ]");
      if (i == 1) return _TR("[ Display ]", "[ Экран ]");
      if (i == 2) return _TR("[ Connection ]", "[ Связь ]");
      if (i == 3) return _TR("[ System ]", "[ Система ]");
      if (i == 4) return _TR("[ Exit ]", "[ Выход ]");
    } else if (i == 0) {
      return _TR("< Back", "< Назад");
    }

    if (page == SettingsPage::CHAT) {
      if (i == 1) return _TR("Popups", "Уведомл.");
      if (i == 2) return _TR("Show Time", "Время показа");
      if (i == 3) return _TR("Translit", "Транслит");
      if (i == 4) return _TR("Serial Chat", "Чат в порт");
      if (i == 5) return _TR("History", "История");
    } else if (page == SettingsPage::DISP) {
      if (i == 1) return _TR("Language", "Язык");
      if (i == 2) return _TR("Always On", "Часы всегда");
      if (i == 3) return _TR("Rotation", "Поворот");
      if (i >= 4) {
        const UIExtraSetting* e = extraAt(UI_FOLDER_DISPLAY, i - 4);
        if (e && e->label) return e->label();
      }
    } else if (page == SettingsPage::COMMS) {
      if (i == 1) return _TR("Interface", "Интерфейс");
      if (i == 2) return _TR("Admin PIN", "PIN админа");
      if (i == 3) return _TR("BLE Prefix", "Префикс BLE");
      if (i == 4) return _TR("Timesync", "Синхр. врем.");
      if (i >= 5) {
        const UIExtraSetting* e = extraAt(UI_FOLDER_CONNECTION, i - 5);
        if (e && e->label) return e->label();
      }
    } else if (page == SettingsPage::SYSTEM) {
      if (i == 1) return _TR("Power Mode", "Энергосбер.");
      if (i == 2) return _TR("Batt Cal", "Калибр. АКБ");
      if (i == 3) return _TR("Shutdown", "Откл. по АКБ");
      if (i == 4) return _TR("Codecs >", "Кодеки >");
    } else if (page == SettingsPage::CODECS) {
      if (i == 1) return "MCOtxt";
      if (i == 2) return "MCMP";
      if (i == 3) return "AEIC";
      if (i == 4) return "MCOimg";
    }
    return "";
  }

  bool getSettingValue(SettingsPage page, int i, char* val, size_t val_len) const {
    if (page == SettingsPage::ROOT || i == 0) return false;

    if (page == SettingsPage::CHAT) {
      if (i == 1) {
        snprintf(val, val_len, "%s", _pm_clock_mode ? _TR("PM only", "Только ЛС") : _TR("All", "Все"));
      } else if (i == 2 && _node_prefs) {
        static const char* h_en[4] = { "Off", "5s", "15s", "30s" };
        static const char* h_ru[4] = { "Выкл", "5с", "15с", "30с" };
        int idx = constrain(_node_prefs->ui_msg_hold, 0, 3);
        snprintf(val, val_len, "%s", uiIsLangRu() ? h_ru[idx] : h_en[idx]);
      } else if (i == 3) {
#ifdef WITH_COMPANION_CLI
        bool ch = the_mesh.isCyr2LatChannelsEnabled();
        bool dm = the_mesh.isCyr2LatContactsEnabled();
        int state = (ch ? 1 : 0) | (dm ? 2 : 0);
        static const char* t_en[4] = { "Off", "Chan", "DM", "All" };
        static const char* t_ru[4] = { "Выкл", "Канал", "ЛС", "Все" };
        snprintf(val, val_len, "%s", uiIsLangRu() ? t_ru[state] : t_en[state]);
#endif
      } else if (i == 4) {
#ifdef WITH_COMPANION_CLI
        static const char* sc_en[4] = { "All", "Chan", "PM", "Off" };
        static const char* sc_ru[4] = { "Все", "Канал", "ЛС", "Выкл" };
        int m = constrain(the_mesh.getChatMode(), 0, 3);
        snprintf(val, val_len, "%s", uiIsLangRu() ? sc_ru[m] : sc_en[m]);
#endif
      } else if (i == 5 && _node_prefs) {
        static const int unread_vals[3] = { 16, 32, 64 };
        int idx = constrain(_node_prefs->ui_max_unread_idx, 0, 2);
        snprintf(val, val_len, "%d", unread_vals[idx]);
      }
      return true;
    }

    if (page == SettingsPage::DISP) {
      if (i == 1) {
        snprintf(val, val_len, "%s", (_node_prefs && _node_prefs->ui_lang == 1) ? "RU" : "EN");
      } else if (i == 2) {
        int dim = _task->getClockDimMode();
        snprintf(val, val_len, "%s", dim == 0 ? _TR("Off", "Выкл") : _TR("Clock", "Всегда"));
      } else if (i == 3 && _node_prefs) {
        static const char* rot_vals[4] = { "0", "90", "180", "270" };
        int r = constrain(_node_prefs->ui_display_rotation, 0, 3);
        snprintf(val, val_len, "%s\xb0", rot_vals[r]);
      } else if (i >= 4) {
        const UIExtraSetting* e = extraAt(UI_FOLDER_DISPLAY, i - 4);
        if (e && e->value) e->value(val, val_len);
      }
      return true;
    }

    if (page == SettingsPage::COMMS) {
      if (i == 1) {
#ifdef WITH_COMMS_SWITCHING
        uint8_t mode = the_mesh.getWifiPrefs()->comms_mode;
#if defined(ENABLE_WIFI_INTERFACE)
        if (mode == COMMS_MODE_WIFI) {
          if (the_mesh.isWifiConnecting())     snprintf(val, val_len, "WiFi...");
          else if (the_mesh.isWifiConnected()) snprintf(val, val_len, "WiFi OK");
          else                                 snprintf(val, val_len, "WiFi");
        } else
#endif
        if (mode == COMMS_MODE_USB) {
          snprintf(val, val_len, "USB");
        } else {
          snprintf(val, val_len, "BLE");
        }
#else
        snprintf(val, val_len, "BLE");
#endif
      } else if (i == 2) {
#ifdef WITH_COMPANION_CLI
        bool show = _pin_show_until && millis() < _pin_show_until;
        snprintf(val, val_len, "%s", show ? the_mesh.getCliPin() : "****");
#endif
      } else if (i == 3 && _node_prefs) {
        snprintf(val, val_len, "%s", blePrefixEnabled(_node_prefs) ? _TR("On", "Вкл") : _TR("Off", "Выкл"));
      } else if (i == 4) {
#ifdef WITH_COMPANION_CLI
        int tsm = the_mesh.getTimesyncMode();
        static const char* ts_en[3] = { "All", "GPS", "Mesh" };
        static const char* ts_ru[3] = { "Все", "GPS", "Mesh" };
        if (tsm >= 0 && tsm < 3) {
          snprintf(val, val_len, "%s", uiIsLangRu() ? ts_ru[tsm] : ts_en[tsm]);
        } else {
          const char* ts_lbl = uiTimeSourceModeLabel(tsm);
          snprintf(val, val_len, "%s", ts_lbl ? ts_lbl : "?");
        }
#endif
      } else if (i >= 5) {
        const UIExtraSetting* e = extraAt(UI_FOLDER_CONNECTION, i - 5);
        if (e && e->value) e->value(val, val_len);
      }
      return true;
    }

    if (page == SettingsPage::SYSTEM) {
      if (i == 1 && _node_prefs) {
        static const char* p_en[4] = { "Auto", "Off", "Save", "Max" };
        static const char* p_ru[4] = { "Авто", "Выкл", "Эконом", "Макс" };
        int m = constrain(_node_prefs->power_mode, 0, 3);
        snprintf(val, val_len, "%s", uiIsLangRu() ? p_ru[m] : p_en[m]);
      } else if (i == 2) {
        uint16_t mv = board.getBattAdcMilliVolts();
        snprintf(val, val_len, "%u.%02uV >", mv / 1000, (mv % 1000) / 10);
      } else if (i == 3 && _node_prefs) {
        uint16_t mv = _node_prefs->shutdown_mv;
        if (mv == 0) {
          snprintf(val, val_len, "%s", _TR("Off", "Выкл"));
        } else {
          snprintf(val, val_len, "%u.%u V", mv / 1000, (mv % 1000) / 100);
        }
      } else if (i == 4) {
        snprintf(val, val_len, ">");
      }
      return true;
    }

    if (page == SettingsPage::CODECS) {
      if (i == 1) snprintf(val, val_len, "%s", the_mesh.isMCOtxtEnabled() ? _TR("On", "Вкл") : _TR("Off", "Выкл"));
      else if (i == 2) snprintf(val, val_len, "%s", the_mesh.isMCMPDetectEnabled() ? _TR("On", "Вкл") : _TR("Off", "Выкл"));
      else if (i == 3) snprintf(val, val_len, "%s", the_mesh.isAEICDetectEnabled() ? _TR("On", "Вкл") : _TR("Off", "Выкл"));
      else if (i == 4) snprintf(val, val_len, "%s", the_mesh.isMCOimgDetectEnabled() ? _TR("On", "Вкл") : _TR("Off", "Выкл"));
      return true;
    }

    return false;
  }

  void onSettingPress(SettingsPage page, int i, bool fwd) {
    if (page == SettingsPage::ROOT) {
      if (!fwd) { _in_settings = false; return; }
      if (i == 0) { _settings_page = SettingsPage::CHAT; }
      else if (i == 1) { _settings_page = SettingsPage::DISP; }
      else if (i == 2) { _settings_page = SettingsPage::COMMS; }
      else if (i == 3) { _settings_page = SettingsPage::SYSTEM; }
      else if (i == 4) { _in_settings = false; return; }
      _settings_sel = 1;
      _settings_scroll = 0;
      while (_settings_sel < settingsPageItemCount(_settings_page) && isSettingHidden(_settings_page, _settings_sel)) {
        _settings_sel++;
      }
      return;
    }

    if (i == 0) {
      if (page == SettingsPage::CODECS) {
        _settings_page = SettingsPage::SYSTEM;
        _settings_sel = 4;
        _settings_scroll = 0;
      } else {
        SettingsPage prev = _settings_page;
        _settings_page = SettingsPage::ROOT;
        if (prev == SettingsPage::CHAT) _settings_sel = 0;
        else if (prev == SettingsPage::DISP) _settings_sel = 1;
        else if (prev == SettingsPage::COMMS) _settings_sel = 2;
        else if (prev == SettingsPage::SYSTEM) _settings_sel = 3;
        _settings_scroll = 0;
      }
      return;
    }

    if (page == SettingsPage::CHAT) {
      if (i == 1) {
        _pm_clock_mode = (_pm_clock_mode + 1) % 2;
        if (_node_prefs) _node_prefs->ui_pm_clock_mode = _pm_clock_mode;
        the_mesh.savePrefs();
      } else if (i == 2 && _node_prefs) {
        _node_prefs->ui_msg_hold = (_node_prefs->ui_msg_hold + 1) % 4;
        the_mesh.savePrefs();
      } else if (i == 3) {
#ifdef WITH_COMPANION_CLI
        bool ch = the_mesh.isCyr2LatChannelsEnabled();
        bool dm = the_mesh.isCyr2LatContactsEnabled();
        int state = (ch ? 1 : 0) | (dm ? 2 : 0);
        state = fwd ? (state + 1) % 4 : (state + 3) % 4;
        the_mesh.setCyr2LatChannelsEnabled(state == 1 || state == 3);
        the_mesh.setCyr2LatContactsEnabled(state == 2 || state == 3);
#endif
      } else if (i == 4) {
#ifdef WITH_COMPANION_CLI
        int m = the_mesh.getChatMode();
        the_mesh.setChatMode(fwd ? (m + 1) % 4 : (m + 3) % 4);
#endif
      } else if (i == 5 && _node_prefs) {
        int idx = _node_prefs->ui_max_unread_idx;
        idx = fwd ? (idx + 1) % 3 : (idx + 2) % 3;
        _node_prefs->ui_max_unread_idx = idx;
        _node_prefs->ui_max_log_idx = idx;
        static const int size_table[3] = { 16, 32, 64 };
        _task->updateMsgMaxSizes(size_table[idx], size_table[idx]);
        the_mesh.savePrefs();
      }
    } else if (page == SettingsPage::DISP) {
      if (i == 1 && _node_prefs) {
#ifdef CYRILLIC_SUPPORT
        _node_prefs->ui_lang = (_node_prefs->ui_lang == 1) ? 0 : 1;
        the_mesh.savePrefs();
#endif
      } else if (i == 2) {
        _task->setClockDimMode((_task->getClockDimMode() + 1) % 2);
        the_mesh.savePrefs();
      } else if (i == 3 && _node_prefs) {
        int r = _node_prefs->ui_display_rotation;
        r = fwd ? (r + 1) % 4 : (r + 3) % 4;
        _node_prefs->ui_display_rotation = r;
        _task->setDisplayRotation(r);
        the_mesh.savePrefs();
      } else if (i >= 4) {
        const UIExtraSetting* e = extraAt(UI_FOLDER_DISPLAY, i - 4);
        if (e && e->press) e->press(fwd);
      }
    } else if (page == SettingsPage::COMMS) {
      if (i == 1) {
#if defined(WITH_COMMS_SWITCHING) && defined(ENABLE_WIFI_INTERFACE)
        if (!_wifi_wait && fwd) {
          _in_wifi_select   = true;
          _wifi_sel         = 0;
          _wifi_scan_scroll = 0;
          _wifi_scanning    = true;
          _wifi_scan_n      = -1;
          WiFi.mode(WIFI_STA);
          WiFi.scanNetworks(true);
        }
#elif defined(WITH_COMMS_SWITCHING)
        // No WiFi here: the Interface row steps through the transports this build
        // has (BLE and USB), skipping the unavailable ones.
        if (fwd) {
          uint8_t mode = the_mesh.getWifiPrefs()->comms_mode;
          the_mesh.switchCommsMode(mode == COMMS_MODE_BLE ? COMMS_MODE_USB : COMMS_MODE_BLE);
        }
#endif
      } else if (i == 2) {
#ifdef WITH_COMPANION_CLI
        // First press shows the PIN; a second one while it is shown makes a new one.
        if (_pin_show_until && millis() < _pin_show_until) {
          the_mesh.regenerateCliPin();
          _pin_show_until = millis() + 5000;
        } else {
          _pin_show_until = millis() + 3000;
        }
#endif
      } else if (i == 3 && _node_prefs) {
        // Applies at the next boot: the BLE name is set once when BLE starts.
        _node_prefs->ui_ble_prefix = blePrefixEnabled(_node_prefs) ? BLE_PREFIX_OFF : BLE_PREFIX_ON;
        the_mesh.savePrefs();
      } else if (i == 4) {
#ifdef WITH_COMPANION_CLI
        int n_ts = uiTimeSourceModeCount();
        int m = the_mesh.getTimesyncMode();
        the_mesh.setTimesyncMode(fwd ? (m + 1) % n_ts : (m + n_ts - 1) % n_ts);
#endif
      } else if (i >= 5) {
        const UIExtraSetting* e = extraAt(UI_FOLDER_CONNECTION, i - 5);
        if (e && e->press) e->press(fwd);
      }
    } else if (page == SettingsPage::SYSTEM) {
      if (i == 1 && _node_prefs) {
        int m = _node_prefs->power_mode;
        _node_prefs->power_mode = fwd ? (m + 1) % 4 : (m + 3) % 4;
        _node_prefs->powersaving_enabled = (_node_prefs->power_mode != POWER_MODE_OFF) ? 1 : 0;
        the_mesh.savePrefs();
      } else if (i == 2) {
        _adccal.measure(board);
        _in_adccal = true;
      } else if (i == 3 && _node_prefs) {
        static const uint16_t values[4] = { 0, 3000, 3200, 3400 };
        uint16_t mv = _node_prefs->shutdown_mv;
        int idx = (mv == 3000) ? 1 : (mv == 3200) ? 2 : (mv == 3400) ? 3 : 0;
        idx = fwd ? (idx + 1) % 4 : (idx + 3) % 4;
        _node_prefs->shutdown_mv = values[idx];
        board.setShutdownMilliVolts(values[idx]);
        the_mesh.savePrefs();
      } else if (i == 4) {
        _settings_page = SettingsPage::CODECS;
        _settings_sel = 1;
        _settings_scroll = 0;
      }
    } else if (page == SettingsPage::CODECS) {
      if (i == 1) the_mesh.setMCOtxtEnabled(!the_mesh.isMCOtxtEnabled());
      else if (i == 2) the_mesh.setMCMPDetectEnabled(!the_mesh.isMCMPDetectEnabled());
      else if (i == 3) the_mesh.setAEICDetectEnabled(!the_mesh.isAEICDetectEnabled());
      else if (i == 4) the_mesh.setMCOimgDetectEnabled(!the_mesh.isMCOimgDetectEnabled());
    }
  }


  // Cal ADC confirm screen. Full screen, no header, so it fits a 128x64 OLED:
  // five rows of the smallest font — title, volts, multiplier, status, action.
  void renderAdcCal(DisplayDriver& display) {
    const AdcCalibration& c = _adccal;
    int sz = (display.width() >= 200) ? 2 : 1;
    int lh = display.getLineH(sz);
    int cx = display.width() / 2;
    int y  = 0;
    char buf[32];
    display.setTextSize(sz);
    display.setColor(UIColor::primary_txt);
    display.drawTextCentered(cx, y, _TR("Cal ADC", "Калибр. АКБ"));
    y += lh;
    snprintf(buf, sizeof(buf), "%u.%02uV>%u.%02uV%s",
             c.mv / 1000, (c.mv % 1000) / 10, c.target_mv / 1000, (c.target_mv % 1000) / 10,
             c.target_ina ? " INA" : "");
    display.drawTextCentered(cx, y, buf);
    y += lh;
    snprintf(buf, sizeof(buf), "k %.3f>%.3f", c.old_k, c.new_k);
    display.drawTextCentered(cx, y, buf);
    y += lh;
    const char* note = c.statusText();
    if (!note && AdcCalibration::usbKnown() && !c.target_ina && !board.isExternalPowered())
      note = _TR("No USB power", "Нет питания USB");
    if (note) {
      display.setColor(UIColor::warning_txt);
      display.drawTextCentered(cx, y, note);
      display.setColor(UIColor::primary_txt);
    }
    y += lh;
    snprintf(buf, sizeof(buf), "< %s >", AdcCalibration::actionName(c.sel));
    display.drawTextCentered(cx, y, buf);
  }

  int render(DisplayDriver& display) override {
    if (uiOverlayActive()) return uiOverlayRender(display);
    if (_in_adccal) { renderAdcCal(display); return 60000; }
    char tmp[80];
    int hdr_size   = (display.width() >= 200) ? 2 : 1;
    int hdr_line_h = display.getLineH(hdr_size) - 3;
    int dot_y      = hdr_line_h + 5;
    int content_y  = hdr_line_h + 14;
    int line_h     = display.getLineH(hdr_size);

    // CLOCK + pending PM: render full-screen like MsgPreviewScreen (no node/battery header)
    if (_page == HomePage::CLOCK && _clock_pm_pending > 0) {
      UITask::ClockPMInfo info;
      if (_task->peekTopMsg(info)) {
        int msg_start_y = hdr_line_h + 3;

        // Header: "#N (D)name" + time elapsed top-right
        char time_str[8];
        int secs = (int)(_rtc->getCurrentTime() - info.timestamp);
        if (secs < 0) secs = 0;
        if      (secs < 60)   snprintf(time_str, sizeof(time_str), _TR("%ds", "%dс"),  secs);
        else if (secs < 3600) snprintf(time_str, sizeof(time_str), _TR("%dm", "%dм"),  secs / 60);
        else                  snprintf(time_str, sizeof(time_str), _TR("%dh", "%dч"),  secs / 3600);

        int hdr_ch_w    = 6 * hdr_size;
        int hdr_total   = display.width() / hdr_ch_w;
        int name_budget = hdr_total - (int)strlen(time_str) - 4;  // "#N " + gap
        if (name_budget < 0) name_budget = 0;

        char raw_name[36];
        if (info.path_len == 0xFF)
          snprintf(raw_name, sizeof(raw_name), "(D)%s", info.from_name);
        else
          snprintf(raw_name, sizeof(raw_name), "[%d]%s", info.path_len, info.from_name);
        utf8_truncate(raw_name, name_budget);

        char hdr_left[64];
        snprintf(hdr_left, sizeof(hdr_left), "#%d %s", _clock_pm_pending, raw_name);

        display.setTextSize(hdr_size);
        display.setColor(UIColor::primary_txt);
        display.setCursor(0, 0);
        display.print(hdr_left);
        int time_x = display.width() - 1 - (int)strlen(time_str) * hdr_ch_w;
        display.setCursor(time_x, 0);
        display.print(time_str);

        // Divider
        display.setColor(UIColor::primary_txt);
        display.drawRect(0, hdr_line_h + 2, display.width(), 1);

        // Message body
        if (!uiDrawMessageContent(display, info.msg, 0, msg_start_y, display.width(), display.height() - msg_start_y)) {
          display.setTextSize(1);
          display.setCursor(0, msg_start_y);
          display.printWordWrap(info.msg, display.width());
        }
      }
      return 60000;
    }

    // node name
    display.setTextSize(hdr_size);
    display.setColor(UIColor::primary_txt);
    display.setCursor(0, 2);
    display.print(_node_prefs->node_name);

    char note_buf[32];
    if (uiHeaderNote((int)_page, note_buf, sizeof(note_buf))) {
      display.setTextSize(1);
      display.drawTextRightAlign(display.width() - 32, 0, note_buf);
      display.setTextSize(hdr_size);
    }

    // battery voltage
    renderBatteryIndicator(display, _task->getBattMilliVolts());

    // curr page indicator
    int visible_count = HomePage::Count;
    int x = display.width() / 2 - 5 * (visible_count - 1);
    for (uint8_t i = 0; i < HomePage::Count; i++) {
      if (i == _page) {
        display.fillRect(x-1, dot_y-1, 3, 3);
      } else {
        display.fillRect(x, dot_y, 1, 1);
      }
      x += 10;
    }

    int custom_refresh = uiDrawPage((int)_page, display, content_y);
    if (custom_refresh > 0) {
      return custom_refresh;
    } else if (_page == HomePage::CLOCK) {
      // --- Large clock display ---
      _clock_pm_pending = 0;
      time_t now = (time_t)_rtc->getCurrentTime();
      if (_node_prefs && _node_prefs->tz_offset != 0) {
        now += (time_t)_node_prefs->tz_offset * 3600;
      }
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      char timeBuf[6];
      strftime(timeBuf, sizeof(timeBuf), "%H:%M", &timeinfo);
      char sourceBuf[32];
      snprintf(sourceBuf, sizeof(sourceBuf), "%s", the_mesh.getTimeSourceLabel());
      if (the_mesh.getTimeSyncCount() > 0 && the_mesh.getTimeSource() == MyMesh::TIME_SOURCE_ADVERT) {
        snprintf(sourceBuf, sizeof(sourceBuf), "%s %+lds",
                 the_mesh.getTimeSourceLabel(), (long)the_mesh.getTimeLastAdjustment());
      }
#ifdef CYRILLIC_SUPPORT
      // Russian short date (native UTF-8 Cyrillic) — shared by e-ink, colour TFT and OLED.
      static const char* days_ru[]   = {"\xd0\xb2\xd1\x81","\xd0\xbf\xd0\xbd","\xd0\xb2\xd1\x82","\xd1\x81\xd1\x80","\xd1\x87\xd1\x82","\xd0\xbf\xd1\x82","\xd1\x81\xd0\xb1"};
      static const char* months_ru[] = {"\xd1\x8f\xd0\xbd\xd0\xb2","\xd1\x84\xd0\xb5\xd0\xb2","\xd0\xbc\xd0\xb0\xd1\x80","\xd0\xb0\xd0\xbf\xd1\x80","\xd0\xbc\xd0\xb0\xd0\xb9","\xd0\xb8\xd1\x8e\xd0\xbd","\xd0\xb8\xd1\x8e\xd0\xbb","\xd0\xb0\xd0\xb2\xd0\xb3","\xd1\x81\xd0\xb5\xd0\xbd","\xd0\xbe\xd0\xba\xd1\x82","\xd0\xbd\xd0\xbe\xd1\x8f","\xd0\xb4\xd0\xb5\xd0\xba"};
#endif
      display.setColor(UIColor::primary_txt);
      if (_task->isEinkDisplay()) {
        char dateBuf[32];
#ifdef CYRILLIC_SUPPORT
        if (uiIsLangRu()) {
          snprintf(dateBuf, sizeof(dateBuf), "%s %d %s",
                   days_ru[timeinfo.tm_wday], timeinfo.tm_mday, months_ru[timeinfo.tm_mon]);
        } else {
          strftime(dateBuf, sizeof(dateBuf), "%a %d %b", &timeinfo);
        }
#else
        strftime(dateBuf, sizeof(dateBuf), "%a %d %b", &timeinfo);
#endif
        // date uses hdr_size for readability on wide displays; source shown at bottom
        int date_sz = hdr_size;
        int clock_sz = min(display.width() / 30, (display.height() - 22) / 8);
        if (clock_sz < 1) clock_sz = 1;
        if (clock_sz > 8) clock_sz = 8;
        int clock_y = (display.height() - 22 - 8 * clock_sz) / 2;
        if (clock_y < content_y) clock_y = content_y;  // don't overlap page dots
        display.setTextSize(clock_sz);
        display.drawDigitsCentered(display.width() / 2, clock_y, timeBuf);
        display.setTextSize(date_sz);
        display.drawTextCentered(display.width() / 2, display.height() - 20, dateBuf);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, display.height() - 8, sourceBuf);
      } else if (_task->isColorTFTDisplay()) {
        char dateBuf[32];
#ifdef CYRILLIC_SUPPORT
        if (uiIsLangRu()) {
          snprintf(dateBuf, sizeof(dateBuf), "%s %d %s",
                   days_ru[timeinfo.tm_wday], timeinfo.tm_mday, months_ru[timeinfo.tm_mon]);
        } else {
          strftime(dateBuf, sizeof(dateBuf), "%a %d %b", &timeinfo);
        }
#else
        strftime(dateBuf, sizeof(dateBuf), "%a %d %b", &timeinfo);
#endif
        // T114 TFT 240×135, virtual canvas 128×64 (SCALE=1.875, Y_OFFSET=7).
        // setTextSize(3) activates glcdfont 6×8 renderer scaled via SCALE_X/Y — same
        // macros as all other primitives, so virtual positions match exactly:
        //   clock  virt 22–46 → phys 48–93 px  (45 px = 37.5% usable, same as OLED V3)
        //   date   virt 46    → phys 93 px      (ArialMT_Plain_24, 24 px tall → 117 px)
        //   source virt 58    → phys 115 px     (ArialMT_Plain_16, 16 px tall → 131 px)
        display.setTextSize(3);
        display.drawTextCentered(display.width() / 2, content_y, timeBuf);
        display.setTextSize(2);
        display.drawTextCentered(display.width() / 2, display.height() - 18, dateBuf);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, display.height() - 6, sourceBuf);
      } else {
        // OLED 128x64: size 3 (18px wide per char, 24px tall) fits "HH:MM" in 90px
        display.setTextSize(3);
        display.drawTextCentered(display.width() / 2, content_y, timeBuf);
        display.setTextSize(1);
        char dateBuf[32];
#ifdef CYRILLIC_SUPPORT
        if (uiIsLangRu()) {
          snprintf(dateBuf, sizeof(dateBuf), "%s %d %s",
                   days_ru[timeinfo.tm_wday], timeinfo.tm_mday, months_ru[timeinfo.tm_mon]);
        } else {
          strftime(dateBuf, sizeof(dateBuf), "%a %d %b", &timeinfo);
        }
#else
        strftime(dateBuf, sizeof(dateBuf), "%a %d %b", &timeinfo);
#endif
        display.drawTextCentered(display.width() / 2, content_y + 24 + 2, dateBuf);
        display.drawTextCentered(display.width() / 2, display.height() - 8, sourceBuf);
      }
      return 60000;  // refresh once per minute
    } else if (_page == HomePage::FIRST) {
      display.setColor(UIColor::primary_txt);
      display.setTextSize(2);
      int unrd = _task->getUnreadMsgCount();
      if (unrd > 0)
        sprintf(tmp, _TR("NEW: %d", "НОВЫХ: %d"), unrd);
      else
        sprintf(tmp, _TR("MSG: %d", "СООБЩ: %d"), _task->getMsgCount());
      display.drawTextCentered(display.width() / 2, content_y, tmp);

      // A build whose WiFi is always on (no runtime switch). Not under WIFI_SSID:
      // NodePrefs.h defines that for every WiFi build, and the line then sat on
      // top of the BLE PIN in BLE mode. The switching builds show the IP in the
      // PIN's place below, once WiFi is up.
      #if defined(ENABLE_WIFI_INTERFACE) && !defined(WITH_COMMS_SWITCHING)
      if (_task->hasConnection() || the_mesh.getBLEPin() == 0) {   // the PIN would take this space
        IPAddress ip = WiFi.localIP();
        snprintf(tmp, sizeof(tmp), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, display.height() - 10, tmp);
      }
      #endif
      if (_task->hasConnection()) {
        display.setColor(UIColor::primary_txt);
        display.setTextSize(hdr_size);
        display.drawTextCentered(display.width() / 2, content_y + 24, _TR("< Connected >", "< Подключено >"));
#if defined(WITH_COMMS_SWITCHING) && defined(ENABLE_WIFI_INTERFACE)
      } else if (the_mesh.isWifiConnected()) {
        String wip = the_mesh.getWifiIP();
        snprintf(tmp, sizeof(tmp), "IP:%s", wip.c_str());
        display.setColor(UIColor::primary_txt);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, content_y + 24, tmp);
      } else if (the_mesh.isWifiConnecting()) {
        display.setColor(UIColor::secondary_txt);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, content_y + 24, "WiFi...");
#endif
      } else if (the_mesh.getBLEPin() != 0) {
#ifdef WITH_COMMS_SWITCHING
        if (the_mesh.getWifiPrefs()->comms_mode != COMMS_MODE_USB)   // BLE runs alongside WiFi
#endif
        {
          display.setColor(UIColor::warning_txt);
          display.setTextSize(2);
          sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
          display.drawTextCentered(display.width() / 2, content_y + 24, tmp);
        }
      }
    } else if (_page == HomePage::RECENT) {
      int visible_rows = (display.height() - content_y) / line_h;
      if (visible_rows < 1) visible_rows = 1;
      if (visible_rows > ADVERT_PATH_TABLE_SIZE) visible_rows = ADVERT_PATH_TABLE_SIZE;
      the_mesh.getRecentlyHeard(recent, visible_rows);
      display.setColor(UIColor::primary_txt);
      display.setTextSize(hdr_size);
      int y = content_y;
      for (int i = 0; i < visible_rows; i++) {
        auto a = &recent[i];
        if (a->name[0] == 0) continue;  // empty slot
        int secs = _rtc->getCurrentTime() - a->recv_timestamp;
        if (secs < 60) {
          sprintf(tmp, _TR("%ds", "%dс"), secs);
        } else if (secs < 60*60) {
          sprintf(tmp, _TR("%dm", "%dм"), secs / 60);
        } else {
          sprintf(tmp, _TR("%dh", "%dч"), secs / (60*60));
        }

        int timestamp_width = display.getTextWidth(tmp);
        int max_name_width = display.width() - timestamp_width - 1;

        display.drawTextEllipsized(0, y, max_name_width, a->name);
        display.setCursor(display.width() - timestamp_width - 1, y);
        display.print(tmp);
        y += line_h;
      }
    } else if (_page == HomePage::RADIO) {
      display.setColor(UIColor::secondary_txt);
      display.setTextSize(hdr_size);
      int y = content_y;
      if (hdr_size == 2) {
        snprintf(tmp, sizeof(tmp), "FQ:%.3f SF:%d", _node_prefs->freq, _node_prefs->sf);
        display.setCursor(0, y); display.print(tmp); y += line_h;
        snprintf(tmp, sizeof(tmp), "BW:%.2f  CR:%d", _node_prefs->bw, _node_prefs->cr);
        display.setCursor(0, y); display.print(tmp); y += line_h;
        snprintf(tmp, sizeof(tmp), "TX:%ddBm", _node_prefs->tx_power_dbm);
        display.setCursor(0, y); display.print(tmp); y += line_h;
        snprintf(tmp, sizeof(tmp), _TR("Noise:%d", "Шум:%d"), radio_driver.getNoiseFloor());
        display.setCursor(0, y); display.print(tmp);
      } else {
        snprintf(tmp, sizeof(tmp), "FQ: %06.3f   SF: %d", _node_prefs->freq, _node_prefs->sf);
        display.setCursor(0, y); display.print(tmp); y += line_h;
        snprintf(tmp, sizeof(tmp), "BW: %03.2f     CR: %d", _node_prefs->bw, _node_prefs->cr);
        display.setCursor(0, y); display.print(tmp); y += line_h;
        snprintf(tmp, sizeof(tmp), "TX: %ddBm", _node_prefs->tx_power_dbm);
        display.setCursor(0, y); display.print(tmp); y += line_h;
        snprintf(tmp, sizeof(tmp), _TR("Noise floor: %d", "Уровень шума: %d"), radio_driver.getNoiseFloor());
        display.setCursor(0, y); display.print(tmp);
      }
#ifndef WITH_COMMS_SWITCHING
    } else if (_page == HomePage::BLUETOOTH) {
      display.setColor(UIColor::primary_txt);
      display.drawXbm((display.width() - 32) / 2, content_y,
          _task->isBluetoothEnabled() ? bluetooth_on : bluetooth_off,
          32, 32);
      display.setTextSize(hdr_size);
      display.drawTextCentered(display.width() / 2, display.height() - display.getLineH(hdr_size) + 1, UI_TOGGLE_HINT);
#endif
    } else if (_page == HomePage::ADVERT) {
      display.setColor(UIColor::primary_txt);
      display.drawXbm((display.width() - 32) / 2, content_y, advert_icon, 32, 32);
      display.setTextSize(hdr_size);
      display.drawTextCentered(display.width() / 2, display.height() - display.getLineH(hdr_size) + 1, UI_ADVERT_HINT);
#if ENV_INCLUDE_GPS == 1
    } else if (_page == HomePage::GPS) {
      LocationProvider* nmea = sensors.getLocationProvider();
      char buf[50];
      display.setTextSize(hdr_size);
      int y = content_y;
      bool gps_state = _task->getGPSState();
#ifdef PIN_GPS_SWITCH
      bool hw_gps_state = digitalRead(PIN_GPS_SWITCH);
      if (gps_state != hw_gps_state) {
        strcpy(buf, gps_state ? _TR("gps off(hw)", "gps выкл(hw)") : _TR("gps off(sw)", "gps выкл(sw)"));
      } else {
        strcpy(buf, gps_state ? _TR("gps on", "gps вкл") : _TR("gps off", "gps выкл"));
      }
#else
      strcpy(buf, gps_state ? _TR("gps on", "gps вкл") : _TR("gps off", "gps выкл"));
#endif
      display.setColor(UIColor::primary_txt);
      display.drawTextLeftAlign(0, y, buf);
      if (nmea == NULL) {
        y += line_h;
        display.drawTextLeftAlign(0, y, _TR("Can't access GPS", "GPS недоступен"));
      } else {
        display.setColor(UIColor::primary_txt);
        strcpy(buf, nmea->isValid() ? _TR("fix", "захват") : _TR("no fix", "нет захв."));
        display.drawTextRightAlign(display.width()-1, y, buf);
        y += line_h;
        display.drawTextLeftAlign(0, y, _TR("sat", "спут"));
        display.setColor(UIColor::primary_txt);
        sprintf(buf, "%d", nmea->satellitesCount());
        display.drawTextRightAlign(display.width()-1, y, buf);
        y += line_h;
        display.drawTextLeftAlign(0, y, _TR("pos", "коорд"));
        display.setColor(UIColor::primary_txt);
        sprintf(buf, "%.4f %.4f",
          nmea->getLatitude()/1000000., nmea->getLongitude()/1000000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y += line_h;
        display.drawTextLeftAlign(0, y, _TR("alt", "выс"));
        display.setColor(UIColor::primary_txt);
        sprintf(buf, "%.2f", nmea->getAltitude()/1000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
      }
#endif
#if UI_SENSORS_PAGE == 1
    } else if (_page == HomePage::SENSORS) {
      display.setTextSize(hdr_size);
      int y = content_y;
      refresh_sensors();
      char buf[30];
      char name[30];

      // How many rows fit on this display; uptime rows always appended after LPP data
      int sensors_max_rows = (display.height() - content_y) / line_h;
      if (sensors_max_rows < 1) sensors_max_rows = 1;
      _sensors_visible_rows = sensors_max_rows;
      int total_rows = sensors_nb + 2;  // +2: "uptime" and "on batt"

      // Clamp scroll offset to valid range
      int max_offset = total_rows - sensors_max_rows;
      if (max_offset < 0) max_offset = 0;
      if (sensors_scroll_offset > max_offset) sensors_scroll_offset = max_offset;

      // Skip LPP entries above the scroll window
      LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());
      int lpp_skip = sensors_scroll_offset < sensors_nb ? sensors_scroll_offset : sensors_nb;
      for (int i = 0; i < lpp_skip; i++) {
        uint8_t channel, type;
        r.readHeader(channel, type);
        r.skipData(type);
      }

      int rows_to_show = total_rows - sensors_scroll_offset;
      if (rows_to_show > sensors_max_rows) rows_to_show = sensors_max_rows;

      for (int row = 0; row < rows_to_show; row++) {
        int abs_row = sensors_scroll_offset + row;
        display.setCursor(0, y);
        float v;

        if (abs_row < sensors_nb) {
          // --- LPP sensor row ---
          uint8_t channel, type;
          r.readHeader(channel, type);
          switch (type) {
            case LPP_GPS: {
              float lat, lon, alt;
              r.readGPS(lat, lon, alt);
              strcpy(name, "gps"); sprintf(buf, "%.4f %.4f", lat, lon);
              break;
            }
            case LPP_VOLTAGE:
              r.readVoltage(v);
              strcpy(name, _TR("Voltage", "Напряж.")); sprintf(buf, "%.2f V", v);
              break;
            case LPP_CURRENT:
              r.readCurrent(v);
              strcpy(name, _TR("Current", "Ток")); sprintf(buf, "%.3f A", v);
              break;
            case LPP_TEMPERATURE:
              r.readTemperature(v);
              strcpy(name, _TR("Temp", "Темп.")); sprintf(buf, "%.1f\xb0" "C", v);
              break;
            case LPP_RELATIVE_HUMIDITY:
              r.readRelativeHumidity(v);
              strcpy(name, _TR("Humidity", "Влажн.")); sprintf(buf, "%.1f%%", v);
              break;
            case LPP_BAROMETRIC_PRESSURE:
              r.readPressure(v);
              strcpy(name, _TR("Pressure", "Давл.")); sprintf(buf, "%.1f hPa", v);
              break;
            case LPP_ALTITUDE:
              r.readAltitude(v);
              strcpy(name, _TR("Altitude", "Высота")); sprintf(buf, "%.0f m", v);
              break;
            case LPP_POWER:
              r.readPower(v);
              strcpy(name, _TR("Power", "Мощн.")); sprintf(buf, "%.2f W", v);
              break;
            default:
              r.skipData(type);
              strcpy(name, _TR("unk", "неизв")); sprintf(buf, "");
          }
        } else {
          // --- Uptime rows: computed fresh each render, never cached ---
          uint32_t secs;
          if (abs_row == sensors_nb) {
            strcpy(name, _TR("Uptime(boot)", "Работа(старт)"));
            secs = millis() / 1000;
          } else {
            bool usb = _task->isUsbConnected();
            strcpy(name, usb ? _TR("On charge", "Зарядка") : _TR("On batt", "От АКБ"));
            secs = _task->usbElapsedSecs();
          }
          formatDuration(buf, sizeof(buf), secs);
        }

        display.setCursor(0, y);
        display.print(name);
        display.setCursor(display.width() - display.getTextWidth(buf) - 1, y);
        display.print(buf);
        y += line_h;
      }
      // No auto-scroll: offset is controlled manually via KEY_UP / KEY_DOWN
#endif
    } else if (_page == HomePage::SETTINGS) {
      display.setTextSize(hdr_size);
#if defined(WITH_COMMS_SWITCHING) && defined(ENABLE_WIFI_INTERFACE)
      if (_in_wifi_select) {
        const int LINE_H_W  = display.getLineH(hdr_size) - 1;
        const int CURSOR_W  = 6 * hdr_size;
        const int visible_w = (display.height() - content_y) / LINE_H_W;

        // Command overlay: fills content area only (header stays visible)
        if (_wifi_cmd_shown) {
          display.setColor(UIColor::secondary_txt);
          display.setTextSize(1);
          display.setCursor(0, content_y);
          display.printWordWrap(_wifi_cmd, display.width());
          return 60000;
        }

        // Poll async scan completion
        if (_wifi_scanning) {
          int n = WiFi.scanComplete();
          if (n >= 0) {
            _wifi_scanning    = false;
            _wifi_scan_n      = n;
            _wifi_sel         = 0;
            _wifi_scan_scroll = 0;
          }
        }

        if (the_mesh.isWifiConnecting()) {
          display.setColor(UIColor::secondary_txt);
          display.drawTextCentered(display.width() / 2, content_y + 8, _TR("Connecting...", "Подключение..."));
        } else if (_wifi_scanning) {
          display.setColor(UIColor::secondary_txt);
          display.drawTextCentered(display.width() / 2, content_y + 8, _TR("Scanning WiFi...", "Поиск WiFi..."));
        } else {
          int net_n = _wifi_scan_n >= 0 ? _wifi_scan_n : 0;
          WifiPrefs* wp = the_mesh.getWifiPrefs();
          int S = wp->network_count;

          // Gather unsaved scanned SSID indices
          int unsaved[20];
          int N_u = 0;
          for (int si = 0; si < net_n && N_u < 20; si++) {
            String ss = WiFi.SSID(si);
            bool found = false;
            for (int k = 0; k < S; k++) {
              if (ss == wp->networks[k].ssid) { found = true; break; }
            }
            if (!found) unsaved[N_u++] = si;
          }

          // RSSI for each saved network (-200 if not visible)
          int saved_rssi[WIFI_MAX_NETWORKS];
          for (int k = 0; k < S; k++) {
            saved_rssi[k] = -200;
            for (int si = 0; si < net_n; si++) {
              if (WiFi.SSID(si) == wp->networks[k].ssid) { saved_rssi[k] = WiFi.RSSI(si); break; }
            }
          }

          // Items: 0..S-1 = saved SSIDs, S..S+N_u-1 = unsaved scan, S+N_u = BLE, +1 = USB, +2 = Back
          int total_n = S + N_u + 3;
          int max_ssid = display.width() / (6 * hdr_size) - 5;
          if (max_ssid < 4) max_ssid = 4;

          display.setColor(UIColor::primary_txt);
          for (int vi = 0; vi < visible_w; vi++) {
            int i = _wifi_scan_scroll + vi;
            if (i >= total_n) break;
            int y = content_y + vi * LINE_H_W;
            display.setTextSize(hdr_size);
            display.setCursor(0, y);
            display.print(_wifi_sel == i ? ">" : " ");
            display.setCursor(CURSOR_W, y);
            if (i < S) {
              display.print("*");
              display.setCursor(CURSOR_W * 2, y);
              char sbuf[33];
              strncpy(sbuf, wp->networks[i].ssid, max_ssid);
              sbuf[max_ssid] = '\0';
              display.print(sbuf);
              char rbuf[6];
              if (saved_rssi[i] > -200) snprintf(rbuf, sizeof(rbuf), "%4d", saved_rssi[i]);
              else                       strcpy(rbuf, " ---");
              display.drawTextRightAlign(display.width() - 1, y, rbuf);
            } else if (i < S + N_u) {
              display.print(" ");
              display.setCursor(CURSOR_W * 2, y);
              int si = unsaved[i - S];
              String ss = WiFi.SSID(si);
              char sbuf[33];
              strncpy(sbuf, ss.c_str(), max_ssid);
              sbuf[max_ssid] = '\0';
              display.print(sbuf);
              char rbuf[6];
              snprintf(rbuf, sizeof(rbuf), "%4d", WiFi.RSSI(si));
              display.drawTextRightAlign(display.width() - 1, y, rbuf);
            } else if (i == S + N_u)     { display.print("  BLE"); }
            else if (i == S + N_u + 1)   { display.print("  USB"); }
            else                          { display.print(_TR("  Back", "  Назад")); }
          }
        }
        return (_wifi_scanning || the_mesh.isWifiConnecting()) ? 400 : 200;
      } else
#endif
      if (!_in_settings) {
        display.setColor(UIColor::primary_txt);
        display.drawTextCentered(display.width() / 2, content_y + 8, _TR("Settings", "Настройки"));
        display.drawTextCentered(display.width() / 2, content_y + 24, UI_ENTER_HINT);
        // Firmware version with the commit hash, e.g. "v1.17.1-6cf6277b" — the boot
        // splash is gone in 3 s, and this is where to check what is flashed. On a
        // 64-px OLED there is no room at content_y + 40: it takes the bottom line.
        const int ver_h = display.getLineH(hdr_size) - 3;
        int ver_y = content_y + 40;
        if (ver_y + ver_h > display.height()) ver_y = display.height() - ver_h;
        if (ver_y >= content_y + 24 + ver_h) {
          display.drawTextCentered(display.width() / 2, ver_y, FIRMWARE_VERSION);
        }
      } else {
        const int CURSOR_W = 6 * hdr_size, LINE_H = display.getLineH(hdr_size) - 1, Y0 = content_y;
        _settings_visible = (display.height() - Y0) / LINE_H;
        int total_n = settingsPageItemCount(_settings_page);

        display.setColor(UIColor::primary_txt);
        int _drawn = 0;
        for (int i = _settings_scroll; i < total_n && _drawn < _settings_visible; i++) {
          if (isSettingHidden(_settings_page, i)) continue;
          int y = Y0 + (_drawn++) * LINE_H;
          display.setColor(UIColor::primary_txt);
          display.setCursor(0, y);
          display.print(_settings_sel == i ? ">" : " ");
          display.setCursor(CURSOR_W, y);
          display.print(getSettingLabel(_settings_page, i));
          char val[24] = "";
          if (getSettingValue(_settings_page, i, val, sizeof(val))) {
            display.drawTextRightAlign(display.width() - 2, y, val);
          }
        }
      }
#if !(defined(UI_NO_DISCOVER_SCREEN) && (UI_NO_DISCOVER_SCREEN + 0 != 0))
    } else if (_page == HomePage::DISCOVERY) {
      int count = (int)disc_nodes_count;
      display.setColor(UIColor::primary_txt);
      display.setTextSize(hdr_size);
      int y = content_y;
      for (int i = 0; i < count; i++, y += line_h) {
        char name[32];
        auto a = &discovered[i];
        if ((a->name[0] == 0) || !discovery_disp_names) {
          mesh::Utils::toHex(name, a->pubkey_prefix, 4);
        } else {
          strncpy(name, a->name, 32);
          name[31] = 0;
        }
        char snr_s[16];
        if (strlen(name) <= 8) { // display snr_o
          snprintf(snr_s, sizeof(snr_s), "%02.1f>%02.1f", a->snr_out, a->snr_in);
        } else {
          snprintf(snr_s, sizeof(snr_s), "%02.1f", a->snr_in);
        }
        int snr_width = display.getTextWidth(snr_s);
        int max_name_width = display.width() - snr_width - 1;
        display.drawTextEllipsized(0, y, max_name_width, name);
        display.setCursor(display.width() - snr_width - 1, y);
        display.print(snr_s);
      }
      if (millis() < discovery_req_time + 5000) {
        return 1000; // more frequent updates just after req
      } else if (count < UI_RECENT_LIST_SIZE - 1) { // show only 5 sec after last disc
        display.drawTextCentered(display.width() / 2, display.height() - display.getLineH(hdr_size) + 1, UI_DISCOVER_HINT);
      }
#endif
#ifndef UI_NO_HIBERNATE   // boards without a hibernate screen (Wio Tracker L1)
    } else if (_page == HomePage::SHUTDOWN) {
      display.setColor(UIColor::primary_txt);
      display.setTextSize(hdr_size);
      if (_shutdown_init) {
        display.drawTextCentered(display.width() / 2, content_y + 16, _TR("hibernating...", "выключение..."));
      } else {
        const int line_h = display.getLineH(hdr_size);
        if (display.height() - content_y >= 32 + 2 * line_h) {
          display.drawXbm((display.width() - 32) / 2, content_y, power_icon, 32, 32);
          display.drawTextCentered(display.width() / 2, display.height() - line_h * 2, _TR("hibernate:", "выключить:"));
          display.drawTextCentered(display.width() / 2, display.height() - line_h + 1, UI_HIBERNATE_LABEL);
        } else {
          // A small screen (64-px OLED) has no room for the label under the icon:
          // it goes to the right of it, both centred together.
          int text_w = display.getTextWidth(_TR("hibernate:", "выключить:"));
          const int label_w = display.getTextWidth(UI_HIBERNATE_LABEL);
          if (label_w > text_w) text_w = label_w;
          const int x = (display.width() - (32 + 6 + text_w)) / 2;
          const int y = content_y + (display.height() - content_y - 32) / 2;
          display.drawXbm(x, y, power_icon, 32, 32);
          display.setCursor(x + 32 + 6, y + 16 - line_h + 1);
          display.print(_TR("hibernate:", "выключить:"));
          display.setCursor(x + 32 + 6, y + 16 + 1);
          display.print(UI_HIBERNATE_LABEL);
        }
      }
#endif
    }
    if (_page == HomePage::SETTINGS && _in_settings && _pin_show_until) {
      unsigned long now = millis();
      if (now < _pin_show_until) {
        int ms = (int)(_pin_show_until - now);
        return ms < 100 ? 100 : ms;  // refresh exactly when PIN expires
      }
    }
    return 5000;   // next render after 5000 ms
  }

  bool handleInput(char c) override {
    if (uiOverlayActive()) { uiOverlayInput(c); return true; }
    // Cal ADC confirm screen. Single button: short = next action, long = run
    // it, double = leave. Joystick: arrows pick, ● runs, long ● leaves.
    if (_in_adccal) {
      if (c == KEY_NEXT || c == KEY_DOWN || c == KEY_RIGHT) {
        _adccal.step(true);
      } else if (c == KEY_UP || c == KEY_LEFT) {
        _adccal.step(false);
      } else if (c == KEY_PREV || c == KEY_CANCEL) {
        _in_adccal = false;
      } else if (c == KEY_ENTER) {
        _in_adccal = false;
        if (_node_prefs) {
          AdcCalibration::Result r = _adccal.run(board, *_node_prefs);
          if (r == AdcCalibration::APPLIED || r == AdcCalibration::RESTORED) the_mesh.savePrefs();
          if (r == AdcCalibration::APPLIED)        _task->showAlert(_TR("ADC calibrated", "АКБ откалибр."), 1500);
          else if (r == AdcCalibration::RESTORED)  _task->showAlert(_TR("ADC default", "АКБ сброшен"), 1500);
          else if (r == AdcCalibration::NO_EFFECT) _task->showAlert(_TR("ADC: no effect", "АКБ: без изм."), 1500);
        }
      }
      return true;
    }

    // On CLOCK page: intercept navigation while PMs are being read inline
    if (_page == HomePage::CLOCK && _clock_pm_pending > 0) {
      if (c == KEY_NEXT || c == KEY_RIGHT) {
        _task->consumeTopMsg();
        _clock_pm_pending--;
        return true;
      }
      if (c == KEY_LEFT || c == KEY_PREV) {
        return true;  // block page nav while reading PMs
      }
    }

#if defined(WITH_COMMS_SWITCHING) && defined(ENABLE_WIFI_INTERFACE)
    if (_page == HomePage::SETTINGS && _in_wifi_select) {
      // Any key dismisses the full-screen command overlay
      if (_wifi_cmd_shown) {
        _wifi_cmd_shown = false;
        return true;
      }
      if (_wifi_scanning) return true;  // absorb keys during scan
      if (!the_mesh.isWifiConnecting()) {
        int net_n = _wifi_scan_n >= 0 ? _wifi_scan_n : 0;
        WifiPrefs* wp = the_mesh.getWifiPrefs();
        int S = wp->network_count;
        // Rebuild unsaved indices (same logic as render)
        int unsaved[20];
        int N_u = 0;
        for (int si = 0; si < net_n && N_u < 20; si++) {
          String ss = WiFi.SSID(si);
          bool found = false;
          for (int k = 0; k < S; k++) {
            if (ss == wp->networks[k].ssid) { found = true; break; }
          }
          if (!found) unsaved[N_u++] = si;
        }
        int total_n = S + N_u + 3;

        if (c == KEY_NEXT || c == KEY_RIGHT) {
          _wifi_sel = (_wifi_sel + 1) % total_n;
          if (_wifi_sel < _wifi_scan_scroll) _wifi_scan_scroll = _wifi_sel;
          if (_wifi_sel >= _wifi_scan_scroll + 3) _wifi_scan_scroll = _wifi_sel - 2;
        } else if (c == KEY_LEFT || c == KEY_PREV) {
          the_mesh.wifiScanDone(); _wifi_scan_n = -1;
          _in_wifi_select = false;
        } else if (c == KEY_ENTER) {
          if (_wifi_sel < S) {
            // saved SSID → connect
            WiFi.scanDelete(); _wifi_scan_n = -1;
            the_mesh.switchCommsMode(COMMS_MODE_WIFI, _wifi_sel);
            _wifi_wait = true;
          } else if (_wifi_sel < S + N_u) {
            // unsaved scanned → show full-screen command overlay
            int si = unsaved[_wifi_sel - S];
            String ss = WiFi.SSID(si);
            snprintf(_wifi_cmd, sizeof(_wifi_cmd),
                     "wifi add\n%.32s\n<pass>", ss.c_str());
            _wifi_cmd_shown = true;
          } else if (_wifi_sel == S + N_u) {
            WiFi.scanDelete(); _wifi_scan_n = -1;
            the_mesh.switchCommsMode(COMMS_MODE_BLE);
            _in_wifi_select = false;
          } else if (_wifi_sel == S + N_u + 1) {
            WiFi.scanDelete(); _wifi_scan_n = -1;
            the_mesh.switchCommsMode(COMMS_MODE_USB);
            _in_wifi_select = false;
          } else {
            the_mesh.wifiScanDone(); _wifi_scan_n = -1;
            _in_wifi_select = false;
          }
        }
      }
      return true;
    }
#endif
    // Settings edit mode absorbs all navigation before standard page switching
    if (_page == HomePage::SETTINGS && _in_settings) {
      int total_n = settingsPageItemCount(_settings_page);

#ifdef UI_ENCODER_NAV
      if (c == KEY_PREV) c = KEY_UP;
      if (c == KEY_CANCEL) {
#else
      // ● long (KEY_CANCEL) or KEY_PREV = exit subpage or settings
      if (c == KEY_CANCEL || c == KEY_PREV) {
#endif
        if (_settings_page == SettingsPage::CODECS) {
          _settings_page = SettingsPage::SYSTEM;
          _settings_sel = 4;
          _settings_scroll = 0;
        } else if (_settings_page != SettingsPage::ROOT) {
          SettingsPage prev = _settings_page;
          _settings_page = SettingsPage::ROOT;
          if (prev == SettingsPage::CHAT) _settings_sel = 0;
          else if (prev == SettingsPage::DISP) _settings_sel = 1;
          else if (prev == SettingsPage::COMMS) _settings_sel = 2;
          else if (prev == SettingsPage::SYSTEM) _settings_sel = 3;
          _settings_scroll = 0;
        } else {
          _in_settings = false;
        }
        return true;
      }
      // ↑ (KEY_UP) = previous item, with wrap-around scroll
      if (c == KEY_UP) {
        _settings_sel = (_settings_sel + total_n - 1) % total_n;
        while (isSettingHidden(_settings_page, _settings_sel)) _settings_sel = (_settings_sel + total_n - 1) % total_n;
        if (_settings_sel == total_n - 1) {
          _settings_scroll = (total_n > _settings_visible) ? (total_n - _settings_visible) : 0;
        } else if (_settings_sel < _settings_scroll) {
          _settings_scroll = _settings_sel;
        }
        return true;
      }
      // ↓ (KEY_DOWN) or KEY_NEXT = next item, with wrap-around scroll
      if (c == KEY_DOWN || c == KEY_NEXT) {
        _settings_sel = (_settings_sel + 1) % total_n;
        while (isSettingHidden(_settings_page, _settings_sel)) _settings_sel = (_settings_sel + 1) % total_n;
        if (_settings_sel == 0) _settings_scroll = 0;
        else if (_settings_sel >= _settings_scroll + _settings_visible) _settings_scroll = _settings_sel - _settings_visible + 1;
        return true;
      }
      // → (KEY_RIGHT) or ● short (KEY_ENTER) = cycle value forward
      // ← (KEY_LEFT) = cycle value backward
      // For binary items direction doesn't matter; for multi-value LEFT reverses.
      if (c == KEY_RIGHT || c == KEY_ENTER || c == KEY_LEFT) {
        bool fwd = (c != KEY_LEFT);
        onSettingPress(_settings_page, _settings_sel, fwd);
        return true;
      }
      return true;  // absorb all other keys in edit mode
    }

#if UI_SENSORS_PAGE == 1
    // On SENSORS page: UP/DOWN scroll the list; LEFT/RIGHT still navigate pages
    if (_page == HomePage::SENSORS && (sensors_nb + 2) > _sensors_visible_rows) {
      if (c == KEY_UP) {
        if (sensors_scroll_offset > 0) sensors_scroll_offset--;
        return true;
      }
      if (c == KEY_DOWN) {
        if (sensors_scroll_offset < sensors_nb + 2 - _sensors_visible_rows)
          sensors_scroll_offset++;
        return true;
      }
    }
#endif
    if (c == KEY_LEFT || c == KEY_PREV || c == KEY_UP) {
      _page = (_page + HomePage::Count - 1) % HomePage::Count;
#if UI_SENSORS_PAGE == 1
      if (_page == HomePage::SENSORS) sensors_scroll_offset = 0;
#endif
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT || c == KEY_DOWN) {
      _page = (_page + 1) % HomePage::Count;
#if UI_SENSORS_PAGE == 1
      if (_page == HomePage::SENSORS) sensors_scroll_offset = 0;
#endif
      return true;
    }
#ifndef WITH_COMMS_SWITCHING
    if (c == KEY_ENTER && _page == HomePage::BLUETOOTH) {
      if (_task->isBluetoothEnabled()) {  // toggle Bluetooth on/off
        _task->disableBluetooth();
      } else {
        _task->enableBluetooth();
      }
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::ADVERT) {
      _task->notify(UIEventType::ack);
      if (the_mesh.advert()) {
        _task->showAlert(_TR("Advert sent!", "Анонс отправлен"), 1000);
      } else {
        _task->showAlert(_TR("Advert failed..", "Сбой анонса.."), 1000);
      }
      return true;
    }
#if ENV_INCLUDE_GPS == 1
    if (c == KEY_ENTER && _page == HomePage::GPS) {
      _task->toggleGPS();
      return true;
    }
#endif
#if UI_SENSORS_PAGE == 1
    if (c == KEY_ENTER && _page == HomePage::SENSORS) {
      _task->toggleGPS();
      next_sensors_refresh=0;
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::SETTINGS) {
      _in_settings = true;
      _settings_page = SettingsPage::ROOT;
      _settings_sel = 0;
      _settings_scroll = 0;
      return true;
    }
#if !(defined(UI_NO_DISCOVER_SCREEN) && (UI_NO_DISCOVER_SCREEN + 0 != 0))
    if (c == KEY_ENTER && _page == HomePage::DISCOVERY) {
      if (millis() > discovery_req_time + 5000) { // rate limiter
        if (sendDiscoverRequest()) discovery_req_time = millis();
      }
      return true;
    }
    if (c == KEY_SELECT && _page == HomePage::DISCOVERY) {
      discovery_disp_names = !discovery_disp_names;
      return true;
    }
#endif
#ifndef UI_NO_HIBERNATE
    if (c == HIBERNATE_KEY && _page == HomePage::SHUTDOWN) {
      _shutdown_init = true;  // need to wait for button to be released
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::FIRST) {
      if (_task->getUnreadMsgCount() > 0) {
        _task->gotoMsgPreview();
        return true;
      }
      if (_task->getLogCount() > 0) {
        _task->gotoMsgHistory();
        return true;
      }
      return false;
    }
    return false;
  }
};

class MsgPreviewScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;

  #define MAX_UNREAD_MSGS   64   // compile-time array capacity; runtime cap via _max_unread
  #define MAX_LOG_MSGS      64   // compile-time array capacity; runtime cap via _max_log
  int num_unread;
  int head = MAX_UNREAD_MSGS - 1; // index of latest unread message

public:
  struct MsgEntry {
    uint32_t timestamp;
    uint8_t  path_len;       // 0xFF = direct, otherwise hop count (group)
    bool     is_pm;
    char     from_name[32];  // sender name (direct) or channel name (group)
    char     msg[MAX_TEXT_LEN];
  };

  MsgPreviewScreen(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) {
    num_unread = 0;
    for (int i = 0; i < MAX_UNREAD_MSGS; i++) unread[i] = nullptr;
    for (int i = 0; i < MAX_LOG_MSGS; i++) _log[i] = nullptr;
  }

  ~MsgPreviewScreen() {
    for (int i = 0; i < MAX_UNREAD_MSGS; i++) {
      if (unread[i]) { delete unread[i]; unread[i] = nullptr; }
    }
    for (int i = 0; i < MAX_LOG_MSGS; i++) {
      if (_log[i]) { delete _log[i]; _log[i] = nullptr; }
    }
  }

  bool _in_history  = false;
  int  _hist_cursor = 0;

  int _max_unread = 32;  // runtime cap, changed via setMaxSizes()
  int _max_log    = 32;

  void setMaxSizes(int max_unread, int max_log) {
    _max_unread = constrain(max_unread, 1, MAX_UNREAD_MSGS);
    _max_log    = constrain(max_log,    1, MAX_LOG_MSGS);
  }

private:
  MsgEntry* unread[MAX_UNREAD_MSGS];
  MsgEntry* _log[MAX_LOG_MSGS];
  int _log_head  = 0;
  int _log_count = 0;

  int indexFromNewest(int offset) const {
    return (head - offset + MAX_UNREAD_MSGS) % MAX_UNREAD_MSGS;
  }

  void pushEntry(const MsgEntry& entry) {
    head = (head + 1) % MAX_UNREAD_MSGS;
    if (num_unread < _max_unread) num_unread++;
    if (!unread[head]) unread[head] = new MsgEntry();
    if (unread[head]) *unread[head] = entry;
  }

public:
  void addPreview(uint8_t path_len, const char* from_name, const char* msg, bool is_pm) {
    MsgEntry entry = {};
    auto p = &entry;
    p->timestamp = _rtc->getCurrentTime();
    p->path_len  = path_len;
    p->is_pm     = is_pm;
    strncpy(p->from_name, from_name, sizeof(p->from_name) - 1);
    p->from_name[sizeof(p->from_name) - 1] = '\0';
    StrHelper::strncpy(p->msg, msg, sizeof(p->msg));
    pushEntry(entry);
    if (!_log[_log_head]) _log[_log_head] = new MsgEntry();
    if (_log[_log_head]) *_log[_log_head] = entry;
    _log_head = (_log_head + 1) % MAX_LOG_MSGS;
    if (_log_count < _max_log) _log_count++;
    _in_history = false;  // new message interrupts history browse
  }

  int  logCount()       const { return _log_count; }
  void enterHistoryMode()     { _in_history = true; _hist_cursor = 0; }

  // Peek at the current (latest) unread message without consuming it
  const MsgEntry* peekCurrent() const {
    if (num_unread == 0 || !unread[head]) return nullptr;
    return unread[head];
  }

  const MsgEntry* peekLatestPM() const {
    for (int i = 0; i < num_unread; i++) {
      const MsgEntry* p = unread[indexFromNewest(i)];
      if (p && p->is_pm) return p;
    }
    return nullptr;
  }

  // Advance past the current message (mark as read) without switching screens
  void consumeOne() {
    if (num_unread == 0) return;
    head = (head + MAX_UNREAD_MSGS - 1) % MAX_UNREAD_MSGS;
    num_unread--;
  }

  bool consumeLatestPM() {
    int target = -1;
    for (int i = 0; i < num_unread; i++) {
      const MsgEntry* p = unread[indexFromNewest(i)];
      if (p && p->is_pm) {
        target = i;
        break;
      }
    }
    if (target < 0) return false;

    MsgEntry* kept[MAX_UNREAD_MSGS];
    MsgEntry* target_ptr = unread[indexFromNewest(target)];
    int kept_count = 0;
    for (int i = 0; i < num_unread; i++) {
      if (i == target) continue;
      kept[kept_count++] = unread[indexFromNewest(i)];
    }

    num_unread = 0;
    head = MAX_UNREAD_MSGS - 1;
    for (int i = kept_count - 1; i >= 0; i--) {
      head = (head + 1) % MAX_UNREAD_MSGS;
      unread[head] = kept[i];
      num_unread++;
    }
    if (target_ptr) delete target_ptr;
    return true;
  }

  int unreadCount() const { return num_unread; }

  int render(DisplayDriver& display) override {
    if (_in_history) {
      if (_log_count == 0) { _in_history = false; return 1000; }
      int idx = (_log_head - _log_count + _hist_cursor + MAX_LOG_MSGS * 2) % MAX_LOG_MSGS;
      const MsgEntry* p = _log[idx];
      if (!p) return 1000;

      int hdr_size   = (display.width() >= 200) ? 2 : 1;
      int hdr_line_h = display.getLineH(hdr_size) - 3;
      int msg_start_y = hdr_line_h + 3;

      char time_str[8];
      int secs = (int)(_rtc->getCurrentTime() - p->timestamp);
      if (secs < 0) secs = 0;
      if      (secs < 60)   snprintf(time_str, sizeof(time_str), _TR("%ds", "%dс"),  secs);
      else if (secs < 3600) snprintf(time_str, sizeof(time_str), _TR("%dm", "%dм"),  secs / 60);
      else                  snprintf(time_str, sizeof(time_str), _TR("%dh", "%dч"),  secs / 3600);

      int hdr_ch_w    = 6 * hdr_size;
      int hdr_total   = display.width() / hdr_ch_w;
      int name_budget = hdr_total - (int)strlen(time_str) - 6; // "[N/M] " prefix
      if (name_budget < 0) name_budget = 0;

      char raw_name[36];
      if (p->path_len == 0xFF)
        snprintf(raw_name, sizeof(raw_name), "(D)%s", p->from_name);
      else
        snprintf(raw_name, sizeof(raw_name), "[%d]%s", p->path_len, p->from_name);
      utf8_truncate(raw_name, name_budget);

      char hdr_left[64];
      snprintf(hdr_left, sizeof(hdr_left), "#%d/%d %s",
               _hist_cursor + 1, _log_count, raw_name);

      display.setTextSize(hdr_size);
      display.setColor(UIColor::secondary_txt);
      display.setCursor(0, 0);
      display.print(hdr_left);
      int time_x = display.width() - 1 - (int)strlen(time_str) * hdr_ch_w;
      display.setCursor(time_x, 0);
      display.print(time_str);

      display.setColor(UIColor::primary_txt);
      display.drawRect(0, hdr_line_h + 2, display.width(), 1);

      if (!uiDrawMessageContent(display, p->msg, 0, msg_start_y, display.width(), display.height() - msg_start_y)) {
        display.setTextSize(1);
        display.setCursor(0, msg_start_y);
        display.printWordWrap(p->msg, display.width());
      }

      return (AUTO_OFF_MILLIS == 0) ? 10000 : 1000;
    }
    if (num_unread == 0) return 1000;
    const MsgEntry* p = unread[head];
    if (!p) return 1000;

    // 1. Metrics
    int hdr_size    = (display.width() >= 200) ? 2 : 1;
    int hdr_line_h  = display.getLineH(hdr_size) - 3;
    int msg_start_y = hdr_line_h + 3;

    // 2. Time string
    char time_str[8];
    int secs = (int)(_rtc->getCurrentTime() - p->timestamp);
    if (secs < 0) secs = 0;
    if      (secs < 60)   snprintf(time_str, sizeof(time_str), _TR("%ds", "%dс"),  secs);
    else if (secs < 3600) snprintf(time_str, sizeof(time_str), _TR("%dm", "%dм"),  secs / 60);
    else                  snprintf(time_str, sizeof(time_str), _TR("%dh", "%dч"),  secs / 3600);

    // 3. Header left: "#N (D)name" for direct, "#N channel" for group
    int hdr_ch_w    = 6 * hdr_size;
    int hdr_total   = display.width() / hdr_ch_w;
    int name_budget = hdr_total - (int)strlen(time_str) - 4;  // 4 = "#N " + gap
    if (name_budget < 0) name_budget = 0;

    char raw_name[36];
    if (p->path_len == 0xFF)
      snprintf(raw_name, sizeof(raw_name), "(D)%s", p->from_name);
    else
      snprintf(raw_name, sizeof(raw_name), "[%d]%s", p->path_len, p->from_name);
    utf8_truncate(raw_name, name_budget);

    char hdr_left[64];
    snprintf(hdr_left, sizeof(hdr_left), "#%d %s", num_unread, raw_name);

    // 4. Render header
    display.setTextSize(hdr_size);
    display.setColor(UIColor::primary_txt);
    display.setCursor(0, 0);
    display.print(hdr_left);
    int time_x = display.width() - 1 - (int)strlen(time_str) * hdr_ch_w;
    display.setCursor(time_x, 0);
    display.print(time_str);

    // 5. Divider
    display.setColor(UIColor::primary_txt);
    display.drawRect(0, hdr_line_h + 2, display.width(), 1);

    if (!uiDrawMessageContent(display, p->msg, 0, msg_start_y, display.width(), display.height() - msg_start_y)) {
      display.setTextSize(1);
      display.setCursor(0, msg_start_y);
      display.printWordWrap(p->msg, display.width());
    }

    return (AUTO_OFF_MILLIS == 0) ? 10000 : 1000;
  }

  bool handleInput(char c) override {
    if (_in_history) {
      // → / ↓ = newer message (or exit at the newest end)
      if (c == KEY_NEXT || c == KEY_RIGHT || c == KEY_DOWN) {
        _hist_cursor++;
        if (_hist_cursor >= _log_count) {
          _in_history = false;
          _task->gotoHomeScreen();
        }
        return true;
      }
      // ← / ↑ = previous (older) message
      if (c == KEY_PREV || c == KEY_LEFT || c == KEY_UP) {
        if (_hist_cursor > 0) _hist_cursor--;
        return true;
      }
      // ● short / ● long = exit history
      if (c == KEY_ENTER || c == KEY_CANCEL) {
        _in_history = false;
        _task->gotoHomeScreen();
        return true;
      }
      return false;
    }
    // → = next unread (dismiss current)
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      head = (head + MAX_UNREAD_MSGS - 1) % MAX_UNREAD_MSGS;
      num_unread--;
      if (num_unread == 0) {
        // All unread dismissed — slide into history at the newest message
        // so the user can confirm they've read it and navigate back if needed.
        if (_log_count > 0) {
          _in_history    = true;
          _hist_cursor   = _log_count - 1;  // newest entry
        } else {
          _task->gotoHomeScreen();
        }
      }
      return true;
    }
    // ← = выход (back to home)
    if (c == KEY_LEFT) {
      _task->gotoHomeScreen();
      return true;
    }
    // ↓ = clear all unread messages
    if (c == KEY_DOWN) {
      num_unread = 0;
      head = MAX_UNREAD_MSGS - 1;
      _task->gotoHomeScreen();
      return true;
    }
    // KEY_UP reserved for quick reply (TODO: implement)
    if (c == KEY_ENTER || c == KEY_CANCEL) {
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _auto_off = millis() + AUTO_OFF_MILLIS;

#ifdef DISPLAY_TZ
  setenv("TZ", DISPLAY_TZ, 1);
  tzset();
#endif

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if UI_HAS_JOYSTICK
  joystick_left.begin();
  joystick_right.begin();
  back_btn.begin();
  // Enable GPIO interrupts so presses during eInk refresh (~230ms busy-wait) are
  // not lost. Directional inputs stay polled; analog buttons are excluded inside
  // enableInterrupt() (threshold != 0).
  user_btn.enableInterrupt();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
#endif

  _node_prefs = node_prefs;
#ifdef UI_HAS_STATUS_LEDS
  status_leds.begin();
  applyLedPrefs();
#endif
  if (_node_prefs != NULL) {
    setClockDimMode(_node_prefs->ui_clock_dim_mode);
  }

  if (_display != NULL) {
    // Pre-set rotation BEFORE turnOn() so GxEPDDisplay::begin() initialises the panel
    // with the correct scan direction from the very first display() call.
    if (_node_prefs != NULL) {
      _display->setRotation(_node_prefs->ui_display_rotation);
    }
    uiBegin(*_display, _node_prefs);
    _display->turnOn();

    _display->startFrame();
    _display->clear();
    _display->endFrame();
  }

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
  buzzer.startup();
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
  _alert_expiry = 0;
  _prev_usb_powered = _board->isExternalPowered();
  _usb_connected = _prev_usb_powered;
  _usb_event_ms = 0;
  // Restore on-batt/on-charge elapsed from prefs (bit 31 = USB state, bits 30-0 = seconds)
  _usb_elapsed_base = 0;
  if (_node_prefs) {
    bool stored_usb = (_node_prefs->ui_charge_uptime_base >> 31) & 1;
    uint32_t stored_secs = _node_prefs->ui_charge_uptime_base & 0x7FFFFFFF;
    if (_usb_connected == stored_usb)
      _usb_elapsed_base = stored_secs;
  }
  _next_prefs_save = millis() + 300000;

  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  msg_preview = new MsgPreviewScreen(this, &rtc_clock);
  // Apply runtime buffer sizes from prefs
  if (_node_prefs != NULL) {
    static const int size_table[3] = { 16, 32, 64 };
    int mu = size_table[constrain(_node_prefs->ui_max_unread_idx, 0, 2)];
    int ml = size_table[constrain(_node_prefs->ui_max_log_idx,    0, 2)];
    ((MsgPreviewScreen*)msg_preview)->setMaxSizes(mu, ml);
  }
  setCurrScreen(splash);
}

void UITask::setClockDimMode(int m) {
  _clock_dim_mode = constrain(m, 0, 1);
  if (_node_prefs != NULL) {
    _node_prefs->ui_clock_dim_mode = _clock_dim_mode;
  }
}

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
}

void UITask::_saveElapsedToPrefs(bool force) {
  if (!_node_prefs) return;
  if (!force && millis() < _next_prefs_save) return;
  uint32_t cur_secs = _usb_elapsed_base + (millis() - _usb_event_ms) / 1000;
  _node_prefs->ui_charge_uptime_base = (_usb_connected ? (1u << 31) : 0u) | (cur_secs & 0x7FFFFFFF);
  the_mesh.savePrefs();
  _next_prefs_save = millis() + 60000;  // min 60s between saves
}

void UITask::updateUsbState(bool force_refresh) {
  bool usb_now = _board->isExternalPowered();
  if (usb_now != _prev_usb_powered) {
    _usb_connected = usb_now;
    _usb_event_ms = millis();
    _usb_elapsed_base = 0;
    _saveElapsedToPrefs(true);  // force-save immediately on USB state change
    if (force_refresh) {
      _next_refresh = 0;
    }
  }
  _prev_usb_powered = usb_now;
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
switch(t){
  case UIEventType::contactMessage:
    // gemini's pick
    buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
    break;
  case UIEventType::channelMessage:
    buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
  case UIEventType::roomMessage:
  case UIEventType::newContactMessage:
  case UIEventType::none:
  default:
    break;
}
#endif

#ifdef PIN_VIBRATION
  // Trigger vibration for all UI events except none
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
  _next_refresh = 0;  // always trigger a redraw on any notification
}


void UITask::onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) {
  // we only want to alert on text messages, not cli data
  if (!(txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_SIGNED_PLAIN)) return;
  if (!hasConnection()) notify(UIEventType::contactMessage);
}

void UITask::onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) {
  if (!hasConnection()) notify(UIEventType::channelMessage);
}

void UITask::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  if (!hasConnection()) notify(UIEventType::newContactMessage);
}

void UITask::onControlDataRecv(const mesh::Packet* packet) {
#if !(defined(UI_NO_DISCOVER_SCREEN) && (UI_NO_DISCOVER_SCREEN + 0 != 0))
  if (packet->payload_len >= 14 && (packet->payload[0] & 0xF0) == CTL_TYPE_NODE_DISCOVER_RESP) {
    ((HomeScreen *) home)->handleDiscoverResponse(packet);
  }
#endif
}

void UITask::onQueueSizeChanged(int msgcount) {
  _msgcount = msgcount;
#ifdef UI_HAS_STATUS_LEDS
  _led_unread.queueChanged(msgcount, getUnreadMsgCount());
#endif
  if (msgcount != 0) return;
  // Queue emptied (the app has taken the messages). A preview that has only just
  // come up stays until the MSG HOLD time after the message arrived, unless the
  // user leaves it first; see loop().
  const unsigned long hold = msgHoldMillis(_node_prefs);
  if (curr == msg_preview && millis() - _preview_shown_at < hold) {
    _preview_close_at = _preview_shown_at + hold;
    if (_preview_close_at == 0) _preview_close_at = 1;
  } else {
    gotoHomeScreen();
  }
}

void UITask::onShowMessage(uint8_t path_len, const char* from_name, const char* text, int msgcount, bool is_pm) {
  _msgcount = msgcount;
  _preview_shown_at = millis();
  _preview_close_at = 0;

  bool on_clock = (curr == home) && ((HomeScreen*)home)->isOnClockPage();

  ((MsgPreviewScreen*)msg_preview)->addPreview(path_len, from_name, text, is_pm);
#ifdef UI_HAS_STATUS_LEDS
  _led_unread.arrived();   // also covers a full unread buffer
#endif

  if (on_clock) {
    int pm_mode = ((HomeScreen*)home)->getPmClockMode();
    if (pm_mode == 1 && is_pm) {
      // PM inline: stay on clock page, show PM overlay
      ((HomeScreen*)home)->incrementClockPM();
      _next_refresh = 100;
    } else if (pm_mode == 0) {
      // All messages switch to msg_preview
      setCurrScreen(msg_preview);
    }
    // pm_mode==1 && !is_pm: silently queued, no screen switch
  } else {
    setCurrScreen(msg_preview);
  }

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;  // extend the auto-off timer
      _next_refresh = 100;  // trigger refresh
    }
  }
}

bool UITask::peekTopMsg(ClockPMInfo& out) const {
  if (!msg_preview) return false;
  const MsgPreviewScreen::MsgEntry* p = ((MsgPreviewScreen*)msg_preview)->peekLatestPM();
  if (!p) return false;
  out.path_len  = p->path_len;
  out.timestamp = p->timestamp;
  strncpy(out.from_name, p->from_name, sizeof(out.from_name) - 1);
  out.from_name[sizeof(out.from_name) - 1] = '\0';
  strncpy(out.msg, p->msg, sizeof(out.msg) - 1);
  out.msg[sizeof(out.msg) - 1] = '\0';
  return true;
}

void UITask::consumeTopMsg() {
  if (msg_preview) ((MsgPreviewScreen*)msg_preview)->consumeLatestPM();
}

int UITask::getUnreadMsgCount() const {
  if (!msg_preview) return 0;
  return ((MsgPreviewScreen*)msg_preview)->unreadCount();
}

int UITask::getLogCount() const {
  if (!msg_preview) return 0;
  return ((MsgPreviewScreen*)msg_preview)->logCount();
}

void UITask::gotoMsgHistory() {
  if (!msg_preview) return;
  ((MsgPreviewScreen*)msg_preview)->enterHistoryMode();
  setCurrScreen(msg_preview);
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_msgcount > 0) {
        last_led_increment = LED_ON_MSG_MILLIS;
      } else {
        last_led_increment = LED_ON_MILLIS;
      }
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
  }
#endif
}

void UITask::setCurrScreen(UIScreen* c) {
  if (c != msg_preview) _preview_close_at = 0;   // left the preview: nothing to close later
  curr = c;
  _next_refresh = 100;
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart){

  #ifdef PIN_BUZZER
  /* note: we have a choice here -
     we can do a blocking buzzer.loop() with non-deterministic consequences
     or we can set a flag and delay the shutdown for a couple of seconds
     while a non-blocking buzzer.loop() plays out in UITask::loop()
  */
  buzzer.shutdown();
  uint32_t buzzer_timer = millis(); // fail-safe shutdown
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();

  #endif // PIN_BUZZER

  if (restart) {
    _saveElapsedToPrefs(true);  // force-save regardless of timer
    _board->reboot();
  } else {
    // A last frame saying the node is off and how to turn it on; on e-ink it
    // stays on the panel.
    if (_display != NULL) {
      power_screens::drawPowerOff(_display, _low_battery_off);
      _display->turnOff();   // after the last e-ink frame this keeps the image
    }
#if defined(NRF52_POWER_MANAGEMENT)
    // Arms the wake on USB power / a recovered battery (configureVoltageWake).
    if (_low_battery_off) static_cast<NRF52Board&>(board).initiateShutdown(SHUTDOWN_REASON_LOW_VOLTAGE);
#endif
    // Power off board including radio, display, GPS and components
    _board->powerOff();
  }
}

void UITask::setDisplayRotation(uint8_t r) {
  if (_display != NULL) {
    _display->setRotation(r);
  }
}

void UITask::updateMsgMaxSizes(int max_unread, int max_log) {
  if (msg_preview) {
    ((MsgPreviewScreen*)msg_preview)->setMaxSizes(max_unread, max_log);
  }
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {
  char c = 0;
  if (_preview_close_at && (long)(millis() - _preview_close_at) >= 0) {
    _preview_close_at = 0;
    if (curr == msg_preview && _msgcount == 0) gotoHomeScreen();
  }
#if UI_HAS_JOYSTICK
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  }
  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_LEFT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_LEFT);
  }
  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_RIGHT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_RIGHT);
  }
  ev = back_btn.check();
  if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#elif defined(PIN_USER_BTN) && defined(UI_ENCODER_NAV)
  // encoder press: short = enter, long / double = back; turning scrolls (see rotary below)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_CANCEL);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_CANCEL);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#elif defined(PIN_USER_BTN)
  int ev = user_btn.check();
  #ifdef UI_HAS_NAV_INPUT
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    if (_display) _display->turnOff();
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_SELECT);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
  #else
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_PREV);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
  #endif
#endif
#if defined(UI_HAS_ROTARY_INPUT)
  RotaryInputEvent rotaryEv = rotary_input.poll();
#ifdef UI_ENCODER_NAV
  // turning the encoder while the screen is off wakes it (the step itself is consumed)
  if (c == 0 && rotaryEv != RotaryInputEvent::None && _display != NULL && !_display->isOn()) {
    checkDisplayOn(KEY_NEXT);
    rotaryEv = RotaryInputEvent::None;
  }
#endif
  if (c == 0 && _display != NULL && _display->isOn()) {
    if (rotaryEv == RotaryInputEvent::Next) {
      c = KEY_NEXT;
    } else if (rotaryEv == RotaryInputEvent::Prev) {
      c = KEY_PREV;
    }
  }
#endif
#if defined(PIN_USER_BTN_ANA)
  if (abs(millis() - _analogue_pin_read_millis) > 10) {
    int ev = analog_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      c = checkDisplayOn(KEY_NEXT);
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      c = handleLongPress(KEY_ENTER);
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      c = handleDoubleClick(KEY_PREV);
    } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
      c = handleTripleClick(KEY_SELECT);
    }
    _analogue_pin_read_millis = millis();
  }
#endif
#if defined(BACKLIGHT_BTN)
  if (millis() > next_backlight_btn_check) {
    bool touch_state = digitalRead(PIN_BUTTON2);
#if defined(DISP_BACKLIGHT)
    digitalWrite(DISP_BACKLIGHT, !touch_state);
#elif defined(EXP_PIN_BACKLIGHT)
    expander.digitalWrite(EXP_PIN_BACKLIGHT, !touch_state);
#endif
    next_backlight_btn_check = millis() + 300;
  }
#endif

  if (c != 0 && curr) {
#ifdef UI_HAS_STATUS_LEDS
    if (curr == msg_preview) _led_unread.read(getUnreadMsgCount());
#endif
    curr->handleInput(c);
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
  }

  userLedHandler();
#ifdef UI_HAS_STATUS_LEDS
  StatusLedState leds_now;
  #ifdef SX126X_TXEN
  leds_now.tx = digitalRead(SX126X_TXEN) == HIGH;   // RadioLib drives it HIGH while transmitting
  #else
  leds_now.tx = false;
  #endif
  leds_now.rx_count = radio_driver.getPacketsRecv();
  // breathe only for messages the user has not acted on yet; the screen alone waking
  // up for a message does not count as seen (the node may be on a belt)
  leds_now.unread = _led_unread.sample(getUnreadMsgCount());
  LocationProvider* loc = sensors.getLocationProvider();
  leds_now.gps_on = loc != NULL && getGPSState();
  leds_now.gps_fix = leds_now.gps_on && loc->isValid();
  status_leds.update(leds_now);
#endif

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  // Timers on the sensors page use this state, so update it before rendering.
  updateUsbState(true);

  _saveElapsedToPrefs();  // saves at most once per 60s; turnOff() also triggers this

  if (curr) curr->poll();

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry) {  // render alert popup
        _display->setTextSize(1);
        int y = _display->height() / 3;
        int p = _display->height() / 32;
        _display->setColor(UIColor::popup_bkg);
        _display->fillRect(p, y, _display->width() - p*2, y);
        _display->setColor(UIColor::popup_txt);  // draw box border
        _display->drawRect(p, y, _display->width() - p*2, y);
        _display->drawTextCentered(_display->width() / 2, y + p*3, _alert);
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        _next_refresh = millis() + delay_millis;
      }
      if (_display->isEink() && home)
        _display->setFullRefreshSuppressed(((HomeScreen*)home)->isOnClockPage());
      _display->endFrame();
    }
#if AUTO_OFF_MILLIS > 0
#ifdef KEEP_DISPLAY_ON_USB
    // Opt-in: refresh the auto-off deadline while externally powered, so the
    // timer counts from the moment external power is removed. Off by default
    // because OLED panels burn in quickly; only enable for LCD targets or
    // where the display is replaceable.
    if (board.isExternalPowered()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;
    }
#endif
    if (_clock_dim_mode == 1) {
      // OFF: no dim while on CLOCK page
      if (home && ((HomeScreen*)home)->isOnClockPage())
        _auto_off = millis() + AUTO_OFF_MILLIS;
      if (millis() > _auto_off) { _saveElapsedToPrefs(); _display->turnOff(); }
    } else {
      // ON: normal auto-off
      if (millis() > _auto_off) { _saveElapsedToPrefs(); _display->turnOff(); }
    }
#endif
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

  static LowBatteryGuard low_batt_guard;
  const uint16_t shutdown_mv = _node_prefs ? _node_prefs->shutdown_mv : 0;
  if (low_batt_guard.due(board, shutdown_mv)) {
    _low_battery_off = true;
    shutdown();
  }
}

#ifdef UI_HAS_STATUS_LEDS
void UITask::applyLedPrefs() {
  if (_node_prefs) {
    status_leds.configure(_node_prefs->ui_led_enabled, _node_prefs->ui_led_msg_enabled, _node_prefs->ui_led_level);
  }
}
#endif

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn()) {
      _display->turnOn();   // turn display on and consume event
      c = 0;
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 0;  // trigger refresh
  }
  return c;
}

char UITask::handleLongPress(char c) {
  // Rescue mode kills the companion protocol, and the entry window is exactly
  // when a user is most likely to be prodding a freshly powered node. Make the
  // same gesture the way back out, and say on screen that it happened -
  // otherwise the only symptom is a phone app that stopped working.
  if (the_mesh.isCLIRescue()) {
    the_mesh.exitCLIRescue();
    showAlert("CLI Rescue OFF", 1500);
    return 0;
  }
  if (millis() - ui_started_at < 8000) {   // long press in first 8 seconds since startup -> CLI/rescue
#ifdef WITH_COMMS_SWITCHING
    // USB mode: the same Serial carries the app's companion frames, so the
    // rescue CLI (which prints its banner and echoes) stays off.
    if (the_mesh.isUsbComms()) return c;
#endif
    the_mesh.enterCLIRescue();
    showAlert("CLI RESCUE", 2000);
    c = 0;   // consume event
  }
  return c;
}

char UITask::handleDoubleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: double-click triggered");
  checkDisplayOn(c);
  return c;
}

char UITask::handleTripleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: triple click triggered");
  checkDisplayOn(c);
  toggleBuzzer();
  c = 0;
  return c;
}

bool UITask::getGPSState() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        return !strcmp(_sensors->getSettingValue(i), "1");
      }
    }
  }
  return false;
}

void UITask::toggleGPS() {
    if (_sensors != NULL) {
    // toggle GPS on/off
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        if (strcmp(_sensors->getSettingValue(i), "1") == 0) {
          _sensors->setSettingValue("gps", "0");
          _node_prefs->gps_enabled = 0;
          notify(UIEventType::ack);
        } else {
          _sensors->setSettingValue("gps", "1");
          _node_prefs->gps_enabled = 1;
          notify(UIEventType::ack);
        }
        the_mesh.savePrefs();
        showAlert(_node_prefs->gps_enabled ? _TR("GPS: Enabled", "GPS: Включен") : _TR("GPS: Disabled", "GPS: Выключен"), 800);
        _next_refresh = 0;
        break;
      }
    }
  }
}

void UITask::toggleBuzzer() {
    // Toggle buzzer quiet mode
  #ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
    } else {
      buzzer.quiet(true);
    }
    _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? _TR("Buzzer: OFF", "Зуммер: ВЫКЛ") : _TR("Buzzer: ON", "Зуммер: ВКЛ"), 800);
    _next_refresh = 0;  // trigger refresh
  #endif
}
