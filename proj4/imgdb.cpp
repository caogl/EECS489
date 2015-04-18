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
#include <math.h>
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


void Flow::
init(int sd, struct sockaddr_in *qhost, iqry_t *iqry, imsg_t *imsg, float currFi, unsigned short linkrateFIFO)
{
  int err, usable;
  socklen_t optlen;
  double imgdsize;

  imsg->im_type = readimg(iqry->iq_name, 1);
  
  if (imsg->im_type == NETIMG_FOUND) 
  {
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

    // if the flow is FIFO with TBF
    if(!frate)
    {
      frate=linkrateFIFO;
      bsize=ceil((double)(mss-sizeof(ihdr_t))*(iqry->iq_rwnd)/IMGDB_BPTOK);
      trate=(frate*1024)/(8*IMGDB_BPTOK);
      bavail=0; // the available token number in the bucket
      bpseg=(float)(mss-sizeof(ihdr_t))/IMGDB_BPTOK; // the token number needed per segment, excluding headers
    }
    
    /* for non-gated flow starts */
    gettimeofday(&start, NULL);
  }

  return;
}


float Flow::
nextFi(float multiplier, bool TBF)
{
  /* size of this segment */
  segsize = imgsize - snd_next;
  segsize = segsize > datasize ? datasize : segsize;

  if(!TBF) // if WFQ flow
  {
    duration=segsize/(128*frate*multiplier);
  }
  else
  {
    if(bavail<bpseg)
    {
      incre=bpseg-bavail+((float)random()/INT_MAX)*bsize;
      //bavail=min(bsize, bavail+incre);
      duration=incre/trate;
    }
    else
    {
      duration=0;
    }
  }  

  return (duration+Fi);  
}


int Flow::
sendpkt(int sd, int fd, float currFi)
{
  int bytes;

  // update the flow's finish time to the current
  // global minimum finish time
  Fi = currFi;

  iov[1].iov_base = ip+snd_next;
  iov[1].iov_len = segsize;
  hdr.ih_seqn = htonl(snd_next);
  hdr.ih_size = htons(segsize);
  
  usleep(1000000*duration);  

  if(fd==-1)
  {    
    if(bavail<bpseg)
    {
      bavail=min(bsize, bavail+incre);
    }
    bavail-=bpseg;
  }

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
 * Returns 0 on success or 1 on failure.
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

  int linkrate = IMGDB_LRATE; // total link rate
  float frate = IMGDB_FRATE; // fraction of link for WFQ
  minflow = IMGDB_MINFLOW;

  while ((c = getopt(argc, argv, "l:g:f:")) != EOF) {
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
    case 'f':
      frate = atof(optarg);
      if (frate<0 || frate>1)
        return (1);
      break;
    default:
      return(1);
      break;
    }
  }

  linkrateWFQ=frate*linkrate;  
  linkrateFIFO=(1-frate)*linkrate;
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
    fprintf(stderr, "Usage: %s [ -l <linkrate [1, 10 Mbps]> -g <minflow> -f <frateWFQ>]\n", argv[0]); 
    exit(1);
  }
  
  srandom(NETIMG_SEED+linkrateWFQ+linkrateFIFO+minflow);
}


