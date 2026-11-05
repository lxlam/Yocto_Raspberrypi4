/*
 * app_can.h - OJT CAN test node for Arduino UNO (ATmega328P, 5 V, 16 MHz)
 *
 * Test node for OJT CAN protocol v1:
 * sends OJT CMD frames (0x100) to the Raspberry Pi, checks RESPONSE (0x102),
 * confirms the real playback change with STATUS (0x101), shows HEALTH (0x103).
 * Trigger: commands typed in the Serial Monitor (USB cable, 115200 8N1)
 *          or the optional push button (next demo command).
 *
 * Hardware mapping (Arduino UNO):
 *   D13 SCK, D12 MISO, D11 MOSI (hardware SPI) -> MCP2515 SCK, SO, SI
 *   D10 (output)                               -> MCP2515 CS
 *   D2  (input, pull-up)                       <- MCP2515 INT (low = frame received)
 *   5V / GND                                   -> MCP2515 VCC / GND
 *   D0 / D1 = USB serial (Serial Monitor) -> do NOT connect anything there
 *   Optional: push button D4 -> GND, LEDs (with 220 ohm) on D5 green / D6 orange / D7 red / D8 blue
 */
#ifndef APP_CAN_H
#define APP_CAN_H

#include <Arduino.h>

/* ---- must match the MCP2515 module + the Pi (80-can0.network, CAN_OSCILLATOR) ---- */
#ifndef APP_MCP2515_OSC_HZ
#define APP_MCP2515_OSC_HZ   8000000UL    /* crystal on the module: 8 MHz or 16 MHz */
#endif
#ifndef APP_CAN_BITRATE
#define APP_CAN_BITRATE      500000UL
#endif

#define APP_SERIAL_BAUD  115200UL

#define APP_CS_PIN      10
#define APP_INT_PIN     2
#define APP_BTN_PIN     4                /* optional button to GND (INPUT_PULLUP)  */
#define APP_LED_ONLINE  5                /* green : Pi STATUS fresh       (optional) */
#define APP_LED_PLAY    6                /* orange: player PLAYING        (optional) */
#define APP_LED_ERROR   7                /* red   : last command failed   (optional) */
#define APP_LED_TX      8                /* blue  : toggles on each CMD   (optional) */

void app_init(void);    /* call from setup() */
void app_loop(void);    /* call from loop()  */

#endif /* APP_CAN_H */
