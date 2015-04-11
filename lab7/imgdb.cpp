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
#include <math.h>          // ceil(), floor()
#include <assert.h>        // assert()
#include <limits.h>        // LONG_MAX, INT_MAX
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
 * imgdb::args: parses command line args.
 *
 * Returns 0 on success or 1 on failure.
 *
 * Nothing else is modified.
int imgdb::
args(int argc, char *argv[])
{
  char c;
  extern char *optarg;

  if (argc < 1) {
    return (1);
  }
  
  while ((c = getopt(argc, argv, "")) != EOF) {
    switch (c) {
    default:
      return(1);
      break;
    }
  }

  srandom(NETIMG_SEED+IMGDB_BPTOK+NETIMG_FRATE);

  return (0);
}
*/

/*
 * readimg: load TGA image from file "imgname" to curimg.
 * "imgname" must point to valid memory allocated by caller.
 * Terminate process on encountering any error.
 * Returns NETIMG_FOUND if "imgname" found, else returns NETIMG_NFOUND.
 */
char imgdb::
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
 * marshall_imsg: Initialize *imsg with image's specifics.
 * Upon return, the *imsg fields are in host-byte order.
 * Return value is the size of the image in bytes.
 *
 * Terminate process on encountering any error.
 */
double imgdb::
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
 * recvqry: receives an iqry_t packet and stores the client's address
 * and port number in the imgdb::client member variable.  Checks that
 * the incoming iqry_t packet is of version NETIMG_VERS and of type
 * NETIMG_SYNQRY.
 *
 * If error encountered when receiving packet or if packet is of the
 * wrong version or type returns appropriate NETIMG error code.
 * Otherwise returns 0.
 *
 * Nothing else is modified.
