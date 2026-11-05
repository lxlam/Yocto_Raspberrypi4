ENABLE_UART = "1"

# CAN: MCP2515 on SPI0 CE0 -> config.txt: dtparam=spi=on + dtoverlay=mcp2515-can0,oscillator=...,interrupt=25
ENABLE_SPI_BUS = "1"
ENABLE_CAN = "1"
# Crystal on the MCP2515 module (read the marking: 8.000 or 16.000). The Arduino node sets its own crystal (APP_MCP2515_OSC_HZ).
CAN_OSCILLATOR = "8000000"
# MCP2515 INT -> GPIO25 (physical pin 22)
CAN0_INTERRUPT_PIN = "25"
