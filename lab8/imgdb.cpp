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
#include "socks.h"
#include "netimg.h"
#include "imgdb.h"

#define USECSPERSEC 1000000

/*
 * Flow::readimg: load TGA image from file "imgname" to Flow::curimg.
 * "imgname" must point to valid memory allocated by caller.
 * Terminate process on encountering any error.
 * Returns NETIMG_FOUND if "imgname" found, else returns NETIMG_NFOUND.
 */
char Flow::
readimg(char *imgname, int verbose)
{
  string pathname=IMGDB_FOLDER;

  if (!imgname || !imgname[0]) {
    return(NETIMG_ENAME);
  }
  
  curimg.LoadFromFile(pathname+IMGDB_DIRSEP+imgname);

  if (!curimg.IsLoaded()) {
    return(NETIMG_NFOUND);
  }

  if (verbose) {
    cerr << "Image: " << endl;
    cerr << "       Type = " << LImageTypeString[curimg.GetImageType()] 
         << " (" << curimg.GetImageType() << ")" << endl;
    cerr << "      Width = " << curimg.GetImageWidth() << endl;
    cerr << "     Height = " << curimg.GetImageHeight() << endl;
    cerr << "Pixel depth = " << curimg.GetPixelDepth() << endl;
    cerr << "Alpha depth = " << curimg.GetAlphaDepth() << endl;
    cerr << "RL encoding = " << (((int) curimg.GetImageType()) > 8) << endl;
    /* use curimg.GetPixels()  to obtain the pixel array */
  }
  
  return(NETIMG_FOUND);
}

/*
 * Flow::marshall_imsg: Initialize *imsg with image's specifics.
 * Upon return, the *imsg fields are in host-byte order.
 * Return value is the size of the image in bytes.
 *
 * Terminate process on encountering any error.
 */
double Flow::
marshall_imsg(imsg_t *imsg)
{
  int alpha, greyscale;

  imsg->im_depth = (unsigned char)(curimg.GetPixelDepth()/8);
  imsg->im_width = curimg.GetImageWidth();
  imsg->im_height = curimg.GetImageHeight();
  alpha = curimg.GetAlphaDepth();
  greyscale = curimg.GetImageType();
  greyscale = (greyscale == 3 || greyscale == 11);
  if (greyscale) {
    imsg->im_format = alpha ? GL_LUMINANCE_ALPHA : GL_LUMINANCE;
  } else {
    imsg->im_format = alpha ? GL_RGBA : GL_RGB;
  }

  return((double) (imsg->im_width*imsg->im_height*imsg->im_depth));
}

/*
 * Flow::init
 * initialize flow by:
 * - indicating that flow is "in_use"
 * - loading and initializing image by calling Flow::readimg()
 *   and Flow::marshall_imsg(), update imsg->im_type accordingly.
 *   Also initialize member variables "ip" and "snd_next"
 * - initialize "mss" and "datasize", ensure that socket send
 *   buffer is at least mss size
 * - set flow's reserved rate "frate" to client's specification
 * - initial flow finish time is current global minimum finish time
 * - populate a struct msghdr for sending chunks of image
 * - save current system time as flow start time.  For gated start,
 *   this may be updated later with actual start time.
 * Assume that all fields in *iqry are already in host-byte order.
 * Leave all fields in *imsg in host-byte order also.
*/
void Flow::
init(int sd, struct sockaddr_in *qhost, iqry_t *iqry,
     imsg_t *imsg, float currFi)
{
  int err, usable;
  socklen_t optlen;
  double imgdsize;

  imsg->im_type = readimg(iqry->iq_name, 1);
  
  if (imsg->im_type == NETIMG_FOUND) {

    // flow is in use
    in_use = 1;
    
    // initialize imsg
    imgdsize = marshall_imsg(imsg);
    net_assert((imgdsize > (double) LONG_MAX),
               "imgdb::sendimg: image too big");
    imgsize = (long)imgdsize;

    // ip points to the start of byte buffer holding image
    ip = (char *) curimg.GetPixels();
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
    datasize = mss - sizeof(ihdr_t) - NETIMG_UDPIP;

    // flow's reserved rate as specified by client
    // flow's initial finish time is the current global minimum finish time
    frate = iqry->iq_frate;
    Fi = currFi;
    
    /* 
     * Populate a struct msghdr with information of the destination client,
     * a pointer to a struct iovec array.  The iovec array should be of size
     * NETIMG_NUMIOV.  The first entry of the iovec should be initialized
     * to point to an ihdr_t, which should be re-used for each chunk of data
     * to be sent.
     */
    client = *qhost;
    msg.msg_name = &client;
    msg.msg_namelen = sizeof(sockaddr_in);
    msg.msg_iov = iov;
    msg.msg_iovlen = NETIMG_NUMIOV;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;
    
    hdr.ih_vers = NETIMG_VERS;
    hdr.ih_type = NETIMG_DATA;
    iov[0].iov_base = &hdr;
    iov[0].iov_len = sizeof(ihdr_t);
    
    /* for non-gated flow starts */
    gettimeofday(&start, NULL);
  }

  return;
}

