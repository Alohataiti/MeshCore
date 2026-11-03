// Built only for envs that have this hardware enabled (the variant folder is compiled whole).
#ifdef UI_HAS_ROTARY_INPUT

#include "ScoutRotaryInput.h"

// Index = (previous AB << 2) | current AB; value = step direction, 0 = invalid/bounce.
static const int8_t QUAD_TABLE[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };

uint8_t ScoutRotaryInput::readPins() const {
  return (digitalRead(_pin_a) << 1) | digitalRead(_pin_b);
}

bool ScoutRotaryInput::begin() {
  _initialized = true;
  pinMode(_pin_a, INPUT_PULLUP);
  pinMode(_pin_b, INPUT_PULLUP);
  _prev = readPins();
  _steps = 0;
  attachInterruptArg(digitalPinToInterrupt(_pin_a), onEdge, this, CHANGE);
  attachInterruptArg(digitalPinToInterrupt(_pin_b), onEdge, this, CHANGE);
  _ready = true;
  return true;
}

void IRAM_ATTR ScoutRotaryInput::onEdge(void* arg) {
  static_cast<ScoutRotaryInput*>(arg)->decode();
}

void IRAM_ATTR ScoutRotaryInput::decode() {
  uint8_t cur = readPins();
  portENTER_CRITICAL_ISR(&_mux);
  _steps += QUAD_TABLE[(_prev << 2) | cur];
  _prev = cur;
  portEXIT_CRITICAL_ISR(&_mux);
}

RotaryInputEvent ScoutRotaryInput::poll() {
  if (!_initialized) {
    begin();
    return RotaryInputEvent::None;
  }

  RotaryInputEvent ev = RotaryInputEvent::None;
  portENTER_CRITICAL(&_mux);
  if (_steps >= ENCODER_STEPS_PER_DETENT) {
    _steps -= ENCODER_STEPS_PER_DETENT;
    ev = RotaryInputEvent::Next;
  } else if (_steps <= -ENCODER_STEPS_PER_DETENT) {
    _steps += ENCODER_STEPS_PER_DETENT;
    ev = RotaryInputEvent::Prev;
  }
  portEXIT_CRITICAL(&_mux);

#ifdef ENCODER_REVERSE
  if (ev == RotaryInputEvent::Next) return RotaryInputEvent::Prev;
  if (ev == RotaryInputEvent::Prev) return RotaryInputEvent::Next;
#endif
  return ev;
}

#endif // UI_HAS_ROTARY_INPUT
