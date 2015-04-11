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
#ifndef __IMGDB_H__
#define __IMGDB_H__

#include "ltga.h"
#include "socks.h"

#ifdef _WIN32
#define IMGDB_DIRSEP "\\"
#else
#define IMGDB_DIRSEP "/"
#endif
#define IMGDB_FOLDER    "."

#define IMGDB_BPTOK     512   // bytes per token

class imgdb {
  struct sockaddr_in self;
  char sname[NETIMG_MAXFNAME];

  struct sockaddr_in client;

  unsigned short mss;   // receiver's maximum segment size, in bytes
  unsigned char rwnd;   // receiver's window, in packets, each of size <= mss
  unsigned short frate; // flow rate, in Kbps
  float bsize; // bucket size, in number of tokens
  float trate; // token generation rate, in tokens/sec

  LTGA curimg;

public:
  int sd;  // image socket

  imgdb() { // default constructor
    sd = socks_servinit((char *) "imgdb", &self, sname); // Task 1
  }

  //  int args(int argc, char *argv[]);

  char readimg(char *imgname, int verbose);

  // image query-reply
  void handleqry();
  char recvqry(int sd, iqry_t *iqry);
  double marshall_imsg(imsg_t *imsg);
  int sendpkt(int sd, char *pkt, int size);
  void sendimg(int sd, imsg_t *imsg, char *image, long imgsize, int numseg);
};  

#endif /* __IMGDB_H__ */
