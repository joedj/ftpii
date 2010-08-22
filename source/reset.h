// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#ifndef _RESET_H_
#define _RESET_H_

u8 reset();

void set_reset_flag();

void initialise_reset_buttons();

void die(char *msg, int errnum);

bool check_reset_synchronous();

void maybe_poweroff();

#endif /* _RESET_H_ */