/*
 * Flow::nextFi: compute the flow's next finish time
 * from the size of the current segment, the flow's
 * reserved rate, and the multiplier passed in.
 * The multiplier is linkrate/total_reserved_rate.
 * To avoid unnecessary arithmetic, you can assume that
 * the Fi's are multiplied by 128 (or 1024/8) so you
 * can keep the segment size in bytes instead of Kbits.
 * Since we're comparing relative finish times (Fi's),
 * the unit doesn't really matter, as long as the ordering
 * is correct.
*/
float Flow::
nextFi(float multiplier)
{
  /* size of this segment */
  segsize = imgsize - snd_next;
  segsize = segsize > datasize ? datasize : segsize;

  /* Task 2: YOUR CODE HERE */
  /* Replace the following return statement with your
     computation of the next finish time as indicated above
     and return the result instead. */

  //float duration = segsize/((1024/8)*frate*multiplier);
  float duration=segsize/(128*frate*multiplier);
  return(duration+Fi);    
}

/*
 * Flow::sendpkt:
 * Send the image contained in *image to the client
 * pointed to by *client. Send the image in
 * chunks of segsize, not to exceed mss, instead of
 * as one single image.
 * The argument "sd" is the socket to send packet out of.
 * The argument "fd" is the array index this flow occupies
 * on the flow table.  It is passed in here just so that we can
 * log it with the packet transmission message.
 * Update the flow's finish time to the current global
 * minimum finish time passed in as "currFi".
 *
 * Return 0 if there's more of the image to send.
 * Return 1 if we've finished sending image.
 * Terminate process upon encountering any error.
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
  
  fprintf(stderr, "Flow::sendpkt: flow %d: sent offset 0x%x, Fi: %.6f, %d bytes\n",
          fd, snd_next, Fi, segsize);
  snd_next += segsize;
  
  if ((int) snd_next < imgsize) {
    return 0;
  } else {
    return 1;
  }
}

/*
 * imgdb_args: parses command line args.
 *
 * Returns 0 on success or 1 on failure.
 *
 * Nothing else is modified.
 */
int imgdb::
args(int argc, char *argv[])
{
  char c;
  extern char *optarg;
  int arg;

  if (argc < 1) {
    return (1);
  }

  linkrate = IMGDB_LRATE;
  minflow = IMGDB_MINFLOW;

  while ((c = getopt(argc, argv, "l:g:")) != EOF) {
    switch (c) {
    case 'l':
      arg = atoi(optarg);
      if (arg < IMGDB_MINLRATE || arg > IMGDB_MAXLRATE) {
        return(1);
      }
      linkrate = (unsigned short) arg*1024;  // in Kbps
      break;
    case 'g':
      arg = atoi(optarg);
      if (arg < IMGDB_MINFLOW || arg > IMGDB_MAXFLOW) {
        return(1);
      }
      minflow = (short) arg;
      break;
    default:
      return(1);
      break;
    }
  }

  return (0);
}

