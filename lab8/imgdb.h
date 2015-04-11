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

#include "netimg.h"
#include "ltga.h"
#include "socks.h"

#ifdef _WIN32
#define IMGDB_DIRSEP "\\"
#else
#define IMGDB_DIRSEP "/"
#endif
#define IMGDB_FOLDER    "."

#define IMGDB_BPTOK     512   // bytes per token

#define IMGDB_MINFLOW           2   // minimum number of flows
#define IMGDB_MAXFLOW          20   // maximum number of flows
#define IMGDB_LRATE         10240   // link rate, in Kbps
#define IMGDB_MINLRATE          1   // minimum link rate, in Mbps
#define IMGDB_MAXLRATE         10   // maximum link rate, in Mbps

class Flow {
  LTGA curimg;
  long imgsize;
  char *ip;               // pointer to start of image
  int snd_next;           // offset from start of image

  struct sockaddr_in client;

  unsigned short mss;     // receiver's maximum segment size, in bytes
  unsigned short frate;   // flow rate, in Kbps

  unsigned short datasize;
  int segsize;

  float Fi;               // finish time of last pkt sent

  ihdr_t hdr;
  struct msghdr msg;
  struct iovec iov[NETIMG_NUMIOV];

  char readimg(char *imgname, int verbose);
  double marshall_imsg(imsg_t *imsg);
public:
  int in_use;             // 1: in use; 0: not
  struct timeval start;   // flow creation wall-clock time

  Flow() { in_use = 0; }
  void init(int sd, struct sockaddr_in *qhost,
            iqry_t *iqry, imsg_t *imsg, float currFi);
  float nextFi(float multiplier);
  int sendpkt(int sd, int fd, float currFi);
  /* Flow::done: set flow to not "in_use" and return the flow's
     reserved rate to be deducted from total reserved rate. */
  unsigned short done() { in_use = 0; return (frate); }
};

class imgdb {
  int sd;  // image socket
  struct sockaddr_in self;
  char sname[NETIMG_MAXFNAME];

  Flow flow[IMGDB_MAXFLOW];
  unsigned short rsvdrate;  // reserved rate, in Kbps
  unsigned short linkrate;  // in Kbps
  float currFi;             // current finish time

  /*
  float bsize; // bucket size, in number of tokens
  float trate; // token generation rate, in tokens/sec
  */

  // to implement gated transmission start
  short minflow;  // number of flows to trigger gated start
  short nflow;    // number of resident flows
  short started;  // or not

  int args(int argc, char *argv[]);

  // image query-reply
  char recvqry(int sd, struct sockaddr_in *qhost, iqry_t *iqry);
  void sendimsg(int sd, struct sockaddr_in *qhost, imsg_t *imsg);

public:
  imgdb(int argc, char *argv[]);

  int handleqry();
  void sendpkt();
};  

#endif /* __IMGDB_H__ */
