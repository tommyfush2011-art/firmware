// Fush OS: USB serial control and USB Navigator alongside the Wi-Fi WebUI.
// Screen messages use framed ASCII hex to avoid binary/terminal mixing on CDC.
#include "hybrid_commands.h"
#include "core/utils.h"
#include "core/wifi/webInterface.h"
#include "core/wifi/wifi_common.h"
#include <globals.h>
#include <esp32-hal-psram.h>
#include <cstdlib>
#include <cstring>
#include <esp_system.h>

namespace {
bool usbNavigator = false;
uint32_t screenSequence = 0;
bool liveUSB = false;
uint32_t liveSequence = 0;
uint32_t lastResyncAt = 0;

// Funny SSID presets use ONE ordinary encrypted ESP32 SoftAP, not crafted
// beacon frames or multiple virtual networks. Never broadcast more than one.
constexpr int FUNNY_PRESET_COUNT = 5;
const char *const FUNNY_SSIDS[FUNNY_PRESET_COUNT] = {
    "Fush Shark",
    "Megalodon Lives Here",
    "Definitely Not WiFi",
    "Shark Territory",
    "Fush OS Online"
};
bool funnyApOwned = false;
int funnyApPreset = -1; // zero-based; -1 = off
String funnyApPassword;

bool funnyApRunning() {
    const wifi_mode_t mode = WiFi.getMode();
    return funnyApOwned && funnyApPreset >= 0 && funnyApPreset < FUNNY_PRESET_COUNT &&
           (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) &&
           WiFi.softAPSSID() == FUNNY_SSIDS[funnyApPreset];
}

// The password is per-boot and printed only through the local USB console.
// The device's Wi-Fi management HTTP server is NOT started by this feature.
void ensureFunnyApPassword() {
    if (funnyApPassword.length() >= 8) return;
    char generated[21];
    snprintf(generated, sizeof(generated), "Fush%08lX%08lX",
             (unsigned long)esp_random(), (unsigned long)esp_random());
    funnyApPassword = generated;
}

void stopFunnyAp() {
    if (funnyApOwned) {
        wifiDisconnect();
        funnyApOwned = false;
        funnyApPreset = -1;
    }
}

void printFunnyApStatus() {
    const bool active = funnyApRunning();
    const int selected = active ? funnyApPreset + 1 : 0;
    serialDevice->printf("@FUSHAP:STATE:%d:%d\n", active ? 1 : 0, selected);
    serialDevice->printf("Funny SSID: AP=%d (single normal access point)\n", active ? 1 : 0);
    if (active) {
        serialDevice->println("Preset: " + String(selected) + "/5 - " + String(FUNNY_SSIDS[funnyApPreset]));
        serialDevice->println("AP address: " + WiFi.softAPIP().toString());
        serialDevice->println("AP password: " + funnyApPassword);
        serialDevice->println("Connected clients: " + String(WiFi.softAPgetStationNum()));
        serialDevice->println("Wi-Fi WebUI: OFF (USB control remains available)");
    } else {
        serialDevice->println("Funny SSID AP is OFF. Choose preset 1-5 to start one normal AP.");
    }
}

uint32_t funnyApCommand(cmd *raw) {
    Command command(raw);
    String action = command.getArgument("action").getValue();
    action.trim(); action.toLowerCase();
    if (action == "" || action == "status") {
        printFunnyApStatus();
        return true;
    }
    if (action == "list") {
        serialDevice->println("Funny SSID presets (AP=1, one at a time):");
        for (int i = 0; i < FUNNY_PRESET_COUNT; ++i)
            serialDevice->println(String(i + 1) + ". " + FUNNY_SSIDS[i]);
        return true;
    }
    if (action == "off" || action == "stop") {
        stopFunnyAp();
        serialDevice->println("Funny SSID access point stopped");
        printFunnyApStatus();
        return true;
    }
    if (action == "set" || action == "start") {
        const String text = command.getArgument("preset").getValue();
        if (text.length() != 1 || text[0] < '1' || text[0] > '5') {
            serialDevice->println("Use: fush ap set 1|2|3|4|5");
            return false;
        }
        const int wanted = text[0] - '1';
        if (funnyApRunning() && funnyApPreset == wanted) {
            printFunnyApStatus();
            return true;
        }
        // Wi-Fi WebUI cannot safely share this AP under another SSID.
        // USB Navigator remains available (no USB serial teardown).
        if (server) stopWebUi();
        wifiDisconnect();
        funnyApOwned = false;
        funnyApPreset = -1;
        ensureFunnyApPassword();
        IPAddress gateway(172, 0, 0, 1);
        if (!WiFi.mode(WIFI_AP) ||
            !WiFi.softAPConfig(gateway, gateway, IPAddress(255, 255, 255, 0)) ||
            !WiFi.softAP(FUNNY_SSIDS[wanted], funnyApPassword.c_str(), 6, 0, 1, false)) {
            wifiDisconnect();
            serialDevice->println("ERROR: unable to start the normal Wi-Fi AP");
            printFunnyApStatus();
            return false;
        }
        funnyApOwned = true;
        funnyApPreset = wanted;
        wifiConnected = true;
        wifiIP = WiFi.softAPIP().toString();
        serialDevice->println("Normal Wi-Fi AP started; no beacon spam or multiple networks");
        printFunnyApStatus();
        return true;
    }
    serialDevice->println("Use: fush ap list|status|set 1..5|off");
    return false;
}

void printStatus() {
    serialDevice->println("Fush OS Hybrid Control");
    serialDevice->println("USB serial: available (115200 baud)");
    serialDevice->println(String("Wi-Fi WebUI: ") + (isWebUIActive && server ? "ON" : "OFF"));
    serialDevice->println(String("USB Navigator: ") + (usbNavigator ? "ON" : "OFF"));
    serialDevice->println(String("USB Live Drawing: ") + (liveUSB ? "ON" : "OFF"));
    serialDevice->println(String("Funny SSID AP=") + (funnyApRunning() ? "1" : "0"));
    const wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        serialDevice->println("Wi-Fi AP address: " + WiFi.softAPIP().toString());
    } else if (WiFi.isConnected()) {
        serialDevice->println("Wi-Fi LAN address: " + WiFi.localIP().toString());
    } else {
        serialDevice->println("Wi-Fi: not connected");
    }
    serialDevice->println("Commands: fush mode usb|hybrid|status; fush ap list|status|set 1..5|off; fush navigator start|stop; fush screen; fush nav up|down|next|prev|sel|esc|menu");
}

