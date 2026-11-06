#ifdef UI_HAS_STATUS_LEDS
#include "target.h"
#include "../../examples/companion_radio/UILang.h"
#include "../../examples/companion_radio/ui-new/UIExtensions.h"
#include <stdio.h>

static void apply() {
  NodePrefs* p = the_mesh.getNodePrefs();
  status_leds.configure(p->ui_led_enabled, p->ui_led_msg_enabled, p->ui_led_level);
  the_mesh.savePrefs();
}
static const char* ledLabel() { return "LED"; }
static const char* msgLabel() { return "LED Msg"; }
static const char* levelLabel() { return "LED Bright"; }
static void ledValue(char* out, size_t n) {
  snprintf(out, n, "%s", the_mesh.getNodePrefs()->ui_led_enabled ? _TR("On", "Вкл") : _TR("Off", "Выкл"));
}
static void msgValue(char* out, size_t n) {
  snprintf(out, n, "%s", the_mesh.getNodePrefs()->ui_led_msg_enabled ? _TR("On", "Вкл") : _TR("Off", "Выкл"));
}
static void levelValue(char* out, size_t n) {
  snprintf(out, n, "%u/4", unsigned(the_mesh.getNodePrefs()->ui_led_level) + 1);
}
static void ledPress(bool) { the_mesh.getNodePrefs()->ui_led_enabled ^= 1; apply(); }
static void msgPress(bool) { the_mesh.getNodePrefs()->ui_led_msg_enabled ^= 1; apply(); }
static void levelPress(bool fwd) {
  auto* p = the_mesh.getNodePrefs();
  p->ui_led_level = (p->ui_led_level + (fwd ? 1 : 3)) % 4;
  apply();
}
int uiExtraSettings(const UIExtraSetting** items) {
  static const UIExtraSetting settings[] = {
    {ledLabel, nullptr, ledValue, ledPress, UI_FOLDER_DISPLAY},
    {msgLabel, nullptr, msgValue, msgPress, UI_FOLDER_DISPLAY},
    {levelLabel, nullptr, levelValue, levelPress, UI_FOLDER_DISPLAY},
  };
  if (items) *items = settings;
  return sizeof(settings) / sizeof(settings[0]);
}
#endif
