// Copyright 2010 Joseph Jordan <joe.ftpii@psychlaw.com.au>
// This code is licensed to you under the terms of the GNU GPL, version 2;
// see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
#ifndef _FTP_H_
#define _FTP_H_

void accept_ftp_client(s32 server);
void set_ftp_password(char *new_password);
bool process_ftp_events(s32 server);
void cleanup_ftp();

#endif /* _FTP_H_ */
