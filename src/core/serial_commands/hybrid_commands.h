#ifndef FUSH_HYBRID_COMMANDS_H
#define FUSH_HYBRID_COMMANDS_H

#include <SimpleCLI.h>

void createHybridCommands(SimpleCLI *cli);
// Keeps screen logging running when Wi-Fi WebUI closes but USB Navigator is active.
bool fushUsbNavigatorActive();

// Pump USB drawing packets from the serial command task (v5).
void fushUsbLivePump();

#endif
