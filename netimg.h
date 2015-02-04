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
 * Authors: Sugih Jamin (jamin@eecs.umich.edu)
 *
*/
#ifndef __NETIMG_H__
#define __NETIMG_H__

#ifdef _WIN32
#define EADDRNOTAVAIL WSAEADDRNOTAVAIL
#define usleep(usec) Sleep(usec/1000)
#define close(sockdesc) closesocket(sockdesc)
#define perror(errmsg) { fprintf(stderr, "%s: %d\n", (errmsg), WSAGetLastError()); }
#endif
#define net_assert(err, errmsg) { if ((err)) { perror(errmsg); assert(!(err)); } }
/* BEGIN SOLUTION */
#define PBO 0
/* END SOLUTION */

#define NETIMG_WIDTH  640
#define NETIMG_HEIGHT 480

#define NETIMG_MAXFNAME    256  // including terminating NULL
#define NETIMG_PORTSEP     ':'

#define NETIMG_VERS 0x2

#define NETIMG_QRY 0x1
#define NETIMG_RPY 0x2

#define NETIMG_FOUND 1
#define NETIMG_NFOUND 0
#define NETIMG_EVERS -1
#define NETIMG_ESIZE -2
#define NETIMG_EBUSY -3

#define NETIMG_QLEN       10 
#define NETIMG_LINGER      2
#define NETIMG_NUMSEG     50
#define NETIMG_MSS      1440
#define NETIMG_USLEEP 250000    // 250 ms

#include "ltga.h"
#include <netinet/in.h>    // struct in_addr

typedef struct {            // peer address structure
  struct in_addr peer_addr; // IPv4 address
  u_short peer_port;        // port#, always stored in network byte order
  u_short peer_rsvd;        // reserved field
} peer_t;

// Message format:              8 bit  8 bit     16 bit
typedef struct {            // +------+------+-------------+
  char pm_vers, pm_type;    // | vers | type |   #peers    |
  u_short pm_npeers;        // +------+------+-------------+
  peer_t pm_peer;           // |     peer ipv4 address     | 
} pmsg_t;                   // +---------------------------+
                            // |  peer port# |   reserved  |
                            // +---------------------------+

typedef struct {            // peer table entry
	int pte_sd;               // socket peer is connected at
	bool pending;
	char *pte_pname;          // peer's fqdn
	peer_t pte_peer;          // peer's address+port#
} pte_t;                    // ptbl entry


typedef struct{
	char vers, type;
	u_short search_ID;
	peer_t img_peer;
	char img_name[NETIMG_MAXFNAME];    

} query_t;


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

extern int sd;
extern long img_size;
extern char *image;

extern void netimg_glutinit(int *argc, char *argv[], void (*idlefunc)());
extern void netimg_imginit();

extern int imgdb_loadimg(char *fname, LTGA *image, imsg_t *imsg, long *img_size);
extern int imgdb_sockinit(peer_t* img_peer);
extern int imgdb_accept(int sd);
extern int imgdb_recvqry(int td, char *fname, imsg_t *imsg);
extern void imgdb_sendimg(int td, imsg_t *imsg, LTGA *image, long img_size);

#endif /* __NETIMG_H__ */
