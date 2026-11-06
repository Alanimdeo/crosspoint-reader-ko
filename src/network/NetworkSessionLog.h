#pragma once

// What the device itself saw of a file transfer session, kept in /wifi-debug.log for units whose USB console is locked.
// Temporary: it is here to show why a session is unreachable, and goes once that is known.
namespace netlog {

void open();  // starts a session, before Wi-Fi comes up
void line(const char* stage);
void tap(bool accessPoint);  // once the interface has an address: counts frames between the driver and lwIP
void tick();                 // call while serving; writes one line per period
void close();

}  // namespace netlog
