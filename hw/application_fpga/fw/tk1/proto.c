/*
 * Copyright (C) 2022, 2023 - Tillitis AB
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "proto.h"
#include "../tk1_mem.h"
#include "assert.h"
#include "led.h"
#include "lib.h"
#include "state.h"
#include "types.h"

// clang-format off
static volatile uint32_t *can_rx = (volatile uint32_t *)TK1_MMIO_UART_RX_STATUS;
static volatile uint32_t *rx =     (volatile uint32_t *)TK1_MMIO_UART_RX_DATA;
static volatile uint32_t *can_tx = (volatile uint32_t *)TK1_MMIO_UART_TX_STATUS;
static volatile uint32_t *tx =     (volatile uint32_t *)TK1_MMIO_UART_TX_DATA;
// clang-format on

static uint8_t genhdr(uint8_t id, uint8_t endpoint, uint8_t status,
		      enum cmdlen len);
static int parseframe(uint8_t b, struct frame_header *hdr);
static void write(uint8_t *buf, size_t nbytes);
static int read(uint8_t *buf, size_t bufsize, size_t nbytes, uint8_t *mode,
		uint8_t *mode_bytes_left);
static size_t bytelen(enum cmdlen cmdlen);

static uint8_t genhdr(uint8_t id, uint8_t endpoint, uint8_t status,
		      enum cmdlen len)
{
	return (id << 5) | (endpoint << 3) | (status << 2) | len;
}

int readcommand(struct frame_header *hdr, uint8_t *cmd, int state,
		uint8_t *mode, uint8_t *mode_bytes_left)
{
	uint8_t in = 0;

	set_led((state == FW_STATE_LOADING) ? LED_BLACK : LED_WHITE);
	in = readbyte(mode, mode_bytes_left);

	if (parseframe(in, hdr) == -1) {
		htif_puts("Couldn't parse header\n");
		return -1;
	}

	(void)memset(cmd, 0, CMDLEN_MAXBYTES);
	// Now we know the size of the cmd frame, read it all
	if (read(cmd, CMDLEN_MAXBYTES, hdr->len, mode, mode_bytes_left) != 0) {
		htif_puts("read: buffer overrun\n");
		return -1;
	}

	// Is it for us?
	if (hdr->endpoint != DST_FW) {
		htif_puts("Message not meant for us\n");
		return -1;
	}

	return 0;
}

static int parseframe(uint8_t b, struct frame_header *hdr)
{
	if ((b & 0x80) != 0) {
		// Bad version
		return -1;
	}

	if ((b & 0x4) != 0) {
		// Must be 0
		return -1;
	}

	hdr->id = (b & 0x60) >> 5;
	hdr->endpoint = (b & 0x18) >> 3;
	hdr->len = bytelen(b & 0x3);

	return 0;
}

// Send a firmware reply with a frame header, response code rspcode and
// following data in buf
void fwreply(struct frame_header hdr, enum fwcmd rspcode, uint8_t *buf)
{
	size_t nbytes = 0;
	enum cmdlen len = 0; // length covering (rspcode + length of buf)

	switch (rspcode) {
	case FW_RSP_NAME_VERSION:
		len = LEN_32;
		break;

	case FW_RSP_LOAD_APP:
		len = LEN_4;
		break;

	case FW_RSP_LOAD_APP_DATA:
		len = LEN_4;
		break;

	case FW_RSP_LOAD_APP_DATA_READY:
		len = LEN_128;
		break;

	case FW_RSP_GET_UDI:
		len = LEN_32;
		break;

	default:
		htif_puts("fwreply(): Unknown response code: 0x");
		htif_puthex(rspcode);
		htif_lf();
		return;
	}

	nbytes = bytelen(len);

	// Mode Protocol Header
	writebyte(MODE_CDC);
	writebyte(2);

	// Frame Protocol Header
	writebyte(genhdr(hdr.id, hdr.endpoint, 0x0, len));

	// FW protocol header
	writebyte(rspcode);
	nbytes--;

	while (nbytes > 0) {
		// Limit transfers to 64 bytes (2 byte header + 62 byte data) to
		// fit in a single USB frame.
		size_t tx_count = nbytes > 62 ? 62 : nbytes;
		// Mode Protocol Header
		writebyte(MODE_CDC);
		writebyte(tx_count & 0xff);

		// Data
		write(buf, tx_count);
		nbytes -= tx_count;
		buf += tx_count;
	}
}

void writebyte(uint8_t b)
{
	for (;;) {
		if (*can_tx) {
			*tx = b;
			return;
		}
	}
}

static void write(uint8_t *buf, size_t nbytes)
{
	for (int i = 0; i < nbytes; i++) {
		writebyte(buf[i]);
	}
}

uint8_t readbyte_(void)
{
	for (;;) {
		if (*can_rx) {
			uint32_t b = *rx;
			return b;
		}
	}
}

uint8_t readbyte(uint8_t *mode, uint8_t *mode_bytes_left)
{
	if (*mode_bytes_left == 0) {
		*mode = readbyte_();
		if (*mode != MODE_CDC) {
			htif_puts("We only support MODE_CDC\n");
			assert(1 == 2);
		} else {
			*mode_bytes_left = readbyte_();
		}
	}
	uint8_t b = readbyte_();
	*mode_bytes_left -= 1;
	return b;
}

static int read(uint8_t *buf, size_t bufsize, size_t nbytes, uint8_t *mode,
		uint8_t *mode_bytes_left)
{
	if (nbytes > bufsize) {
		return -1;
	}

	for (int n = 0; n < nbytes; n++) {
		buf[n] = readbyte(mode, mode_bytes_left);
	}

	return 0;
}

// bytelen returns the number of bytes a cmdlen takes
static size_t bytelen(enum cmdlen cmdlen)
{
	int len = 0;

	switch (cmdlen) {
	case LEN_1:
		len = 1;
		break;

	case LEN_4:
		len = 4;
		break;

	case LEN_32:
		len = 32;
		break;

	case LEN_128:
		len = 128;
		break;

	default:
		// Shouldn't happen
		assert(1 == 2);
	}

	return len;
}
