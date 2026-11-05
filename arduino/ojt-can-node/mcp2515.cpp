/*
 * mcp2515.cpp - MCP2515 driver (Arduino SPI). Register map: Microchip DS20001801.
 *
 * Written in C style (plain functions + struct).
 * It is a .cpp file only because the Arduino SPI library is C++.
 */
#include "mcp2515.h"

#include <Arduino.h>
#include <SPI.h>
#include <string.h>

/* SPI instructions */
#define INS_RESET        0xC0
#define INS_READ         0x03
#define INS_WRITE        0x02
#define INS_BIT_MODIFY   0x05
#define INS_READ_STATUS  0xA0
#define INS_READ_RXB0    0x90   /* starts at RXB0SIDH, clears RX0IF at CS high */
#define INS_READ_RXB1    0x94
#define INS_LOAD_TXB0    0x40   /* starts at TXB0SIDH */
#define INS_RTS_TXB0     0x81

/* registers */
#define REG_CANSTAT   0x0E
#define REG_CANCTRL   0x0F
#define REG_TEC       0x1C
#define REG_REC       0x1D
#define REG_CNF3      0x28
#define REG_CNF2      0x29
#define REG_CNF1      0x2A
#define REG_CANINTE   0x2B
#define REG_CANINTF   0x2C
#define REG_EFLG      0x2D
#define REG_TXB0CTRL  0x30
#define REG_RXB0CTRL  0x60
#define REG_RXB1CTRL  0x70

#define CANINTF_RX0IF 0x01
#define CANINTF_RX1IF 0x02
#define CANINTF_TX0IF 0x04
#define REG_TXB0SIDH  0x31
#define REG_TXB0DLC   0x35
#define CANINTF_ERRIF 0x20
#define TXB_TXREQ     0x08
#define SIDL_SRR      0x10      /* standard remote frame */
#define SIDL_IDE      0x08      /* extended frame        */

/* MCP2515 max SPI clock is 10 MHz; 1 MHz is very tolerant of long jumper wires
   and still far faster than needed for 500 kbit/s CAN */
#define SPI_SETTINGS  SPISettings(1000000UL, MSBFIRST, SPI_MODE0)

/* CNF1, CNF2, CNF3 for SJW = 1 TQ, sample point 75 % (same timing as the Pi can0: tq 250 ns at 8 MHz) */
typedef struct { uint32_t osc, rate; uint8_t cnf1, cnf2, cnf3; } BitTiming;
static const BitTiming k_timing[] = {
    /* 8 MHz: TQ = 2*(BRP+1)/8 MHz, 8 TQ = sync 1 + prop 2 + ps1 3 + ps2 2 */
    {  8000000UL, 500000UL, 0x00, 0x91, 0x01 },
    {  8000000UL, 250000UL, 0x01, 0x91, 0x01 },
    {  8000000UL, 125000UL, 0x03, 0x91, 0x01 },
    /* 16 MHz: 16 TQ = sync 1 + prop 3 + ps1 8 + ps2 4 */
    { 16000000UL, 500000UL, 0x00, 0xBA, 0x03 },
    { 16000000UL, 250000UL, 0x01, 0xBA, 0x03 },
    { 16000000UL, 125000UL, 0x03, 0xBA, 0x03 },
};

/* one SPI "transaction" = CS low ... CS high */
static void cs_low(Mcp2515 *m)
{
    SPI.beginTransaction(SPI_SETTINGS);
    digitalWrite(m->cs_pin, LOW);
}

static void cs_high(Mcp2515 *m)
{
    digitalWrite(m->cs_pin, HIGH);
    SPI.endTransaction();
}

static uint8_t read_reg(Mcp2515 *m, uint8_t reg)
{
    cs_low(m);
    SPI.transfer(INS_READ);
    SPI.transfer(reg);
    uint8_t v = SPI.transfer(0x00);
    cs_high(m);
    return v;
}

static void write_reg(Mcp2515 *m, uint8_t reg, uint8_t val)
{
    cs_low(m);
    SPI.transfer(INS_WRITE);
    SPI.transfer(reg);
    SPI.transfer(val);
    cs_high(m);
}

static void bit_modify(Mcp2515 *m, uint8_t reg, uint8_t mask, uint8_t val)
{
    cs_low(m);
    SPI.transfer(INS_BIT_MODIFY);
    SPI.transfer(reg);
    SPI.transfer(mask);
    SPI.transfer(val);
    cs_high(m);
}

Mcp2515Status mcp2515_set_mode(Mcp2515 *m, Mcp2515Mode mode)
{
    bit_modify(m, REG_CANCTRL, 0xE0, (uint8_t)mode);
    uint32_t t0 = millis();
    while (millis() - t0 < 50) {
        if ((read_reg(m, REG_CANSTAT) & 0xE0) == (uint8_t)mode)
            return MCP2515_OK;
    }
    return MCP2515_ERR_MODE;
}

