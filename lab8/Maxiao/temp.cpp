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
#include <stdio.h>         // fprintf(), perror(), fflush()
#include <stdlib.h>        // atoi(), random()
#include <assert.h>        // assert()
#include <limits.h>        // LONG_MAX, INT_MAX
#include <errno.h>         // errno
#include <iostream>
using namespace std;
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>      // socklen_t
#include "wingetopt.h"
#else
#include <string.h>        // memset(), memcmp(), strlen(), strcpy(), memcpy()
#include <unistd.h>        // getopt(), STDIN_FILENO, gethostname()
#include <signal.h>        // signal()
#include <netdb.h>         // gethostbyname(), gethostbyaddr()
#include <netinet/in.h>    // struct in_addr
#include <arpa/inet.h>     // htons(), inet_ntoa()
#include <sys/types.h>     // u_short
#include <sys/socket.h>    // socket API, setsockopt(), getsockname()
#include <sys/ioctl.h>     // ioctl(), FIONBIO
#include <sys/time.h>      // gettimeofday()
#endif
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "ltga.h"
#include "netimg.h"

#define USECSPERSEC 1000000

class Flow {
  LTGA image;
  int img_size;
  char *ip;               // pointer to start of image
  int snd_next;           // offset from start of image

  unsigned short mss;
  unsigned short datasize;
  int segsize;

  unsigned short frate;
  float Fi;                 // finish time of last pkt sent

  ihdr_t hdr;
  struct sockaddr_in dst;
  struct msghdr msg;
  struct iovec iov[NETIMG_NUMIOVEC];

public:
  int in_use;             // 1: in use; 0: not
  struct timeval start;   // flow creation wall-clock time


  Flow() { in_use = 0; }
  void init(int sd, struct sockaddr_in *client,
            iqry_t *iqry, imsg_t *imsg, float currFi);
  float nextFi(float multiplier);
  int sendpkt(int sd, int fd, float currFi);
  /* Flow::done: set flow to not "in_use" and return the flow's
     reserved rate to be deducted from total reserved rate. */
  unsigned short done() { in_use = 0; return (frate); }
};

class WFQ {
  Flow flow[NETIMG_MAXFLOW];
  unsigned short rsvdrate;    // reserved rate, in Kbps
  unsigned short linkrate;    // link capacity, in Kbps
  float currFi;
  
  // the following are not part of WFQ, but to implement gated
  // transmission start
  short nflow, minflow, started;

public:
  WFQ(unsigned short lrate, short mflow) { rsvdrate=0; linkrate=lrate; currFi=0.0; nflow=0; minflow=mflow; started=0; }
  int addflow(int sd);
  void sendpkt(int sd);
};

void
imgdb_usage(char *progname)
{  
  fprintf(stderr, "Usage: %s -l <linkrate [1, 10]> -g <minflow>\n", progname); 
  exit(1);
}

/*
 * imgdb_args: parses command line args.
 *
 * Returns 0 on success or 1 on failure.
 *
 * Nothing else is modified.
 */
int
imgdb_args(int argc, char *argv[], unsigned short *linkrate, short *minflow)
{
  char c;
  extern char *optarg;
  int arg;

  if (argc < 1) {
    return (1);
  }

  *linkrate = NETIMG_LINKRATE;
  *minflow = 2;

  while ((c = getopt(argc, argv, "l:g:")) != EOF) {
    switch (c) {
    case 'l':
      arg = atoi(optarg);
      if (arg < 1 || arg > 10) {
        return(1);
      }
      *linkrate = (unsigned short) arg*1024;
      break;
    case 'g':
      arg = atoi(optarg);
      if (arg < 1 || arg > 10) {
        return(1);
      }
      *minflow = (short) arg;
      break;
    default:
      return(1);
      break;
    }
  }

  return (0);
}

