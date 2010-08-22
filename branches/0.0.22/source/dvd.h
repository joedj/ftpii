// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#ifndef _DVD_H_
#define _DVD_H_

bool dvd_mountWait();

void set_dvd_mountWait(bool state);

u64 dvd_last_access();

s32 dvd_stop();

void dvd_unmount();

s32 dvd_eject();

void check_dvd_motor_timeout(u64 now);

void check_dvd_mount();

#endif /* _DVD_H_ */
