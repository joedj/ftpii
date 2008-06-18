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
#ifndef _COMMON_H_
#define _COMMON_H_

#include <ogcsys.h>

const char *CRLF;
const u32 CRLF_LENGTH;

void initialise_fat();

u8 initialise_reset_button();

u8 initialise_mount_buttons();

void die(char *msg);

void initialise_video();

void wait_for_network_initialisation();

s32 create_server(u16 port);

s32 accept_peer(s32 server, struct sockaddr_in *addr);

s32 write_exact(s32 s, char *buf, s32 length);

s32 write_from_file(s32 s, FILE *f);

s32 read_exact(s32 s, char *buf, s32 length);

s32 read_to_file(s32 s, FILE *f);

u32 split(char *s, char sep, u32 maxsplit, char *result[]);

#endif /* _COMMON_H_ */