/*
 * imgdb_sockinit: sets up a UDP server socket.
 * Let the call to bind() assign an ephemeral port to the socket.
 * Determine and print out the assigned port number to screen so that user
 * would know which port to use to connect to this server.
 *
 * Terminates process on error.
 * Returns the bound socket id.
*/
int
imgdb_sockinit()
{
  int sd=-1;
  int err, len;
  struct sockaddr_in self;
  char sname[NETIMG_MAXFNAME+1] = { 0 };

#ifdef _WIN32
  WSADATA wsa;

  err = WSAStartup(MAKEWORD(2,2), &wsa);  // winsock 2.2
  net_assert(err, "imgdb: WSAStartup");
#endif

  /* create a UDP socket */
  sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  net_assert((sd < 0), "imgdb_sockinit: socket");
  
  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  self.sin_family = AF_INET;
  self.sin_addr.s_addr = INADDR_ANY;
  self.sin_port = 0;

  /* bind address to socket */
  err = bind(sd, (struct sockaddr *) &self, sizeof(struct sockaddr_in));
  net_assert(err, "imgdb_sockinit: bind");

  /*
   * Obtain the ephemeral port assigned by the OS kernel to this
   * socket and store it in the local variable "self".
   */
  len = sizeof(struct sockaddr_in);
  err = getsockname(sd, (struct sockaddr *) &self, (socklen_t *) &len);
  net_assert(err, "imgdb_sockinit: getsockname");

  /* Find out the FQDN of the current host and store it in the local
     variable "sname".  gethostname() is usually sufficient. */
  err = gethostname(sname, NETIMG_MAXFNAME);
  net_assert(err, "imgdb_sockinit: gethostname");

  /* inform user which port this peer is listening on */
  fprintf(stderr, "imgdb address is %s:%d\n", sname, ntohs(self.sin_port));

  return sd;
}

/*
 * imgdb_imginit: load TGA image from file to *image.
 * Store size of image, in bytes, in *img_size.
 * Initialize *imsg with image's specifics.
 * All three variables must point to valid memory allocated by caller.
 * Terminate process on encountering any error.
 */
void
imgdb_imginit(char *fname, LTGA *image, imsg_t *imsg, int *img_size)
{
  int alpha, greyscale;
  double img_dsize;
  
  imsg->im_vers = NETIMG_VERS;
  imsg->im_type = NETIMG_DIM;

  image->LoadFromFile(fname);

  if (!image->IsLoaded()) {
    imsg->im_found = 0;
  } else {
    imsg->im_found = NETIMG_FOUND;

    cout << "Image: " << endl;
    cout << "     Type   = " << LImageTypeString[image->GetImageType()] 
         << " (" << image->GetImageType() << ")" << endl;
    cout << "     Width  = " << image->GetImageWidth() << endl;
    cout << "     Height = " << image->GetImageHeight() << endl;
    cout << "Pixel depth = " << image->GetPixelDepth() << endl;
    cout << "Alpha depth = " << image->GetAlphaDepth() << endl;
    cout << "RL encoding  = " << (((int) image->GetImageType()) > 8) << endl;
    /* use image->GetPixels()  to obtain the pixel array */
    
    img_dsize = (double) (image->GetImageWidth()*image->GetImageHeight()*(image->GetPixelDepth()/8));
    net_assert((img_dsize > (double) NETIMG_MAXSEQ), "imgdb: image too big");
    *img_size = (int) img_dsize;

    imsg->im_depth = (unsigned char)(image->GetPixelDepth()/8);
    imsg->im_width = htons(image->GetImageWidth());
    imsg->im_height = htons(image->GetImageHeight());
    alpha = image->GetAlphaDepth();
    greyscale = image->GetImageType();
    greyscale = (greyscale == 3 || greyscale == 11);
    if (greyscale) {
      imsg->im_format = htons(alpha ? GL_LUMINANCE_ALPHA : GL_LUMINANCE);
    } else {
      imsg->im_format = htons(alpha ? GL_RGBA : GL_RGB);
    }
  }

  return;
}

