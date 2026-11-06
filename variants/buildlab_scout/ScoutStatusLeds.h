#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <helpers/ui/StatusLeds.h>

// Six WS2812 LEDs used as status indicators (same layout as the Meshtastic build):
//   LED1 green   - packet received (flash) + heartbeat every 5 s
//   LED2 red     - transmitting
//   LED3, LED4   - unread messages: slow breathing (can be switched off separately)
//   LED5 blue    - GPS on, searching (blink)
//   LED6 yellow  - GPS fix (short blink)
class ScoutStatusLeds : public StatusLeds {
public:
  ScoutStatusLeds(int8_t pin, uint8_t count) : _strip(count, pin, NEO_GRB + NEO_KHZ800) { }

  void begin() override;
  void configure(bool enabled, bool msg_enabled, uint8_t level) override;
  uint8_t levelCount() const override { return 4; }
  void update(const StatusLedState& state) override;

private:
  void put(uint8_t idx, uint8_t r, uint8_t g, uint8_t b, uint16_t scale);

  Adafruit_NeoPixel _strip;
  bool _enabled = true;
  bool _msg_enabled = true;
  uint8_t _level = 1;
  bool _dirty = true;
  uint32_t _next_frame = 0;
  uint32_t _last_rx_count = 0;
  bool _rx_seen = false;
  uint32_t _rx_until = 0;
  uint32_t _tx_until = 0;
};
