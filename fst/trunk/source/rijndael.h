/* 
    Rijndael Block Cipher - rijndael.c

    Written by Mike Scott 21st April 1999
    mike@compapp.dcu.ie

    Permission for free direct or derivative use is granted subject 
    to compliance with any conditions that the originators of the 
    algorithm place on its exploitation.  
*/
#ifndef _RIJNDAEL_H
#define _RIJNDAEL_H

void aes_set_key(u8 *key);
void aes_decrypt(u8 *iv, u8 *inbuf, u8 *outbuf, unsigned long long len);

#endif /* _RIJNDAEL_H */
