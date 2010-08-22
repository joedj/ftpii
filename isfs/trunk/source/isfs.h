// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#ifndef _LIBISFS_H
#define _LIBISFS_H

#include <ogc/isfs.h>

#define ISFS_MAXPATHLEN (ISFS_MAXPATH + 1)

bool ISFS_Mount();
bool ISFS_Unmount();
s32 ISFS_SU();

#endif /* _LIBISFS_H */
