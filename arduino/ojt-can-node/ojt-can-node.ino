/*
 * ojt-can-node.ino - OJT CAN test node for Arduino UNO + MCP2515 module
 *
 * Build/upload with arduino-cli, or avrdude + the .hex files in prebuilt (see README.md);
 * the Arduino IDE also works. The folder name must stay "ojt-can-node".
 * Board: Arduino Uno. No extra library is needed (own MCP2515 driver, SPI is built in).
 *
 * The real code is in the other tabs:
 *   app_can.h / app_can.cpp          pins, crystal, console commands, timeouts
 *   mcp2515.h / mcp2515.cpp          MCP2515 driver (SPI)
 *   ojt_can_protocol.h / .c          SHARED with the Pi can-service (keep identical)
 *
 * Crystal on the MCP2515 module is 16 MHz?  -> change APP_MCP2515_OSC_HZ in app_can.h
 */
#include "app_can.h"

void setup()
{
    app_init();
}

void loop()
{
    app_loop();
}
