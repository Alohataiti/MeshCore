#pragma once
#ifdef UI_HAS_STATUS_LEDS
#include <helpers/ui/UnreadLedLatch.h>
#endif

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>
#include <helpers/sensors/LPPDataHelpers.h>

#ifndef LED_STATE_ON
  #define LED_STATE_ON 1
#endif

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

#if !(defined(UI_NO_DISCOVER_SCREEN) && (UI_NO_DISCOVER_SCREEN + 0 != 0))
// A node that answered the "discover repeaters" request (home screen page).
struct DiscoveredNode {
  uint8_t pubkey_prefix[9];
  float snr_in;
  float snr_out;
  char name[32];
  uint8_t type;
};
#endif

class UITask : public AbstractUITask {
  DisplayDriver* _display;
  SensorManager* _sensors;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif
  unsigned long _next_refresh, _auto_off;
  unsigned long _preview_shown_at = 0;   // millis() a message last came in
  unsigned long _preview_close_at = 0;   // deferred return home after the app read it; 0 = none
  NodePrefs* _node_prefs;
  int  _clock_dim_mode  = 0;   // CLOCK DIM: 0 = ON (auto-off as usual), 1 = OFF (clock page stays lit)
  char _alert[80];
  unsigned long _alert_expiry;
  int _msgcount;
#ifdef UI_HAS_STATUS_LEDS
  UnreadLedLatch _led_unread;
#endif
  unsigned long ui_started_at;
  bool _low_battery_off = false; // shutdown() was called for a flat battery
  int next_backlight_btn_check = 0;
  bool _prev_usb_powered = false;
  bool _usb_connected = false;
  uint32_t _usb_event_ms = 0;      // millis() when USB state last changed (current session)
  uint32_t _usb_elapsed_base = 0;  // seconds accumulated before current session
  uint32_t _next_prefs_save = 0;   // millis() for next periodic prefs flush
#ifdef PIN_STATUS_LED
  int led_state = 0;
  int next_led_change = 0;
  int last_led_increment = 0;
#endif

#ifdef PIN_USER_BTN_ANA
  unsigned long _analogue_pin_read_millis = millis();
#endif

  UIScreen* splash;
  UIScreen* home;
  UIScreen* msg_preview;
  UIScreen* curr;

  void userLedHandler();
  void updateUsbState(bool force_refresh);
  void _saveElapsedToPrefs(bool force = false);

  // Button action handlers
  char checkDisplayOn(char c);
  char handleLongPress(char c);
  char handleDoubleClick(char c);
  char handleTripleClick(char c);

  void setCurrScreen(UIScreen* c);

public:

  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    _next_refresh = 0;
    ui_started_at = 0;
    curr = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void gotoHomeScreen() { setCurrScreen(home); }
  void gotoMsgPreview() { if (getUnreadMsgCount() > 0) setCurrScreen(msg_preview); }
  void showAlert(const char* text, int duration_millis);
  int  getMsgCount() const { return _msgcount; }
  bool hasDisplay() const { return _display != NULL; }
  DisplayDriver* getDisplay() const { return _display; }
  bool isEinkDisplay() const { return _display != nullptr && _display->isEink(); }
  bool isUsbConnected() const { return _usb_connected; }
  uint32_t usbEventMs() const { return _usb_event_ms; }
  uint32_t usbElapsedSecs() const { return _usb_elapsed_base + (millis() - _usb_event_ms) / 1000; }
  bool isColorTFTDisplay() const { return _display != nullptr && _display->isColorTFT(); }
  bool isButtonPressed() const;

  struct ClockPMInfo {
    uint8_t  path_len;
    uint32_t timestamp;
    char     from_name[32];
    char     msg[10 * CIPHER_BLOCK_SIZE];
  };
  bool peekTopMsg(ClockPMInfo& out) const;
  void consumeTopMsg();
  int  getUnreadMsgCount() const;
  int  getLogCount() const;
  void gotoMsgHistory();

  void setDisplayRotation(uint8_t r);
  void updateMsgMaxSizes(int max_unread, int max_log);

  void setClockDimMode(int m);
  int  getClockDimMode() const { return _clock_dim_mode; }

  bool isBuzzerQuiet() { 
#ifdef PIN_BUZZER
    return buzzer.isQuiet();
#else
    return true;
#endif
  }

  void toggleBuzzer();
#ifdef UI_HAS_STATUS_LEDS
  void applyLedPrefs();   // push LED on/off/brightness from prefs to the status LEDs
#endif
  bool getGPSState();
  void toggleGPS();


  // from AbstractUITask
  // MyMesh::Listener. Messages reach the screen through onShowMessage(): MyMesh
  // hands over the text as it should be shown (compatibility transforms, decoded
  // or placeholder lines for binary channel data); the *Recv hooks only alert.
  void onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) override;
  void onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) override;
  void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onControlDataRecv(const mesh::Packet* pkt) override;
  void onQueueSizeChanged(int msgcount) override;
  void onShowMessage(uint8_t path_len, const char* from_name, const char* text, int msgcount, bool is_pm) override;
  void onStatusChanged() override { notify(); }
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  void shutdown(bool restart = false);
};
