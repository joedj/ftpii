// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#ifndef _ISO_H
#define _ISO_H

#define ISO_MAXPATHLEN 128

bool ISO9660_Mount();
bool ISO9660_Unmount();
u64 ISO9660_LastAccess();

#endif /* _ISO_H */
