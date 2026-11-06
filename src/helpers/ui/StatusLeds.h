#pragma once

#include <Arduino.h>

// Snapshot of what the status LEDs should reflect, filled by the UI on every loop.
struct StatusLedState {
  bool     tx;        // radio is transmitting right now
  uint32_t rx_count;  // running count of received packets (an increment = a new packet)
  bool     unread;    // there are unread messages
  bool     gps_on;    // GPS is enabled
  bool     gps_fix;   // GPS has a valid fix
};

class StatusLeds {
public:
  virtual ~StatusLeds() = default;

  virtual void begin() = 0;
  // enabled: all indication on/off; msg_enabled: unread-message breathing on/off;
  // level: brightness step, 0 = dimmest .. levelCount()-1 = brightest
  virtual void configure(bool enabled, bool msg_enabled, uint8_t level) = 0;
  virtual uint8_t levelCount() const = 0;
  virtual void update(const StatusLedState& state) = 0;
};