uint32_t hybridMode(cmd *raw) {
    Command command(raw);
    String mode = command.getArgument("target").getValue();
    mode.trim();
    mode.toLowerCase();
    if (mode == "" || mode == "status") {
        printStatus();
        return true;
    }
    if (mode == "usb" || mode == "otg") {
        if (server) stopWebUi();
        // Keep the user's single normal funny-name AP if it was explicitly started.
        // Otherwise stop the management WebUI's AP as before.
        if (!funnyApRunning()) {
            const wifi_mode_t currentMode = WiFi.getMode();
            if (currentMode == WIFI_MODE_AP || currentMode == WIFI_MODE_APSTA) wifiDisconnect();
        }
        serialDevice->println("Mode: USB control (Wi-Fi management WebUI stopped)");
        return true;
    }
    if (mode == "wifi" || mode == "hybrid" || mode == "both") {
        // Switch back from the funny-name AP to Fush's ordinary Wi-Fi WebUI AP.
        stopFunnyAp();
        if (!server) startWebUi(true);
        if (server && isWebUIActive) {
            serialDevice->println("Mode: HYBRID (USB control and Wi-Fi WebUI)");
            return true;
        }
        serialDevice->println("Wi-Fi WebUI failed to start; USB remains available");
        return false;
    }
    serialDevice->println("Use: fush mode usb|hybrid|status");
    return false;
}

