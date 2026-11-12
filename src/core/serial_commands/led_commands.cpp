
#include "led_commands.h"
#include "core/led_control.h"
#include <globals.h>
#include <cstdlib>

#ifdef HAS_RGB_LED
namespace {

bool setSolid(uint8_t red, uint8_t green, uint8_t blue) {
    const uint32_t color =
        (uint32_t(red) << 16) |
        (uint32_t(green) << 8) | blue;

    bruceConfig.setLedEffect(LED_EFFECT_SOLID);
    bruceConfig.setLedColor(color);
    ledSetup();

    serialDevice->printf(
        "LED: #%02X%02X%02X\n", red, green, blue
    );
    return true;
}

bool parseChannel(const String &s, uint8_t &out) {
    if (!s.length()) return false;

    char *end = nullptr;
    const long val = strtol(s.c_str(), &end, 10);

    if (!end || *end != '\0' || val < 0 || val > 255)
        return false;

    out = static_cast<uint8_t>(val);
    return true;
}

uint32_t ledRgb(cmd *c) {
    Command cmd(c);
    uint8_t r, g, b;

    if (!parseChannel(cmd.getArgument("r").getValue(), r) ||
        !parseChannel(cmd.getArgument("g").getValue(), g) ||
        !parseChannel(cmd.getArgument("b").getValue(), b)) {
        serialDevice->println("Use: led rgb R G B (0-255)");
        return false;
    }
    return setSolid(r, g, b);
}

uint32_t ledBrightness(cmd *c) {
    Command cmd(c);
    String value = cmd.getArgument("percent").getValue();

    if (!value.length()) return false;

    char *end = nullptr;
    const long percent = strtol(value.c_str(), &end, 10);

#ifdef FUSH_RGB_LED_48
    const int limit = 16;
#else
    const int limit = 100;
#endif

    if (!end || *end != '\0' ||
        percent < 0 || percent > limit) {
        serialDevice->printf(
            "Brightness must be 0-%d percent\n", limit
        );
        return false;
    }

    bruceConfig.setLedBright(percent);
    setLedBrightness(percent);

    serialDevice->printf(
        "LED brightness: %ld%%\n", percent
    );
    return true;
}

uint32_t ledRainbow(cmd *) {
    bruceConfig.setLedEffect(LED_EFFECT_COLOR_CYCLE);
    ledSetup();
    serialDevice->println("LED: rainbow");
    return true;
}

uint32_t ledRed(cmd *) { return setSolid(255, 0, 0); }
uint32_t ledOrange(cmd *) { return setSolid(255, 80, 0); }
uint32_t ledGreen(cmd *) { return setSolid(0, 255, 0); }
uint32_t ledBlue(cmd *) { return setSolid(0, 0, 255); }
uint32_t ledYellow(cmd *) { return setSolid(255, 255, 0); }
uint32_t ledCyan(cmd *) { return setSolid(0, 255, 255); }
uint32_t ledMagenta(cmd *) { return setSolid(255, 0, 255); }
uint32_t ledWhite(cmd *) { return setSolid(255, 255, 255); }
uint32_t ledOff(cmd *) { return setSolid(0, 0, 0); }

} // namespace
#endif

void createLedCommands(SimpleCLI *cli) {
#ifdef HAS_RGB_LED
    Command led = cli->addCompositeCmd("led");

    led.addCommand("red", ledRed);
    led.addCommand("orange", ledOrange);
    led.addCommand("green", ledGreen);
    led.addCommand("blue", ledBlue);
    led.addCommand("yellow", ledYellow);
    led.addCommand("cyan", ledCyan);
    led.addCommand("magenta", ledMagenta);
    led.addCommand("white", ledWhite);
    led.addCommand("off", ledOff);
    led.addCommand("rainbow", ledRainbow);

    Command rgb = led.addCommand("rgb", ledRgb);
    rgb.addPosArg("r");
    rgb.addPosArg("g");
    rgb.addPosArg("b");

    Command bright =
        led.addCommand("brightness", ledBrightness);
    bright.addPosArg("percent");
#else
    (void)cli;
#endif
}