char imgdb::
recvqry(int sd, struct sockaddr_in *qhost, iqry_t *iqry)
{
  int bytes;  // stores the return value of recvfrom()
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


int imgdb::
handleqry()
{
  int i;
  iqry_t iqry;
  imsg_t imsg;
  struct sockaddr_in qhost;
  
  imsg.im_type = recvqry(sd, &qhost, &iqry);
  if(!imsg.im_type) 
  {
    iqry.iq_mss = (unsigned short) ntohs(iqry.iq_mss);
    iqry.iq_frate = (unsigned short) ntohs(iqry.iq_frate);
    
    if(!iqry.iq_frate) // FIFO client
    {
      // if already an active FIFO flow, do not accept new FIFO client flow
      if(FIFOQ.in_use) 
      {
        imsg.im_type=NETIMG_EFULL;
        sendimsg(sd, &qhost, &imsg);
        return(1);        
      }

      FIFOQ.init(sd, &qhost, &iqry, &imsg, currFi, linkrateFIFO);
      if(imsg.im_type==NETIMG_NFOUND)
      {   
        sendimsg(sd, &qhost, &imsg);
        return(1);
      }
      nflow++;
      fprintf(stderr, "imgdb:handleqry: flow %d added, flow rate: %d, reserved link rate: %d\n", -1, linkrateFIFO, linkrateFIFO);

    }    
    else
    {
      for(i = 0; i<IMGDB_MAXFLOW; i++)
      {
        if(!WFQ[i].in_use)
        {
          WFQ[i].init(sd, &qhost, &iqry, &imsg, currFi, 0);
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
      if(i==IMGDB_MAXFLOW)
      {        
        imsg.im_type=NETIMG_EFULL;
        sendimsg(sd, &qhost, &imsg);
        return(1);     
      }
    }

    if(!started && nflow>=minflow)
    {
      started = 1;
      for (i = 0; i <IMGDB_MAXFLOW; i++) 
        if(WFQ[i].in_use)
          gettimeofday(&WFQ[i].start, NULL);
      gettimeofday(&FIFOQ.start, NULL);
    }
  }
 
  if(imsg.im_type != NETIMG_EAGAIN)
  {
    sendimsg(sd, &qhost, &imsg);
    return(1);
  }

  return(0);
}


void imgdb::
sendpkt()
{
  int fd = IMGDB_MAXFLOW;
  struct timeval end;
  int secs, usecs;
  int done = 0;

  /* pick next client to send packet and send with sleep*/
  float temp;
  for(int i=0; i<IMGDB_MAXFLOW; i++)
  {
    temp=WFQ[i].nextFi((float)linkrateWFQ/rsvdrate, 0);
    if(WFQ[i].in_use && (fd==IMGDB_MAXFLOW || currFi>temp))
    {
      fd=i;
      currFi=temp;
    }
  }

  temp=FIFOQ.nextFi((float)linkrateWFQ/rsvdrate, 1);
  if(FIFOQ.in_use && (fd==IMGDB_MAXFLOW || currFi>temp))
  {
    fd=-1;
    currFi=temp;
    done=FIFOQ.sendpkt(sd, fd, currFi);
  }
  else
    done=WFQ[fd].sendpkt(sd, fd, currFi);

  if(done) 
  {
    gettimeofday(&end, NULL);
    /* compute elapsed time */
    if(fd!=-1)
    {
      usecs = USECSPERSEC-WFQ[fd].start.tv_usec+end.tv_usec;
      secs = end.tv_sec - WFQ[fd].start.tv_sec - 1;
    }
    else
    {
      usecs = USECSPERSEC-FIFOQ.start.tv_usec+end.tv_usec;
      secs = end.tv_sec-FIFOQ.start.tv_sec - 1;
    }

    if (usecs > USECSPERSEC) {
      secs++;
      usecs -= USECSPERSEC;
    }
    
    if(fd==-1)
      fprintf(stderr, "imgdb::sendpkt: flow %d done, elapsed time (m:s:ms:us): %d:%d:%d:%d, reserved link rate: %d\n", fd, secs/60, secs%60, usecs/1000, usecs%1000, linkrateFIFO);
    else
      fprintf(stderr, "imgdb::sendpkt: flow %d done, elapsed time (m:s:ms:us): %d:%d:%d:%d, reserved link rate: %d\n", fd, secs/60, secs%60, usecs/1000, usecs%1000, (int)(WFQ[fd].frate*((float)linkrateWFQ/rsvdrate)));

    if(fd!=-1)
      rsvdrate-=WFQ[fd].done();
    else
      FIFOQ.done();
    nflow--;

    if (nflow <= 0) 
      started = 0;
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
