// Built only for envs that have this hardware enabled (the variant folder is compiled whole).
#ifdef UI_HAS_STATUS_LEDS

#include "ScoutStatusLeds.h"

#define FRAME_MILLIS      20
#define RX_FLASH_MILLIS   120
#define TX_HOLD_MILLIS    80
#define HEARTBEAT_PERIOD  5000
#define HEARTBEAT_MILLIS  60
#define BREATH_PERIOD     3000
#define GPS_SEARCH_PERIOD 1000
#define GPS_FIX_PERIOD    3000
#define GPS_BLINK_MILLIS  80

// Peak channel value per brightness step (out of 255).
static const uint8_t LEVELS[4] = { 6, 20, 60, 160 };

void ScoutStatusLeds::begin() {
  _strip.begin();
  _strip.clear();
  _strip.show();
}

void ScoutStatusLeds::configure(bool enabled, bool msg_enabled, uint8_t level) {
  _enabled = enabled;
  _msg_enabled = msg_enabled;
  _level = level < levelCount() ? level : levelCount() - 1;
  _dirty = true;
}

// scale: 0..256, fraction of the current brightness level
void ScoutStatusLeds::put(uint8_t idx, uint8_t r, uint8_t g, uint8_t b, uint16_t scale) {
  uint32_t peak = (uint32_t)LEVELS[_level] * scale / 256;
  uint32_t c = _strip.Color(r * peak / 255, g * peak / 255, b * peak / 255);
  if (_strip.getPixelColor(idx) != c) {
    _strip.setPixelColor(idx, c);
    _dirty = true;
  }
}

void ScoutStatusLeds::update(const StatusLedState& s) {
  uint32_t now = millis();
  if ((int32_t)(now - _next_frame) < 0) return;
  _next_frame = now + FRAME_MILLIS;

  // Track events even while disabled, so re-enabling does not replay a stale flash.
  if (_rx_seen && s.rx_count != _last_rx_count) _rx_until = now + RX_FLASH_MILLIS;
  _last_rx_count = s.rx_count;
  _rx_seen = true;
  if (s.tx) _tx_until = now + TX_HOLD_MILLIS;

  if (!_enabled) {
    for (uint8_t i = 0; i < _strip.numPixels(); i++) put(i, 0, 0, 0, 0);
  } else {
    // LED1: received packet, otherwise a faint heartbeat
    if ((int32_t)(_rx_until - now) > 0)              put(0, 0, 255, 0, 256);
    else if (now % HEARTBEAT_PERIOD < HEARTBEAT_MILLIS) put(0, 0, 255, 0, 64);
    else                                              put(0, 0, 0, 0, 0);

    // LED2: transmit
    if ((int32_t)(_tx_until - now) > 0) put(1, 255, 0, 0, 256);
    else                                put(1, 0, 0, 0, 0);

    // LED3/LED4: unread messages, smooth fade in/out
    if (_msg_enabled && s.unread) {
      float ph = (float)(now % BREATH_PERIOD) / BREATH_PERIOD;  // 0..1
      float w = sinf(ph * PI);                                    // 0..1..0
      uint16_t scale = 8 + (uint16_t)(w * w * 248);               // never fully dark mid-cycle
      put(2, 160, 0, 255, scale);
      put(3, 160, 0, 255, scale);
    } else {
      put(2, 0, 0, 0, 0);
      put(3, 0, 0, 0, 0);
    }

    // LED5/LED6: GPS searching / fix
    bool searching = s.gps_on && !s.gps_fix;
    put(4, 0, 0, 255, (searching && now % GPS_SEARCH_PERIOD < GPS_BLINK_MILLIS) ? 256 : 0);
    put(5, 255, 160, 0, (s.gps_on && s.gps_fix && now % GPS_FIX_PERIOD < GPS_BLINK_MILLIS) ? 256 : 0);
  }

  if (_dirty) {
    _strip.show();
    _dirty = false;
  }
}

#endif // UI_HAS_STATUS_LEDS
