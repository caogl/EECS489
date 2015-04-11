/* 
 * Copyright (c) 2014, 2015 University of Michigan, Ann Arbor.
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
#define ioctl(sockdesc, request, onoff) ioctlsocket(sockdesc, request, onoff)
#define perror(errmsg) { fprintf(stderr, "%s: %d\n", (errmsg), WSAGetLastError()); }
#endif
#define net_assert(err, errmsg) { if ((err)) { perror(errmsg); assert(!(err)); } }

#define NETIMG_SEED 48915

#define NETIMG_WIDTH  640
#define NETIMG_HEIGHT 480

#define NETIMG_MAXFNAME  256   // including terminating NULL
#define NETIMG_PORTSEP   ':'

#define NETIMG_MAXTRIES    3
#define NETIMG_NUMIOV      2

#define NETIMG_MAXWIN    255   // 2^8 -1
#define NETIMG_MINWIN      4
#define NETIMG_RCVWIN     10
#define NETIMG_UDPIP      28   // 20 bytes IP, 8 bytes UDP headers
#define NETIMG_MSS      3072
#define NETIMG_MINSS     264   // sizeof(iqry_t)

#define NETIMG_VERS    0x40

// imsg_t::img_type from client:
#define NETIMG_SYNQRY  0x10

// imsg_t::img_type from server:
#define NETIMG_FOUND   0x02
#define NETIMG_NFOUND  0x04
#define NETIMG_ERROR   0x08
#define NETIMG_ESIZE   0x09
#define NETIMG_EVERS   0x0a
#define NETIMG_ETYPE   0x0b
#define NETIMG_ENAME   0x0c
#define NETIMG_EBUSY   0x0d
#define NETIMG_EFULL   0x0e      // link full, used in Lab8

#define NETIMG_DATA    0x20

#define NETIMG_FRATE       512   // flow rate, in Kbps
#define NETIMG_MINFRATE     10   // in Kbps
#define NETIMG_LRATE     10240   // link rate, in Kbps, so 10 Mbps, used in Lab8

typedef struct {                  // NETIMG_SYNQRY
  unsigned char iq_vers;
  unsigned char iq_type;
  unsigned short iq_mss;          // receiver's maximum segment size, in bytes
  unsigned char iq_rwnd;          // receiver's window size, in number of packets of mss
  unsigned char iq_rsvd;          // reserved field
  unsigned short iq_frate;        // flow rate, in Kbps
  char iq_name[NETIMG_MAXFNAME];  // must be NULL terminated
} iqry_t;

typedef struct {               
  unsigned char im_vers;
  unsigned char im_type;       // NETIMG_FOUND, NETIMG_NFOUND, or one of the error codes
  unsigned char im_rsvd[3];    // unused
  unsigned char im_depth;      // in bytes, not in bits as
                               // returned by LTGA.GetPixelDepth()
  unsigned short im_format;
  unsigned short im_width;
  unsigned short im_height;
} imsg_t;

typedef struct {
  unsigned char ih_vers;
  unsigned char ih_type;       // NETIMG_DATA
  unsigned short ih_size;      // actual data size, in bytes,
                               // not including header
  unsigned int ih_seqn;
} ihdr_t;

extern void netimg_glutinit(int *argc, char *argv[], void (*idlefunc)());
extern void netimg_imginit(unsigned short format);

#endif /* __NETIMG_H__ */
