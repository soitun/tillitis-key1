// SPDX-FileCopyrightText: 2025 Tillitis AB <tillitis.se>
// SPDX-License-Identifier: BSD-2-Clause

#include <stddef.h>
#include <stdint.h>

#ifndef TKEY_IO_H
#define TKEY_IO_H

// I/O endpoints. Keep it as bits possible to use in a bitmask in
// readselect().
//
// Note that the the TKEYCTRL, CDC, and HID should be kept the same on
// the CH552 side.
enum ioend {
	IO_NONE = 0x00,	    // No endpoint
	IO_UART = 0x01,	    // Only destination, raw UART access
	IO_QEMU = 0x10,	    // Only destination, QEMU debug port
	IO_TKEYCTRL = 0x20, // HID debug port
	IO_CDC = 0x40,	    // CDC "serial port"
	IO_HID = 0x80,	    // HID security token
};

void write(enum ioend dest, const uint8_t *buf, size_t nbytes);
int read(enum ioend src, uint8_t *buf, size_t bufsize, size_t nbytes);
int uart_read(uint8_t *buf, size_t bufsize, size_t nbytes);
int readselect(int bitmask, enum ioend *endpoint, uint8_t *len);
void putchar(enum ioend dest, const uint8_t ch);
void puthex(enum ioend dest, const uint8_t ch);
void putinthex(enum ioend dest, const uint32_t n);
void puts(enum ioend dest, const char *s);
void hexdump(enum ioend dest, void *buf, int len);
#endif
