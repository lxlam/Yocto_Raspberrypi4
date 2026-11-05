/*
 * mcp2515.h - small MCP2515 driver for Arduino (SPI library, mode 0, software CS)
 *
 * Register-level driver (no Arduino CAN library needed): SPI, GPIO and time
 * come from the Arduino core.
 *
 * Standard 11-bit frames only (OJT protocol). TX uses TXB0, RX reads RXB0/RXB1.
 * Bit timing tables for 8 MHz and 16 MHz crystals, sample point 75 %.
 */
#ifndef MCP2515_H
#define MCP2515_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MCP2515_OK = 0,
    MCP2515_ERR_SPI,         /* register read-back wrong: SPI wiring        */
    MCP2515_ERR_NO_CHIP,     /* not in CONFIG mode after reset: wiring/power */
    MCP2515_ERR_MODE,        /* mode change refused                         */
    MCP2515_ERR_BITRATE,     /* oscillator/bitrate pair not in the table    */
    MCP2515_ERR_TX_BUSY,     /* TXB0 still pending (no ACK on the bus?)     */
    MCP2515_NO_MSG
} Mcp2515Status;

typedef enum {
    MCP2515_MODE_NORMAL     = 0x00,
    MCP2515_MODE_LOOPBACK   = 0x40,
    MCP2515_MODE_LISTENONLY = 0x60,
    MCP2515_MODE_CONFIG     = 0x80
} Mcp2515Mode;

typedef struct {
    uint8_t  cs_pin;           /* Arduino pin number of CS */
    uint32_t osc_hz;           /* 8000000 or 16000000 */
    uint32_t bitrate;          /* 125000, 250000, 500000 */
} Mcp2515;

typedef struct {
    uint16_t id;                   /* 11-bit */
    uint8_t  dlc;
    uint8_t  data[8];
} Mcp2515Frame;

typedef struct {
    uint8_t canstat, canctrl, eflg, tec, rec, canintf, txb0ctrl;
} Mcp2515Regs;

/* SPI.begin, reset, bit timing, accept all std frames, RX interrupts on INT pin, NORMAL mode */
Mcp2515Status mcp2515_init(Mcp2515 *m);
/* same as mcp2515_init() but stays in CONFIG mode (used by the self test) */
Mcp2515Status mcp2515_reset_config(Mcp2515 *m);
/* NOTE: entering NORMAL is immediate; LEAVING normal (e.g. to loopback) waits
   until the chip sees the CAN bus idle */

Mcp2515Status mcp2515_set_mode(Mcp2515 *m, Mcp2515Mode mode);

Mcp2515Status mcp2515_send(Mcp2515 *m, const Mcp2515Frame *f);   /* non-blocking (TXB0);
                                     MCP2515_ERR_SPI = buffer read-back wrong (SI wire) */
bool          mcp2515_tx_pending(Mcp2515 *m);                      /* TXREQ still set       */
bool          mcp2515_tx_done(Mcp2515 *m);                         /* TX0IF: sent + ACKed   */
void          mcp2515_abort_tx(Mcp2515 *m);                        /* clear TXREQ of TXB0 */

/* MCP2515_OK + frame, or MCP2515_NO_MSG. Remote/extended frames are dropped. */
Mcp2515Status mcp2515_receive(Mcp2515 *m, Mcp2515Frame *f);

void          mcp2515_read_regs(Mcp2515 *m, Mcp2515Regs *r);
uint8_t       mcp2515_read_register(Mcp2515 *m, uint8_t reg);      /* raw register read   */
void          mcp2515_write_register(Mcp2515 *m, uint8_t reg, uint8_t val);  /* raw write    */

/* TXB0CTRL bits (see mcp2515_read_regs) */
#define MCP2515_TXB_ABTF   0x40   /* aborted                                  */
#define MCP2515_TXB_MLOA   0x20   /* lost arbitration                         */
#define MCP2515_TXB_TXERR  0x10   /* bus error while sending (e.g. no ACK)    */
#define MCP2515_TXB_TXREQ  0x08   /* still waiting to be sent                 */
void          mcp2515_clear_errors(Mcp2515 *m);                    /* clear EFLG overflow + ERRIF */

#endif /* MCP2515_H */
