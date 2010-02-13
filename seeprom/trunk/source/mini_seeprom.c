/*
    mini - a Free Software replacement for the Nintendo/BroadOn IOS.
    SEEPROM support

Copyright (C) 2008, 2009    Sven Peter <svenpeter@gmail.com>
Copyright (C) 2008, 2009    Haxx Enterprises <bushing@gmail.com>
Copyright (C) 2008, 2009    Hector Martin "marcan" <marcan@marcansoft.com>
Copyright (C) 2008, 2009    John Kelley <wiidev@kelley.ca>

# This code is licensed to you under the terms of the GNU GPL, version 2;
# see http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/
#include <ogc/machine/processor.h>
#include <ogcsys.h>
#include <unistd.h>

#define HW_REG_BASE 0xd800000
#define HW_GPIO1OUT (HW_REG_BASE + 0x0e0)
#define HW_GPIO1IN (HW_REG_BASE + 0x0e8)

enum {
    GP_EEP_CS = 0x000400,
    GP_EEP_CLK = 0x000800,
    GP_EEP_MOSI = 0x001000,
    GP_EEP_MISO = 0x002000
};

#define eeprom_delay() usleep(5)

static void send_bits(int b, int bits) {
    while (bits--) {
        if (b & (1 << bits))
            mask32(HW_GPIO1OUT, 0, GP_EEP_MOSI);
        else
            mask32(HW_GPIO1OUT, GP_EEP_MOSI, 0);
        eeprom_delay();
        mask32(HW_GPIO1OUT, 0, GP_EEP_CLK);
        eeprom_delay();
        mask32(HW_GPIO1OUT, GP_EEP_CLK, 0);
        eeprom_delay();
    }
}

static int recv_bits(int bits) {
    int res = 0;
    while (bits--) {
        res <<= 1;
        mask32(HW_GPIO1OUT, 0, GP_EEP_CLK);
        eeprom_delay();
        mask32(HW_GPIO1OUT, GP_EEP_CLK, 0);
        eeprom_delay();
        res |= !!(read32(HW_GPIO1IN) & GP_EEP_MISO);
    }
    return res;
}

int seeprom_read(void *dst, int offset, int size) {
    int i;
    u16 *ptr = (u16 *)dst;
    u16 recv;

    if (size & 1)
        return -1;

    mask32(HW_GPIO1OUT, GP_EEP_CLK, 0);
    mask32(HW_GPIO1OUT, GP_EEP_CS, 0);
    eeprom_delay();

    for (i = 0; i < size; i++) {
        mask32(HW_GPIO1OUT, 0, GP_EEP_CS);
        send_bits((0x600 | (offset + i)), 11);
        recv = recv_bits(16);
        *ptr++ = recv;
        mask32(HW_GPIO1OUT, GP_EEP_CS, 0);
        eeprom_delay();
    }

    return size;
}