*/
char imgdb::
recvqry(int sd, iqry_t *iqry)
{
  int bytes;  // stores the return value of recvfrom()

  /*
   * Call recvfrom() to receive the iqry_t packet from
   * client.  Store the client's address and port number in the
   * imgdb::client member variable and store the return value of
   * recvfrom() in local variable "bytes".
  */
  socklen_t len;
  
  len = sizeof(struct sockaddr_in);
  bytes = recvfrom(sd, iqry, sizeof(iqry_t), 0,
                   (struct sockaddr *) &client, &len);
  net_assert((bytes <= 0), "imgdb::recvqry: recvfrom");
  
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
  
/* 
 * sendpkt: sends the provided "pkt" of size "size"
 * to imgdb::client using sendto().
 *
 * Returns the return value of sendto().
 *
 * Nothing else is modified.
*/
int imgdb::
sendpkt(int sd, char *pkt, int size)
{
  int bytes;
    
  bytes = sendto(sd, pkt, size, 0, (struct sockaddr *) &client,
                 sizeof(struct sockaddr_in));

  return(bytes);
}

/*
 * sendimg:
 * Send the image contained in *image to the client pointed to by
 * *client. Send the image in chunks of segsize, not to exceed mss,
 * instead of as one single image. Each segment can be sent only if
 * there's enough tokens to cover it.  The token bucket filter
 * parameters are "imgdb::bsize" tokens and "imgdb::trate" tokens/sec.
 *
 * Terminate process upon encountering any error.
 * Doesn't otherwise modify anything.
*/
void imgdb::
sendimg(int sd, imsg_t *imsg, char *image, long imgsize, int numseg)
{
  int bytes, segsize, datasize;
  char *ip;
  long left;
  unsigned int snd_next=0;
  int err, offered, usable;
  socklen_t optlen;
  struct msghdr msg;
  struct iovec iov[NETIMG_NUMIOV];
  ihdr_t hdr;


  /* Prepare imsg for transmission: fill in im_vers and convert
   * integers to network byte order before transmission.  Note that
   * im_type is set by the caller and should not be modified.  Send
   * the imsg packet by calling imgdb::sendpkt().
   */
  imsg->im_vers = NETIMG_VERS;
  imsg->im_width = htons(imsg->im_width);
  imsg->im_height = htons(imsg->im_height);
  imsg->im_format = htons(imsg->im_format);

  // send the imsg packet to client by calling sendpkt().
  bytes = sendpkt(sd, (char *) imsg, sizeof(imsg_t));
  net_assert((bytes != sizeof(imsg_t)), "imgdb::sendimg: send imsg");

  if (image) {
    ip = image; /* ip points to the start of image byte buffer */
    datasize = mss - sizeof(ihdr_t) - NETIMG_UDPIP;

    /* Lab7 Task 2:
     * Initialize any token bucket filter variables you may have here.
    */
    /* Lab7 YOUR CODE HERE */
    
    /* 
     * make sure that the send buffer is of size at least mss.
     */
    offered = mss;
    optlen = sizeof(int);
    err = getsockopt(sd, SOL_SOCKET, SO_SNDBUF, &usable, &optlen);
    if (usable < offered) {
      err = setsockopt(sd, SOL_SOCKET, SO_SNDBUF, &offered,
                       sizeof(int));
      net_assert((err < 0), "imgdb::sendimg: setsockopt SNDBUF");
    }

    /* 
     * Populate a struct msghdr with information of the destination
     * client, a pointer to a struct iovec array.  The iovec array
     * should be of size NETIMG_NUMIOV.  The first entry of the iovec
     * should be initialized to point to an ihdr_t, which should be
     * re-used for each chunk of data to be sent.
     */
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

    do {
      /* size of this segment */
      left = imgsize - snd_next;
      segsize = datasize > left ? left : datasize;
      
      /*
       * Lab7 Task 2:
       *
       * If there isn't enough token left to send one segment of
       * data, usleep() until *at least* as many tokens as needed to send a
       * segment, plus a random multiple of bsize amount of data would
       * have been generated.  To determine the "random multiple" you may
       * use the code ((float) random()/INT_MAX) to generate a random number
       * in the range [0.0, 1.0].  Your token generation rate is a given by
       * the "trate" formal argument of this function.  The unit of "trate"
       * is tokens/sec.
       *
       * Enforce that your token bucket size is not larger than "bsize"
       * tokens.  The variable "bsize" is a formal argument of this
       * function.
       *
       * Also decrement token bucket size when a segment is sent.
       */
      /* Lab7 YOUR CODE HERE */

      /* 
       * With sufficient tokens in the token bucket, send one segment
       * of data of size segsize at each iteration.  Point the second
       * entry of the iovec to the correct offset from the start of
       * the image.  Update the sequence number and size fields of the
       * ihdr_t header to reflect the byte offset and size of the
       * current chunk of data.  Send the segment off by calling
       * sendmsg().
       */
      iov[1].iov_base = ip+snd_next;
      iov[1].iov_len = segsize;
      hdr.ih_seqn = htonl(snd_next);
      hdr.ih_size = htons(segsize);
        
      bytes = sendmsg(sd, &msg, 0);
      net_assert((bytes < 0), "imgdb::sendimg: sendmsg");
      net_assert((bytes != (int)(segsize+sizeof(ihdr_t))),
                 "imgdb::sendimg: sendmsg bytes");
        
      fprintf(stderr, "imgdb::sendimg: sent offset 0x%x, %d bytes\n",
              snd_next, segsize);
        
      snd_next += segsize;
    } while ((int) snd_next < imgsize);
  }
    
  return;
}

/*
 * handleqry: accept connection, then receive a query packet, search
 * for the queried image, and reply to client.
 */
void imgdb::
handleqry()
{
  iqry_t iqry;
  imsg_t imsg;
  double imgdsize;
  struct timeval start, end;
  int usecs, secs;

  imsg.im_type = recvqry(sd, &iqry);
  if (imsg.im_type) { // error
    sendimg(sd, &imsg, NULL, 0, 0);
  } else {
    
    imsg.im_type = readimg(iqry.iq_name, 1);
    
    if (imsg.im_type == NETIMG_FOUND) {

      mss = (unsigned short) ntohs(iqry.iq_mss);
      rwnd = iqry.iq_rwnd;
      frate = (unsigned short) ntohs(iqry.iq_frate);
      net_assert(!(mss && rwnd && frate),
                 "imgdb::sendimg: mss, rwnd, and frate cannot be zero");
      /*
       * Lab7 Task 1:
       * 
       * Compute the token bucket size such that it is at least big enough
       * to hold as many tokens as needed to send one "mss" (excluding
       * headers).  If "rwnd" is greater than 1, token bucket size should be
       * set to accommodate as many segments (excluding headers).
       * Store the token bucket size in imgdb::bsize.  The unit
       * of bsize is "number of tokens".
       *
       * Compute the token generation rate based on target flow send rate,
       * stored in imgdb::frate.  The variable "frate" is
       * in Kbps.  You need to convert "frate" to the appropriate token
       * generation rate, in unit of tokens/sec.  Store the token
       * generation rate in imgdb::trate.
       *
       * Each token covers IMGDB_BPTOK bytes.
       */
      /* Lab7: YOUR CODE HERE */
      
      imgdsize = marshall_imsg(&imsg);
      net_assert((imgdsize > (double) LONG_MAX),
                 "imgdb::sendimg: image too big");
      gettimeofday(&start, NULL);
      sendimg(sd, &imsg, (char *) curimg.GetPixels(),
              (long)imgdsize, 0);
      gettimeofday(&end, NULL);

      /* compute elapsed time */
      usecs = USECSPERSEC-start.tv_usec+end.tv_usec;
      secs = end.tv_sec - start.tv_sec - 1;
      if (usecs > USECSPERSEC) {
        secs++;
        usecs -= USECSPERSEC;
      }
      fprintf(stderr, "\nElapsed time (m:s:ms:us): %d:%d:%d:%d\n",
              secs/60, secs%60, usecs/1000, usecs%1000);
      
    } else {
      sendimg(sd, &imsg, NULL, 0, 0);
    }
  }

  return;
}

int
main(int argc, char *argv[])
{ 
  socks_init();

  imgdb imgdb;
  
  /*
  // parse args, see the comments for imgdb::args()
  if (imgdb.args(argc, argv)) {
    fprintf(stderr, "Usage: %s\n", argv[0]); 
    exit(1);
  }
  */

  while (1) {
    imgdb.handleqry();
  }
    
#ifdef _WIN32
  WSACleanup();
#endif // _WIN32
  
  exit(0);
}
