// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#ifndef _FST_H
#define _FST_H

#define FST_MAXPATHLEN 128

bool FST_Mount();
bool FST_Unmount();
u64 FST_LastAccess();

#endif /* _FST_H */