/*
 * Flow::init
 * initialize flow by:
 * - indicating that flow is "in_use"
 * - loading and initializing image by calling imgdb_imginit(),
 *   which will update imsg->im_found accordingly.
 *   Also initialize member variables "ip" and "snd_next"
 * - initialize "mss" and "datasize", ensure that socket send
 *   buffer is at least mss size
 * - set flow's reserved rate "frate" to client's specification
 * - initial flow finish time is current global minimum finish time
 * - populate a struct msghdr for sending chunks of image
 * - save current system time as flow start time.  For gated start,
 *   this may be updated later with actual start time.
*/
void Flow::
init(int sd, struct sockaddr_in *client, iqry_t *iqry,
     imsg_t *imsg, float currFi)
{
  int err, usable;
  socklen_t optlen;

  // flow is in use
  in_use = 1;

  // initialize image
  imgdb_imginit(iqry->iq_name, &image, imsg, &img_size);
  // ip points to the start of byte buffer holding image
  ip = (char *) image.GetPixels();
  snd_next = 0;

  mss = iqry->iq_mss;
  /* make sure that the send buffer is of size at least mss. */
  optlen = sizeof(int);
  err = getsockopt(sd, SOL_SOCKET, SO_SNDBUF, &usable, &optlen);
  if (usable < (int) mss) {
    usable = (int) mss;
    err = setsockopt(sd, SOL_SOCKET, SO_SNDBUF, &usable, sizeof(int));
    net_assert((err < 0), "Flow::init: setsockopt SNDBUF");
  }
  datasize = mss - sizeof(ihdr_t);

  // flow's reserved rate as specified by client
  // flow's initial finish time is the current global minimum finish time
  frate = iqry->iq_frate;
  Fi = currFi;

  /* 
   * Populate a struct msghdr with information of the destination client,
   * a pointer to a struct iovec array.  The iovec array should be of size
   * NETIMG_NUMIOVEC.  The first entry of the iovec should be initialized
   * to point to an ihdr_t, which should be re-used for each chunk of data
   * to be sent.
  */
  dst = *client;
  msg.msg_name = &dst;
  msg.msg_namelen = sizeof(sockaddr_in);
  msg.msg_iov = iov;
  msg.msg_iovlen = NETIMG_NUMIOVEC;
  msg.msg_control = NULL;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;
  
  hdr.ih_vers = NETIMG_VERS;
  hdr.ih_type = NETIMG_DAT;
  iov[0].iov_base = &hdr;
  iov[0].iov_len = sizeof(ihdr_t);

  /* for non-gated flow starts */
  gettimeofday(&start, NULL);

  return;
}

/*
 * Flow::nextFi: compute the flow's next finish time
 * from the size of the current segment, the flow's
 * reserved rate, and the multiplier passed in.
 * The multiplier is linkrate/total_reserved_rate.
*/
float Flow::
nextFi(float multiplier)
{
  /* size of this segment */
  segsize = img_size - snd_next;
  segsize = segsize > datasize ? datasize : segsize;

  /* Task 2: YOUR CODE HERE */
  /* Replace the following return statement with your
     computation of the next finish time as indicated above
     and return the result instead. */
  return(0.0);
}

