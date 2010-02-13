/*
        mini - a Free Software replacement for the Nintendo/BroadOn IOS.
        SEEPROM support

Copyright (C) 2008, 2009    Sven Peter <svenpeter@gmail.com>

# This code is licensed to you under the terms of the GNU GPL, version 2;
# see http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/
#ifndef _MINI_SEEPROM_H
#define _MINI_SEEPROM_H

int seeprom_read(void *dst, int offset, int size);

#endif /* _MINI_SEEPROM_H */
