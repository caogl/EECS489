/* 
 * Copyright (c) 2015 University of Michigan, Ann Arbor.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of Michigan, Ann Arbor. The name of the University 
 * may not be used to endorse or promote products derived from this 
 * software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Author: Sugih Jamin (jamin@eecs.umich.edu)
 *
*/
#ifndef __NETIMG_H__
#define __NETIMG_H__

#ifdef _WIN32
#define usleep(usec) Sleep(usec/1000)
#define perror(errmsg) { fprintf(stderr, "%s: %d\n", (errmsg), WSAGetLastError()); }
#endif
#define net_assert(err, errmsg) { if ((err)) { perror(errmsg); assert(!(err)); } }

#define NETIMG_WIDTH  640
#define NETIMG_HEIGHT 480

#define NETIMG_MAXFNAME    256  // including terminating NULL
#define NETIMG_PORTSEP     ':'

#define NETIMG_VERS 0x2

#define NETIMG_QRY 0x1      // DO NOT REMOVE used in PA1
#define NETIMG_RPY 0x2      // DO NOT REMOVE used in PA1

#define NETIMG_NFOUND 0x0
#define NETIMG_FOUND  0x1
#define NETIMG_ESIZE  0x9
#define NETIMG_EVERS  0xa
#define NETIMG_ETYPE  0xb
#define NETIMG_ENAME  0xc
#define NETIMG_EBUSY  0xd

#define NETIMG_QLEN       10 
#define NETIMG_LINGER      2
#define NETIMG_NUMSEG     25
#define NETIMG_MSS      1440
#define NETIMG_USLEEP 250000    // 250 ms
#define NETIMG_SECTIO 1         // timeout seconds DO NOT REMOVE used in PA1
#define NETIMG_USECTIO 0        // timeout useconds DO NOT REMOVE used in PA1

typedef struct {               
  unsigned char iq_vers;
  unsigned char iq_type;
  char iq_name[NETIMG_MAXFNAME];  // must be NULL terminated
} iqry_t;

typedef struct {
  unsigned char im_vers;
  unsigned char im_type;
  unsigned char im_found;
  unsigned char im_depth;   // in bytes, not in bits as returned by LTGA.GetPixelDepth()
  unsigned short im_format;
  unsigned short im_width;
  unsigned short im_height;
  unsigned char im_adepth;  // not used
  unsigned char im_rle;     // not used
} imsg_t;

extern void netimg_glutinit(int *argc, char *argv[], void (*idlefunc)());
extern void netimg_imginit();

#endif /* __NETIMG_H__ */