/*
 * Flow::sendpkt:
 * Send the image contained in *image to the client
 * pointed to by *client. Send the image in
 * chunks of segsize, not to exceed mss, instead of
 * as one single image.
 * The argument "sd" is the socket to send packet out of.
 * The argument "fd" is the array index this flow occupies
 * on the WFQ.  It is passed in here just so that we can
 * log it with the packet transmission message.
 * Update the flow's finish time to the current global
 * minimum finish time passed in as "currFi".
 *
 * Return 0 if there's more of the image to send.
 * Return 1 if we've finished sending image.
 * Terminate process upon encountering any error.
 * Doesn't otherwise modify anything.
*/
int Flow::
sendpkt(int sd, int fd, float currFi)
{
  int bytes;

  // update the flow's finish time to the current
  // global minimum finish time
  Fi = currFi;

  /* 
   * Send one segment of data of size segsize at each iteration.
   * Point the second entry of the iovec to the correct offset
   * from the start of the image.  Update the sequence number
   * and size fields of the ihdr_t header to reflect the byte
   * offset and size of the current chunk of data.  Send
   * the segment off by calling sendmsg().
   */
  iov[1].iov_base = ip+snd_next;
  iov[1].iov_len = segsize;
  hdr.ih_seqn = htonl(snd_next);
  hdr.ih_size = htons(segsize);
  
  bytes = sendmsg(sd, &msg, 0);
  net_assert((bytes < 0), "imgdb_sendimage: sendmsg");
  net_assert((bytes != (int)(segsize+sizeof(ihdr_t))), "Flow::sendpkt: sendmsg bytes");
  
  fprintf(stderr, "Flow::sendpkt: flow %d: sent offset 0x%x, Fi: %.6f, %d bytes\n", fd, snd_next, Fi, segsize);
  snd_next += segsize;
  
  if ((int) snd_next < img_size) {
    return 0;
  } else {
    return 1;
  }
}

/*
 * WFQ::addflow
 * Wait for an iqry_t packet from client and set up a flow
 * once an iqry_t packet arrives.  If gated transmission has
 * started, do a non-blocking wait and return to caller if no
 * iqry_t packet is waiting.  Otherwise, block for each expected
 * flow, up to minflow number of flows.
 *
 * Once a flow arrives, prepare an imsg_t response packet, with
 * the default response being resources unavailable (NETIMG_FULL).
 *
 * Look for the first empty slot in flow[] to hold the new flow.
 * If an empty slot exists, check that we haven't hit linkrate
 * capacity.  We can only add a flow if there's enough linkrate
 * left over to accommodate the flow's reserved rate.
 * Once a flow is admitted, increment flow count and total reserved
 * rate, then call Flow::init() to initialize the flow.  If the 
 * queried image is found, imgdb_init(), called by Flow::init(),
 * would update the imsg response packet accordingly.
 * 
 * If minflow number of flows have arrived or total reserved rate is
 * at link capacity, toggle the "started" member variable to on (1)
 * and reset the start time of each flow to the current wall clock
 * time.
 * 
 * Finally send back the imsg_t response packet.
*/
int WFQ::
addflow(int sd)
{
  int i, bytes;
  iqry_t iqry;
  imsg_t imsg;
  socklen_t len;
  struct sockaddr_in client;
  
  len = sizeof(struct sockaddr_in);
  bytes = recvfrom(sd, &iqry, sizeof(iqry_t), started ? MSG_DONTWAIT : 0,
                   (struct sockaddr *) &client, &len);
  if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    return (0);
  }
  
  if (bytes == sizeof(iqry_t) &&
      (iqry.iq_vers == NETIMG_VERS && iqry.iq_type == NETIMG_SYN)) {
    
    net_assert((strlen((char *) &iqry.iq_name) >= NETIMG_MAXFNAME),
               "imgdb_recvquery: iqry name length");
    
    iqry.iq_mss = (unsigned short) ntohs(iqry.iq_mss);
    iqry.iq_frate = (unsigned short) ntohs(iqry.iq_frate);
    
    /* default to: not enough resources */
    imsg.im_vers = NETIMG_VERS;
    imsg.im_type = NETIMG_DIM;
    imsg.im_found = NETIMG_FULL;

    /* 
     * Task 1: look for the first empty slot in flow[] to hold the new
     * flow.  If an empty slot exists, check that we haven't hit
     * linkrate capacity.  We can only add a flow if there's enough
     * linkrate left over to accommodate the flow's reserved rate.
     * Once a flow is admitted, increment flow count and total
     * reserved rate, then call Flow::init() to initialize the flow.
     * If the queried image is found, imgdb_init(), called by
     * Flow::init(), would update the imsg response packet
     * accordingly.
    */
    /* Task 1: YOUR CODE HERE */
    for (i = 0; i < NETIMG_MAXFLOW; i++) {
      if (!flow[i].in_use && iqry.iq_frate < )




    /* Toggle the "started" member variable to on (1) if minflow number
     * of flows have arrived or total reserved rate is at link capacity
     * and set the start time of each flow to the current wall clock time.
    */
    if (!started && (nflow >= minflow || rsvdrate >= linkrate)) {
      started = 1;
      for (i = 0; i < NETIMG_MAXFLOW; i++) {
        if (flow[i].in_use) {
          gettimeofday(&flow[i].start, NULL);
        }
      }
    }

  } else {
    net_assert(1, "imgdb_recvquery: bad SYN packet");
  }
  
  bytes = sendto(sd, (char *) &imsg, sizeof(imsg_t), 0,
                 (struct sockaddr *) &client, sizeof(struct sockaddr_in));
  net_assert((bytes != sizeof(imsg_t)), "WFQ::addflow: sendto pkt");
  
  return (1);
}