Mcp2515Status mcp2515_reset_config(Mcp2515 *m)
{
    const BitTiming *bt = NULL;
    for (unsigned i = 0; i < sizeof(k_timing) / sizeof(k_timing[0]); i++)
        if (k_timing[i].osc == m->osc_hz && k_timing[i].rate == m->bitrate)
            bt = &k_timing[i];
    if (!bt)
        return MCP2515_ERR_BITRATE;

    pinMode(m->cs_pin, OUTPUT);
    digitalWrite(m->cs_pin, HIGH);
    SPI.begin();                                    /* D13 SCK, D11 MOSI, D12 MISO */
    delay(2);

    cs_low(m);
    SPI.transfer(INS_RESET);
    cs_high(m);
    delay(5);                                       /* oscillator start-up */

    /* after reset the chip must be in CONFIG mode: 0x80. 0x00/0xFF = no chip / bad wiring */
    if ((read_reg(m, REG_CANSTAT) & 0xE0) != MCP2515_MODE_CONFIG)
        return MCP2515_ERR_NO_CHIP;

    write_reg(m, REG_CNF1, bt->cnf1);
    write_reg(m, REG_CNF2, bt->cnf2);
    write_reg(m, REG_CNF3, bt->cnf3);
    if (read_reg(m, REG_CNF2) != bt->cnf2)          /* read back = SPI really works */
        return MCP2515_ERR_SPI;

    write_reg(m, REG_RXB0CTRL, 0x64);               /* receive any, rollover to RXB1 */
    write_reg(m, REG_RXB1CTRL, 0x60);               /* receive any                   */
    write_reg(m, REG_CANINTE, CANINTF_RX0IF | CANINTF_RX1IF);   /* INT pin low on RX */
    write_reg(m, REG_CANINTF, 0x00);
    return MCP2515_OK;                              /* still in CONFIG mode */
}

Mcp2515Status mcp2515_init(Mcp2515 *m)
{
    Mcp2515Status st = mcp2515_reset_config(m);
    if (st != MCP2515_OK)
        return st;
    return mcp2515_set_mode(m, MCP2515_MODE_NORMAL);
}

uint8_t mcp2515_read_register(Mcp2515 *m, uint8_t reg)
{
    return read_reg(m, reg);
}

void mcp2515_write_register(Mcp2515 *m, uint8_t reg, uint8_t val)
{
    write_reg(m, reg, val);
}

bool mcp2515_tx_done(Mcp2515 *m)
{
    return (read_reg(m, REG_CANINTF) & CANINTF_TX0IF) != 0;
}

bool mcp2515_tx_pending(Mcp2515 *m)
{
    return (read_reg(m, REG_TXB0CTRL) & TXB_TXREQ) != 0;
}

void mcp2515_abort_tx(Mcp2515 *m)
{
    bit_modify(m, REG_TXB0CTRL, TXB_TXREQ, 0x00);
}

Mcp2515Status mcp2515_send(Mcp2515 *m, const Mcp2515Frame *f)
{
    if (mcp2515_tx_pending(m))
        return MCP2515_ERR_TX_BUSY;

    uint8_t dlc = f->dlc > 8 ? 8 : f->dlc;
    bit_modify(m, REG_CANINTF, CANINTF_TX0IF, 0x00);   /* TX0IF = 1 later means "sent + ACKed" */

    cs_low(m);
    SPI.transfer(INS_LOAD_TXB0);
    SPI.transfer((uint8_t)(f->id >> 3));            /* SIDH */
    SPI.transfer((uint8_t)((f->id & 0x07) << 5));   /* SIDL, standard frame */
    SPI.transfer(0);                                /* EID8 */
    SPI.transfer(0);                                /* EID0 */
    SPI.transfer(dlc);                              /* data frame */
    for (uint8_t i = 0; i < dlc; i++)
        SPI.transfer(f->data[i]);
    cs_high(m);

    /* read the buffer back: if SI (MOSI) is loose, the chip did not get our data */
    if (read_reg(m, REG_TXB0SIDH) != (uint8_t)(f->id >> 3) || (read_reg(m, REG_TXB0DLC) & 0x0F) != dlc)
        return MCP2515_ERR_SPI;

    cs_low(m);
    SPI.transfer(INS_RTS_TXB0);                     /* request to send */
    cs_high(m);
    return MCP2515_OK;
}

static Mcp2515Status read_rx_buffer(Mcp2515 *m, uint8_t ins, Mcp2515Frame *f)
{
    uint8_t hdr[5];
    cs_low(m);
    SPI.transfer(ins);
    for (uint8_t i = 0; i < 5; i++)
        hdr[i] = SPI.transfer(0x00);
    uint8_t dlc = hdr[4] & 0x0F;
    if (dlc > 8)
        dlc = 8;
    for (uint8_t i = 0; i < dlc; i++)
        f->data[i] = SPI.transfer(0x00);
    cs_high(m);                                     /* CS high clears RXnIF */

    if (hdr[1] & (SIDL_IDE | SIDL_SRR))             /* extended or remote: not ours */
        return MCP2515_NO_MSG;
    f->id  = (uint16_t)(((uint16_t)hdr[0] << 3) | (hdr[1] >> 5));
    f->dlc = dlc;
    return MCP2515_OK;
}

Mcp2515Status mcp2515_receive(Mcp2515 *m, Mcp2515Frame *f)
{
    cs_low(m);
    SPI.transfer(INS_READ_STATUS);
    uint8_t st = SPI.transfer(0x00);
    cs_high(m);

    if (st & CANINTF_RX0IF)
        return read_rx_buffer(m, INS_READ_RXB0, f);
    if (st & CANINTF_RX1IF)
        return read_rx_buffer(m, INS_READ_RXB1, f);
    return MCP2515_NO_MSG;
}

void mcp2515_read_regs(Mcp2515 *m, Mcp2515Regs *r)
{
    r->canstat  = read_reg(m, REG_CANSTAT);
    r->canctrl  = read_reg(m, REG_CANCTRL);
    r->eflg     = read_reg(m, REG_EFLG);
    r->tec      = read_reg(m, REG_TEC);
    r->rec      = read_reg(m, REG_REC);
    r->canintf  = read_reg(m, REG_CANINTF);
    r->txb0ctrl = read_reg(m, REG_TXB0CTRL);
}

void mcp2515_clear_errors(Mcp2515 *m)
{
    bit_modify(m, REG_EFLG, 0xC0, 0x00);            /* RX0OVR, RX1OVR */
    bit_modify(m, REG_CANINTF, CANINTF_ERRIF, 0x00);
}