/*
 * imgdb: default constructor
*/
imgdb::
imgdb(int argc, char *argv[])
{ 
  started=0; nflow=0; rsvdrate=0; currFi=0.0; 

  sd = socks_servinit((char *) "imgdb", &self, sname);

  // parse args, see the comments for imgdb::args()
  if (args(argc, argv)) {
    fprintf(stderr, "Usage: %s [ -l <linkrate [1, 10 Mbps]> -g <minflow> ]\n", argv[0]); 
    exit(1);
  }
  
  srandom(NETIMG_SEED+linkrate+minflow);
}

/* 
 * recvqry: receives an iqry_t packet and stores the client's address
 * and port number in the qhost variable.  Checks that
 * the incoming iqry_t packet is of version NETIMG_VERS and of type
 * NETIMG_SYNQRY.
 *
 * If error encountered when receiving packet or if packet is of the
 * wrong version or type returns appropriate NETIMG error code.  The
 * receive is done non-blocking.  If the socket buffer is empty,
 * return NETIMG_EAGAIN. Otherwise returns 0.
 *
 * Nothing else is modified.
*/
char imgdb::
recvqry(int sd, struct sockaddr_in *qhost, iqry_t *iqry)
{
  int bytes;  // stores the return value of recvfrom()

  /*
   * Call recvfrom() to receive the iqry_t packet from
   * qhost.  Store the client's address and port number in
   * qhost and store the return value of
   * recvfrom() in local variable "bytes".
  */
  socklen_t len;
  
  len = sizeof(struct sockaddr_in);
  bytes = recvfrom(sd, iqry, sizeof(iqry_t), started ? MSG_DONTWAIT: 0,
                   (struct sockaddr *) qhost, &len);
  if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    return (NETIMG_EAGAIN);
  }
  
  if (bytes != sizeof(iqry_t)) {
    return (NETIMG_ESIZE);
  }
  if (iqry->iq_vers != NETIMG_VERS) {
    return(NETIMG_EVERS);
  }
  if (iqry->iq_type != NETIMG_SYNQRY) {
    return(NETIMG_ETYPE);
  }
  if (strlen((char *) iqry->iq_name) >= NETIMG_MAXFNAME) {
    return(NETIMG_ENAME);
  }

  return(0);
}

/* imgdb::sendimsg:
 *
 * Prepares imsg for transmission: fills in im_vers and converts
 * integers to network byte order before transmission.  Note that
 * im_type is set by the caller and should not be modified.  Sends the
 * imsg packet to qhost using sendto().  Returns the return
 * value of sendto().  Nothing else is modified.
*/
void imgdb::
sendimsg(int sd, struct sockaddr_in *qhost, imsg_t *imsg)
{
  int bytes;

  imsg->im_vers = NETIMG_VERS;
  imsg->im_width = htons(imsg->im_width);
  imsg->im_height = htons(imsg->im_height);
  imsg->im_format = htons(imsg->im_format);

  // send the imsg packet to client
  bytes = sendto(sd, (char *) imsg, sizeof(imsg_t), 0, (struct sockaddr *) qhost,
                 sizeof(struct sockaddr_in));
  net_assert((bytes != sizeof(imsg_t)), "imgdb::sendimsg: sendto");

  return;
}

