/* 
 * Copyright (c) 2014 University of Michigan, Ann Arbor.
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

#include <assert.h>

#ifdef _WIN32
#define usleep(usec) Sleep(usec/1000)
#define close(sockdesc) closesocket(sockdesc)
#define ioctl(sockdesc, request, onoff) ioctlsocket(sockdesc, request, onoff)
#define perror(errmsg) { fprintf(stderr, "%s: %d\n", (errmsg), WSAGetLastError()); }
#endif
#define net_assert(err, errmsg) { if ((err)) { perror(errmsg); assert(!(err)); } }

#define NETIMG_WIDTH  640
#define NETIMG_HEIGHT 480

#define NETIMG_MAXFNAME  256  // including terminating NULL
#define NETIMG_PORTSEP   ':'

#define NETIMG_NUMIOVEC    2

#define NETIMG_MAXWIN    255   // 2^8 -1
#define NETIMG_RCVWIN    255
#define NETIMG_UDPMSS  65527
#define NETIMG_MINSS     264   // sizeof(iqry_t)
#define NETIMG_MSS     10248

#define NETIMG_MAXFLOW     20   // maximum number of concurrent flows
#define NETIMG_FRATE      512   // flow rate, in Kbps
#define NETIMG_LINKRATE 10240   // 10 Mbps

#define NETIMG_VERS       0x4

#define NETIMG_SYN        0x01  // pkts from client
#define NETIMG_DIM        0x10  // pkts from server
#define NETIMG_DAT        0x20

#define NETIMG_FOUND      0x1   // insufficient resources to add flow
#define NETIMG_FULL       0x2   // insufficient resources to add flow

// special seqno's
#define NETIMG_MAXSEQ  2147483647 // 2^31-1

typedef struct {
  unsigned char iq_vers;
  unsigned char iq_type;          // NETIMG_SYN
  unsigned short iq_mss;
  unsigned short iq_frate;        // flow rate, in Kbps
  char iq_name[NETIMG_MAXFNAME];  // must be NULL terminated
} iqry_t;

typedef struct {
  unsigned char im_vers;
  unsigned char im_type;          // NETIMG_DIM
  unsigned char im_found;         // 0: not found; 0x1: found; 0x2: no resource
  unsigned char im_depth;         // in bytes, not in bits as returned by LTGA.GetPixelDepth()
  unsigned short im_format;
  unsigned short im_width;
  unsigned short im_height;
} imsg_t;

typedef struct {
  unsigned char ih_vers;
  unsigned char ih_type;          // NETIMG_DAT
  unsigned short ih_size;         // actual data size, in bytes, not including header
  unsigned int ih_seqn;
} ihdr_t;

extern void netimg_glutinit(int *argc, char *argv[], void (*idlefunc)());
extern void netimg_imginit();

#endif /* __NETIMG_H__ */
