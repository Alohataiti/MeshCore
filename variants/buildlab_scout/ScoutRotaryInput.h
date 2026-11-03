#pragma once

#include <Arduino.h>
#include <helpers/ui/RotaryInput.h>

// Quadrature rotary encoder wired straight to two GPIOs (common pin to GND).
// Edges are decoded in an interrupt so no step is lost while the main loop is busy;
// poll() hands out one Next/Prev event per detent.
#ifndef ENCODER_STEPS_PER_DETENT
  #define ENCODER_STEPS_PER_DETENT 4
#endif

class ScoutRotaryInput : public RotaryInput {
public:
  ScoutRotaryInput(int8_t pin_a, int8_t pin_b) : _pin_a(pin_a), _pin_b(pin_b) { }

  bool begin() override;
  RotaryInputEvent poll() override;
  bool isReady() const override { return _ready; }

private:
  static void IRAM_ATTR onEdge(void* arg);
  void IRAM_ATTR decode();
  uint8_t readPins() const;

  int8_t _pin_a, _pin_b;
  bool _initialized = false;
  bool _ready = false;
  volatile uint8_t _prev = 0;
  volatile int16_t _steps = 0;
  portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};