uint32_t navigatorMode(cmd *raw) {
    Command command(raw);
    String action = command.getArgument("action").getValue();
    action.trim();
    action.toLowerCase();
    if (action == "start") {
        usbNavigator = true;
        if (!tft.getLogging()) {
            tft.setLogging(true);
            if (!tft.getLogging()) {
                usbNavigator = false;
                serialDevice->println("ERROR: screen logger allocation failed");
                return false;
            }
            // The logger began empty. Redraw a menu for the first USB snapshot.
            backToMenu();
        }
        serialDevice->println("USB Navigator enabled. Use: fush screen");
        return true;
    }
    if (action == "stop") {
        liveUSB = false;
        tft.endUsbLive();
        usbNavigator = false;
        // The Wi-Fi Navigator uses the same logger: do not disable it.
        if (!isWebUIActive) tft.setLogging(false);
        serialDevice->println("USB Navigator disabled");
        return true;
    }
    serialDevice->println(String("USB Navigator: ") + (usbNavigator ? "ON" : "OFF"));
    serialDevice->println(String("USB Live Drawing: ") + (liveUSB ? "ON" : "OFF"));
    return true;
}

uint32_t navigationKey(cmd *raw) {
    Command command(raw);
    String key = command.getArgument("key").getValue();
    key.trim();
    key.toLowerCase();
    String holdText = command.getArgument("duration").getValue();
    const long holdInput = holdText.toInt();
    const uint32_t holdMs = (holdInput > 0 && holdInput <= 1500) ? (uint32_t)holdInput : 90;

    volatile bool *target = nullptr;
    if (key == "up") target = &UpPress;
    else if (key == "down") target = &DownPress;
    else if (key == "next" || key == "right") target = &NextPress;
    else if (key == "prev" || key == "left") target = &PrevPress;
    else if (key == "sel" || key == "enter" || key == "menu") target = &SelPress;
    else if (key == "esc" || key == "back") target = &EscPress;
    else if (key == "nextpage") target = &NextPagePress;
    else if (key == "prevpage") target = &PrevPagePress;
    if (!target) {
        serialDevice->println("Use: fush nav up|down|next|prev|sel|esc|menu|nextpage|prevpage [duration]");
        return false;
    }

    // One short press == one menu movement. Repeating the flag every 50 ms
    // made the menu consume the same USB key more than once, skipping entries.
    // Match the original Wi-Fi /cm behavior: a single press plus time for the
    // device's menu task to consume it. Only explicit Menu/long-press repeats.
    if (key == "menu") {
        const uint32_t longHold = (holdMs < 450) ? 500 : holdMs;
        const uint32_t started = millis();
        do {
            if (!*target) {
                AnyKeyPress = true;
                SerialCmdPress = true;
                *target = true;
            }
            vTaskDelay(pdMS_TO_TICKS(40));
        } while (millis() - started < longHold);
    } else {
        AnyKeyPress = true;
        SerialCmdPress = true;
        *target = true;
        // Do not reassert the flag if the UI task has already consumed it.
        vTaskDelay(pdMS_TO_TICKS(190));
    }
    return true;
}

