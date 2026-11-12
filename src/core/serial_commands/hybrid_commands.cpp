// Fush OS hybrid local control: USB Serial always available, Wi-Fi WebUI optional.
#include "hybrid_commands.h"
#include "core/wifi/webInterface.h"
#include "core/wifi/wifi_common.h"
#include <globals.h>

namespace {

void printStatus() {
    serialDevice->println("Fush OS Hybrid Control");
    serialDevice->println("USB serial: available (115200 baud)");
    serialDevice->println(String("Wi-Fi WebUI: ") + (isWebUIActive && server ? "ON" : "OFF"));
    const wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        serialDevice->println("Wi-Fi AP address: " + WiFi.softAPIP().toString());
    } else if (WiFi.isConnected()) {
        serialDevice->println("Wi-Fi LAN address: " + WiFi.localIP().toString());
    } else {
        serialDevice->println("Wi-Fi: not connected");
    }
    serialDevice->println("Commands: fush mode usb | wifi | hybrid | status");
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
        // Close the management server first, then remove its AP if present.
        if (server) stopWebUi();
        const wifi_mode_t wifiMode = WiFi.getMode();
        if (wifiMode == WIFI_MODE_AP || wifiMode == WIFI_MODE_APSTA) {
            wifiDisconnect();
        }
        serialDevice->println("Mode: USB control. Wi-Fi WebUI stopped.");
        serialDevice->println("USB serial remains available; Wi-Fi may be used by other features.");
        return true;
    }

    if (mode == "wifi" || mode == "hybrid" || mode == "both") {
        if (!server) {
            // Start the familiar authenticated WebUI over the device Wi-Fi AP.
            // On headless Fush OS this call returns after the server is started.
            startWebUi(true);
        }
        if (server && isWebUIActive) {
            serialDevice->println("Mode: HYBRID. USB serial and Wi-Fi WebUI are both available.");
            printStatus();
            return true;
        }
        serialDevice->println("Failed to start Wi-Fi WebUI. USB control is still available.");
        return false;
    }

    serialDevice->println("Use: fush mode usb | wifi | hybrid | status");
    return false;
}

} // namespace

void createHybridCommands(SimpleCLI *cli) {
    Command fush = cli->addCompositeCmd("fush");
    Command mode = fush.addCommand("mode", hybridMode);
    mode.addPosArg("target", "status");
}
