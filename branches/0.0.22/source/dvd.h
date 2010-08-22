/*

Copyright (C) 2008 Joseph Jordan <joe.ftpii@psychlaw.com.au>

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from
the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1.The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software in a
product, an acknowledgment in the product documentation would be
appreciated but is not required.

2.Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.

3.This notice may not be removed or altered from any source distribution.

*/
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
