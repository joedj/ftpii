// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#ifndef _WOD_H
#define _WOD_H

#define WOD_DEVICE 0x574f
#define WOD_MAXPATHLEN 120

bool WOD_Mount();
bool WOD_Unmount();
u64 WOD_LastAccess();

#endif /* _WOD_H */
