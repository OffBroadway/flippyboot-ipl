/* 
 * Copyright (c) 2019-2021, Extrems <extrems@extremscorner.org>
 * 
 * This file is part of Swiss.
 * 
 * Swiss is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * Swiss is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * with Swiss.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdbool.h>

#include "usbgecko.h"
#include "picolibc.h"
#include "attr.h"
#include "time.h"

#include "config.h"

#ifndef USE_NATIVE_SPRINTF
#define TINYPRINTF_OVERRIDE_LIBC 1
#include "tinyprintf/tinyprintf.h"
#endif

#ifdef DOLPHIN_PRINT_ENABLE

// __attribute_reloc__ u32 *uart_init;
// __attribute_reloc__ u32 (*InitializeUART)(u32);
// __attribute_reloc__ s32 (*WriteUARTN)(const void *buf, u32 len);

// s32 dolphin_WriteUARTN(const void *buf, u32 len) {
//   if (*uart_init == 0) {
//     InitializeUART(0xe100);
//     *uart_init = 1;
//   }

//     return WriteUARTN(buf, len);
// }

#define custom_WriteUARTN dolphin_WriteUARTN

#endif

#ifdef GECKO_PRINT_ENABLE
extern volatile u32 EXI[3][5];

static void exi_select(void)
{
    EXI[EXI_CHANNEL_1][0] = (EXI[EXI_CHANNEL_1][0] & 0x405) | ((1 << EXI_DEVICE_0) << 7) | (EXI_SPEED_32MHZ << 4);
}

static void exi_deselect(void)
{
    EXI[EXI_CHANNEL_1][0] &= 0x405;
}

static uint32_t exi_imm_read_write(uint32_t data, uint32_t len)
{
    EXI[EXI_CHANNEL_1][4] = data;
    EXI[EXI_CHANNEL_1][3] = ((len - 1) << 4) | (EXI_READ_WRITE << 2) | 0b01;
    while (EXI[EXI_CHANNEL_1][3] & 0b01);
    return EXI[EXI_CHANNEL_1][4] >> ((4 - len) * 8);
}

static bool usb_probe(void)
{
    uint16_t val;

    exi_select();
    val = exi_imm_read_write(0x9 << 28, 2);
    exi_deselect();

    return val == 0x470;
}

static bool usb_receive_byte(uint8_t *data)
{
    uint16_t val;

    exi_select();
    val = exi_imm_read_write(0xA << 28, 2); *data = val;
    exi_deselect();

    return !(val & 0x800);
}

static bool usb_transmit_byte(const uint8_t *data)
{
    uint16_t val;

    exi_select();
    val = exi_imm_read_write(0xB << 28 | *data << 20, 2);
    exi_deselect();

    return !(val & 0x400);
}

static bool usb_transmit_check(void)
{
    uint8_t val;

    exi_select();
    val = exi_imm_read_write(0xC << 28, 1);
    exi_deselect();

    return !(val & 0x4);
}

static bool usb_receive_check(void)
{
    uint8_t val;

    exi_select();
    val = exi_imm_read_write(0xD << 28, 1);
    exi_deselect();

    return !(val & 0x4);
}

static int usb_transmit(const void *data, int size, int minsize)
{
    int i = 0, j = 0, check = 1;

    while (i < size) {
        if ((check && usb_transmit_check()) ||
            (check = usb_transmit_byte(data + i))) {
            j = i % 128;
            if (i < minsize)
                continue;
            else break;
        }

        i++;
        check = i % 128 == j;
    }

    return i;
}

static int usb_receive(void *data, int size, int minsize)
{
    int i = 0, j = 0, check = 1;

    while (i < size) {
        if ((check && usb_receive_check()) ||
            (check = usb_receive_byte(data + i))) {
            j = i % 64;
            if (i < minsize)
                continue;
            else break;
        }

        i++;
        check = i % 64 == j;
    }

    return i;
}

s32 usb_WriteUARTN(const void *buf, u32 len)
{
    if (usb_probe()) {
        usb_transmit(buf, len, len);
        return 0;
    }

    return -1;
}

#define custom_WriteUARTN usb_WriteUARTN

#endif


#ifdef DEBUG

u64 first_print = 0;
void custom_OSReport(const char *fmt, ...) {
    if (first_print == 0) {
        first_print = gettime();
    }

    va_list args;
    static char line[240];
    static char buf[256];

    va_start(args, fmt);
    int length = vsnprintf((char *)buf, sizeof(buf), (char *)fmt, args);
    // int length = vsnprintf((char *)line, sizeof(line), (char *)fmt, args);
    // length = sprintf(buf, "(%f): %s", (f32)diff_usec(first_print, gettime()) / 1000.0, line);

    custom_WriteUARTN(buf, length);

    va_end(args);
}

#else

void custom_OSReport(const char *fmt, ...) {
    (void)fmt;
}

#endif