/*
 * imgdb::handleqry
 * Check for an iqry_t packet from client and set up a flow
 * if an iqry_t packet arrives.
 *
 * Once a flow arrives, look for the first empty slot in flow[] to
 * hold the new flow.  If an empty slot exists, check that we haven't
 * hit linkrate capacity.  We can only add a flow if there's enough
 * linkrate left over to accommodate the flow's reserved rate.  If a flow
 * cannot be admitted due to capacity limit, return an imsg_t packet
 * with im_type set to NETIMG_EFULL.
 *
 * Once a flow is admitted, increment flow count and total reserved
 * rate, then call Flow::init() to initialize the flow.  If the
 * queried image is found, Flow::init(), would update the imsg
 * response packet accordingly.
 * 
 * If minflow number of flows have arrived or total reserved rate is
 * at link capacity, toggle the "started" member variable to on (1)
 * and reset the start time of each flow to the current wall clock
 * time.
 * 
 * Finally send back the imsg_t response packet.
*/
int imgdb::
handleqry()
{
  int i;
  iqry_t iqry;
  imsg_t imsg;
  struct sockaddr_in qhost;
  
  imsg.im_type = recvqry(sd, &qhost, &iqry);
  if (!imsg.im_type) {
    
    iqry.iq_mss = (unsigned short) ntohs(iqry.iq_mss);
    iqry.iq_frate = (unsigned short) ntohs(iqry.iq_frate);
    
    /* 
     * Task 1: look for the first empty slot in flow[] to hold the new
     * flow.  If an empty slot exists, check that we haven't hit
     * linkrate capacity.  We can only add a flow if there's enough
     * linkrate left over to accommodate the flow's reserved rate.
     * Once a flow is admitted, increment flow count and total
     * reserved rate, then call Flow::init() to initialize the flow.
     * Flow::init() will update the imsg response packet accordingly.
     * If a flow cannot be admitted due to capacity limit, return an
     * imsg_t packet with im_type set to NETIMG_EFULL.
     */
    /* Task 1: YOUR CODE HERE */
    for(i = 0; i<IMGDB_MAXFLOW; i++)
    {
      if(!flow[i].in_use && iqry.iq_frate + rsvdrate <= linkrate)
      {
        flow[i].init(sd, &qhost, &iqry, &imsg, currFi);
        if(imsg.im_type==NETIMG_NFOUND)
        {   
          sendimsg(sd, &qhost, &imsg);
          return(1);
        }

        nflow++;
        rsvdrate += iqry.iq_frate;
        fprintf(stderr, "imgdb:handleqry: flow %d added, flow rate: %d, reserved link rate: %d\n", i, iqry.iq_frate, rsvdrate);
        break;
      }
    }

    /* Toggle the "started" member variable to on (1) if minflow number
     * of flows have arrived or total reserved rate is at link capacity
     * and set the start time of each flow to the current wall clock time.
     */
    if (!started && (nflow >= minflow || rsvdrate >= linkrate))
    {
      started = 1;
      for (i = 0; i <IMGDB_MAXFLOW; i++) 
      {
        if(flow[i].in_use)
          gettimeofday(&flow[i].start, NULL);
      }
    }

  }
 
  if(imsg.im_type != NETIMG_EAGAIN)
  {
    sendimsg(sd, &qhost, &imsg);
    return(1);
  }

  return(0);
}

/*
 * imgdb::sendpkt:
 *
 * First compute the next finish time of each flow given current total
 * reserved rate of the system by calling Flow::nextFi() (Task 2) on
 * each flow.
 *
 * Task 3: Determine the minimum finish time and which flow has this
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
void imgdb::
sendpkt()
{
  int fd = IMGDB_MAXFLOW;
  struct timeval end;
  int secs, usecs;
  int done = 0;

  /* Task 3: YOUR CODE HERE */
  for (int i = 0; i <IMGDB_MAXFLOW; i++)
  {
    if(flow[i].in_use && (fd==IMGDB_MAXFLOW || currFi>flow[i].nextFi((float)linkrate/rsvdrate)))
    {
      fd=i;
      currFi=flow[i].nextFi((float)linkrate/rsvdrate);
    }
  }
  done = flow[fd].sendpkt(sd, fd, currFi);

  if (done) {

    /* Task 4: When done sending, remove flow from flow[] by calling
     * Flow::done().  Deduct the flow's reserved rate (returned by
     * Flow::done()) from the total reserved rate, and decrement the
     * flow count.
    */
    /* Task 4: YOUR CODE HERE */
    rsvdrate-=flow[fd].done();
    nflow--;

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
    
    fprintf(stderr,
            "imgdb::sendpkt: flow %d done, elapsed time (m:s:ms:us): %d:%d:%d:%d, reserved link rate: %d\n",
            fd, secs/60, secs%60, usecs/1000, usecs%1000, rsvdrate);
  }

  return;
}

int
main(int argc, char *argv[])
{ 
  socks_init();
  imgdb imgdb(argc, argv);

  while (1) {
    // continue to add flow while there are incoming requests
    while(imgdb.handleqry());

    imgdb.sendpkt();
  }
    
#ifdef _WIN32
  WSACleanup();
#endif
  exit(0);
}