/*
 * WFQ::sendpkt:
 *
 * Task 2: First compute the next finish time of each flow given
 * current total reserved rate of the system by calling Flow::nextFi()
 * on each flow.
 *
 * Task 3: Determine the minimum finish time and the flow with the
 * minimum finish time. Set the current global minimum finish time to
 * be this minimum finish time.
 *
 * Send out the packet with the minimum finish time by calling
 * Flow::sendpkt() on the flow.  Save the return value of Flow::sendpkt()
 * in the local "done" variable.  If the flow is finished sending,
 * Flow::sendpkt() will return 1.
 *
 * Task 4: When done sending, remove flow from flow[] by calling
 * Flow::done().  Deduct the flow's reserved rate (returned by
 * Flow::done()) from the total reserved rate, and decrement the flow
 * count.
 */
void WFQ::
sendpkt(int sd)
{
  int fd;
  struct timeval end;
  int secs, usecs;
  int done = 0;

  /* Task 3: YOUR CODE HERE */

  if (done) {
    /* Task 4: When done sending, remove flow from flow[] by calling
     * Flow::done().  Deduct the flow's reserved rate (returned by
     * Flow::done()) from the total reserved rate, and decrement the
     * flow count.
    */
    /* Task 4: YOUR CODE HERE */

    if (nflow <= 0) {
      started = 0;
    }

    gettimeofday(&end, NULL);
    /* compute elapsed time */
    usecs = USECSPERSEC-flow[fd].start.tv_usec+end.tv_usec;
    secs = end.tv_sec - flow[fd].start.tv_sec - 1;
    if (usecs > USECSPERSEC) {
      secs++;
      usecs -= USECSPERSEC;
    }
    
    fprintf(stderr, "WFQ::sendpkt: flow %d done, elapsed time (m:s:ms:us): %d:%d:%d:%d, link reserved rate: %d\n", fd, secs/60, secs%60, usecs/1000, usecs%1000, rsvdrate);
  }

  return;
}

int
main(int argc, char *argv[])
{ 
  int sd;
  unsigned short linkrate;
  short minflow;

#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);    /* don't die if peer is dead */
#endif

  // parse args, see the comments for imgdb_args()
  if (imgdb_args(argc, argv, &linkrate, &minflow)) {
    imgdb_usage(argv[0]);
  }

  WFQ wfq(linkrate, minflow);

  srandom(48914);
  sd = imgdb_sockinit();
  
  while (1) {
    // continue to add flow while there are incoming requests
    while(wfq.addflow(sd));

    wfq.sendpkt(sd);
  }
    
#ifdef _WIN32
  WSACleanup();
#endif
  exit(0);
}
