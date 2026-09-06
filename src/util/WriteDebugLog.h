#pragma once

// Minimal debug log for devices whose USB-Serial-JTAG port is locked down
// (no user-visible serial console). Writes a line to /debug.log on the SD
// card through HalStorage (mutex-guarded). Used by main.cpp to record the
// deep-sleep entry and wakeup cause so sleep/timer diagnostics can be
// retrieved after the fact.

void writeDebugLog(const char* line);