uint32_t sendScreen(cmd *) {
    if (!usbNavigator && !tft.getLogging()) {
        serialDevice->println("ERROR: start USB Navigator first: fush navigator start");
        return false;
    }
    const size_t capacity = (size_t)MAX_LOG_ENTRIES * MAX_LOG_SIZE;
    uint8_t *bytes = nullptr;
    if (psramFound()) bytes = (uint8_t *)ps_malloc(capacity);
    if (!bytes) bytes = (uint8_t *)malloc(capacity);
    if (!bytes) {
        serialDevice->println("ERROR: screen snapshot out of memory");
        return false;
    }
    size_t size = 0;
    tft.getBinLog(bytes, size);
    if (size > capacity || size < 8) {
        free(bytes);
        serialDevice->println("ERROR: invalid screen snapshot length");
        return false;
    }
    const uint32_t sequence = ++screenSequence;
    uint32_t hash = 2166136261UL; // FNV-1a, detect missing or corrupted lines
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }

    serialDevice->printf("@FUSHNAV:BEGIN:%lu:%lu\n", (unsigned long)sequence, (unsigned long)size);
    const char alphabet[] = "0123456789ABCDEF";
    constexpr size_t chunk = 64;
    char hex[chunk * 2 + 1];
    for (size_t offset = 0; offset < size; offset += chunk) {
        const size_t count = (size - offset < chunk) ? size - offset : chunk;
        for (size_t i = 0; i < count; ++i) {
            const uint8_t b = bytes[offset + i];
            hex[2 * i] = alphabet[b >> 4];
            hex[2 * i + 1] = alphabet[b & 15];
        }
        hex[count * 2] = '\0';
        serialDevice->printf("@FUSHNAV:DATA:%lu:%s\n", (unsigned long)sequence, hex);
    }
    serialDevice->printf("@FUSHNAV:END:%lu:%08lX\n", (unsigned long)sequence, (unsigned long)hash);
    free(bytes);
    return true;
}
uint32_t liveCommand(cmd *raw) {
    Command command(raw);
    String action = command.getArgument("action").getValue();
    action.trim(); action.toLowerCase();
    if (action == "stop") {
        liveUSB = false;
        tft.endUsbLive();
        serialDevice->println("@FUSHLIVE:OFF");
        return true;
    }
    if (action == "start") {
        if (!usbNavigator || !tft.getLogging()) {
            serialDevice->println("ERROR: start USB Navigator first");
            return false;
        }
        if (!tft.beginUsbLive()) {
            serialDevice->println("ERROR: cannot allocate USB live drawing queue");
            return false;
        }
        liveUSB = true;
        liveSequence = 0;
        serialDevice->println("@FUSHLIVE:ON");
        // One full reference screen, then only incremental draw commands.
        sendScreen(nullptr);
        return true;
    }
    serialDevice->println(String("USB Live Drawing: ") + (liveUSB ? "ON" : "OFF"));
    return true;
}
} // namespace

// Called by the serial-command task; all USB protocol writes stay on one task.
// Never block the drawing thread: if a producer outruns USB, resync to the
// latest snapshot rather than sending a long queue of stale game frames.
void fushUsbLivePump() {
    if (!liveUSB) return;
    if (tft.usbLiveLost()) {
        if (millis() - lastResyncAt >= 700) {
            lastResyncAt = millis();
            tft.resetUsbLiveQueue();
            serialDevice->println("@FUSHLIVE:RESYNC");
            sendScreen(nullptr);
            return;
        }
    }
    constexpr char hexDigits[] = "0123456789ABCDEF";
    uint8_t packet[MAX_LOG_SIZE];
    char encoded[MAX_LOG_SIZE * 2 + 1];
    uint8_t len = 0;
    for (int n = 0; n < 12 && tft.popUsbLive(packet, len); ++n) {
        // Image entries need external file bytes; ordinary drawing/text works
        // without Wi-Fi. The initial snapshot still shows image placeholders.
        if (packet[2] == DRAWIMAGE) continue;
        for (uint8_t i = 0; i < len; ++i) {
            encoded[2 * i] = hexDigits[packet[i] >> 4];
            encoded[2 * i + 1] = hexDigits[packet[i] & 15];
        }
        encoded[2 * len] = 0;
        serialDevice->printf("@FUSHLIVE:D:%lu:%s\n", (unsigned long)++liveSequence, encoded);
    }
}

bool fushUsbNavigatorActive() { return usbNavigator; }

void createHybridCommands(SimpleCLI *cli) {
    Command fush = cli->addCompositeCmd("fush");
    Command mode = fush.addCommand("mode", hybridMode);
    mode.addPosArg("target", "status");

    Command ap = fush.addCommand("ap", funnyApCommand);
    ap.addPosArg("action", "status");
    ap.addPosArg("preset", "1");

    Command navigator = fush.addCommand("navigator", navigatorMode);
    navigator.addPosArg("action", "status");
    fush.addCommand("screen", sendScreen);
    Command live = fush.addCommand("live", liveCommand);
    live.addPosArg("action", "status");
    Command nav = fush.addCommand("nav", navigationKey);
    nav.addPosArg("key");
    nav.addPosArg("duration", "90");
}
