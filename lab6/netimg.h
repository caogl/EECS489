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
#define NETIMG_RCVWIN     12
#define NETIMG_FECWIN     11
#define NETIMG_UDPIP      28   // 20 bytes IP, 8 bytes UDP headers
#define NETIMG_MSS     10276   // 10KB segments, corresponds to
                               // SO_SNDBUF/SO_RCVBUF so including
                               // the 36-byte headers (ihdr_t+UDP+IP)
#define NETIMG_MINSS      40   // 36 bytes headers, 4 bytes data
#define NETIMG_PDROP   0.021   // recommended between 0.011 and 0.11,
                               // -1.0 to turn off, values larger
                               // than 0.11 to simulate massive drops
#define NETIMG_SLEEP       0   // secs, set to 20 or larger for X11
                               // forwarding on CAEN over ADSL, to
                               // prevent unnecessary retransmissions
#define NETIMG_USLEEP 500000   // 500 ms

#define NETIMG_VERS    0x30

// imsg_t::img_type from client:
#define NETIMG_SYNQRY  0x10
#define NETIMG_ACK     0x11    // PA3

// imsg_t::img_type from server:
#define NETIMG_FOUND   0x02
#define NETIMG_NFOUND  0x04
#define NETIMG_ERROR   0x08
#define NETIMG_ESIZE   0x09
#define NETIMG_EVERS   0x0a
#define NETIMG_ETYPE   0x0b
#define NETIMG_ENAME   0x0c
#define NETIMG_EBUSY   0x0d

#define NETIMG_DATA    0x20
#define NETIMG_FEC     0x60    // PA3
#define NETIMG_FIN     0xa0    // PA3

// special seqno's for PA3:
#define NETIMG_MAXSEQ  2147483647 // 2^31-1
#define NETIMG_DIMSEQ  4294967295 // 2^32-1
#define NETIMG_FINSEQ  4294967294 // 2^32-2

typedef struct {                  // NETIMG_SYNQRY
  unsigned char iq_vers;
  unsigned char iq_type;
  unsigned short iq_mss;          // receiver's maximum segment size
  unsigned char iq_rwnd;          // receiver's window size
  unsigned char iq_fwnd;          // receiver's FEC window size
                                  // used in Lab6 and PA3
  char iq_name[NETIMG_MAXFNAME];  // must be NULL terminated
} iqry_t;

typedef struct {               
  unsigned char im_vers;
  unsigned char im_type;       // NETIMG_FOUND or NETIMG_NFOUND
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
                               // Lab6: NETIMG_FEC,
                               // PA3: NETIMG_ACK, NETIMG_FIN
  unsigned short ih_size;      // actual data size, in bytes,
                               // not including header
  unsigned int ih_seqn;
} ihdr_t;

extern void netimg_glutinit(int *argc, char *argv[], void (*idlefunc)());
extern void netimg_imginit(unsigned short format);

#endif /* __NETIMG_H__ */
