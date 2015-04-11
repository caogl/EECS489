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
#include "netimg.h"

#define USECSPERSEC 1000000

void
imgdb_usage(char *progname)
{  
  fprintf(stderr, "Usage: %s\n", progname); 
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
imgdb_args(int argc, char *argv[])
{
  if (argc < 1) {
    return (1);
  }
  
  return (0);
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
    imsg->im_found = 1;

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
 * imgdb_recvquery: receive an iqry_t packet and store the client's
 * address and port number in the provided "client" argument.  Check
 * that the incoming iqry_t packet is of version NETIMG_VERS and of
 * type NETIMG_SYN.  If so, save the mss, bucket size, token rate, and
 * image name information into the provided "mss", "bsize", "trate",
 * and "fname" arguments respectively.  The provided arguments must
 * all point to pre-allocated space.
 *
 * Terminate process if error encountered when receiving packet
 * or if packet is of the wrong version or type.
 *
 * Nothing else is modified.
*/
void
imgdb_recvquery(int sd, struct sockaddr_in *client, unsigned short *mss,
                unsigned short *bsize, unsigned short *trate, char *fname)
{
  int bytes;
  socklen_t len;
  iqry_t iqry;
  
  len = sizeof(struct sockaddr_in);
  bytes = recvfrom(sd, &iqry, sizeof(iqry_t), 0, (struct sockaddr *) client, &len);
  net_assert((bytes <= 0), "imgdb_recvquery: recvfrom");
  
  if (bytes == sizeof(iqry_t) &&
      iqry.iq_vers == NETIMG_VERS &&
      iqry.iq_type == NETIMG_SYN) {

    net_assert((strlen((char *) &iqry.iq_name) >= NETIMG_MAXFNAME),
               "imgdb_recvquery: iqry name length");
    
    *mss = (unsigned short) ntohs(iqry.iq_mss);
    *bsize = (unsigned short) ntohs(iqry.iq_bsize);
    *trate = (unsigned short) ntohs(iqry.iq_trate);
    strcpy(fname, iqry.iq_name);
  }

  return;
}
  
/* 
 * imgdb_sendpkt: sends the provided "pkt" of size "size"
 * to the client "client".
 *
 * Terminate process if error encountered when sending packet.
 * Returns 0 on success.
 *
 * Nothing else is modified.
*/
int
imgdb_sendpkt(int sd, struct sockaddr_in *client, char *pkt, int size)
{
  int bytes;
    
  bytes = sendto(sd, pkt, size, 0, (struct sockaddr *) client, sizeof(struct sockaddr_in));
  net_assert((bytes != size), "imgdb_sendpkt: sendto pkt");

  return(0);
}

/*
 * imgdb_sendimage:
 * Send the image contained in *image to the client
 * pointed to by *client. Send the image in
 * chunks of segsize, not to exceed mss, instead of
 * as one single image.  Each segment can be sent
 * only if there's enough tokens to cover it.
 * The token bucket filter parameters are "bsize"
 * tokens and "trate" tokens/sec.
 *
 * Terminate process upon encountering any error.
 * Doesn't otherwise modify anything.
*/
void
imgdb_sendimage(int sd, struct sockaddr_in *client, unsigned short mss,
                unsigned short bsize, unsigned short trate, LTGA *image, int img_size)
{
  char *ip;

  unsigned short segsize;
  int left, datasize;
  unsigned int snd_next;
  int err, bytes, offered, usable;
  socklen_t optlen;
  struct msghdr msg;
  struct iovec iov[NETIMG_NUMIOVEC];
  ihdr_t hdr;
  
  ip = (char *) image->GetPixels();    /* ip points to the start of byte buffer holding image */

  datasize = mss - sizeof(ihdr_t);

  snd_next = 0;

  /* Task 2: initialize any token bucket filter variables you may have here */
  /* YOUR CODE HERE */
  float avail = (float)bsize; // the available token number currently, initially bsize 
  float single = (float)(mss-sizeof(ihdr_t))/NETIMG_BPTOK; // token size needed to transmit one segment
  float incre; // the new generated token size

  /* make sure that the send buffer is of size at least mss. */
  offered = mss;
  optlen = sizeof(int);
  err = getsockopt(sd, SOL_SOCKET, SO_SNDBUF, &usable, &optlen);
  if (usable < offered) {
    err = setsockopt(sd, SOL_SOCKET, SO_SNDBUF, &offered, sizeof(int));
    net_assert((err < 0), "imgdb_sendimage: setsockopt SNDBUF");
  }

  /* 
   * Populate a struct msghdr with information of the destination client,
   * a pointer to a struct iovec array.  The iovec array should be of size
   * NETIMG_NUMIOVEC.  The first entry of the iovec should be initialized
   * to point to an ihdr_t, which should be re-used for each chunk of data
   * to be sent.
  */
  msg.msg_name = client;
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

  do {
    /* size of this segment */
    left = img_size - snd_next;
    segsize = datasize > left ? left : datasize;

    /*
     * Task 2:
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
    /* YOUR CODE HERE */
    if (avail < single){
      incre = single - avail + ((float)random()/INT_MAX) * bsize;
      usleep(ceil(incre*1000000/trate));
      avail += incre;
      if (avail > (float)bsize){
        avail = (float)bsize;
      } 
    }
    
    avail -= single; // take one segment size of tokens out

    /*
     * Send one segment of data of size segsize at each iterationi
     * once there's enough tokens in the token bucket.
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
    net_assert((bytes != (int)(segsize+sizeof(ihdr_t))), "imgdb_sendimage: sendmsg bytes");
    
    fprintf(stderr, "imgdb_sendimage: sent offset 0x%x, %d bytes\n",
            snd_next, segsize);
    
    snd_next += segsize;

  } while ((int) snd_next < img_size);

  return;
}

int
main(int argc, char *argv[])
{ 
  int sd;
  LTGA image;
  imsg_t imsg;
  int img_size;
  struct sockaddr_in client;
  unsigned short mss, bsize, trate;
  char fname[NETIMG_MAXFNAME] = { 0 };
  struct timeval start, end;
  int usecs, secs;

#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);    /* don't die if peer is dead */
#endif

  // parse args, see the comments for imgdb_args()
  if (imgdb_args(argc, argv)) {
    imgdb_usage(argv[0]);
  }

  srandom(489+NETIMG_BPTOK+NETIMG_SRATE);
  sd = imgdb_sockinit();
  
  while (1) {

    imgdb_recvquery(sd, &client, &mss, &bsize, &trate, fname);
    imgdb_imginit(fname, &image, &imsg, &img_size);
    imgdb_sendpkt(sd, &client, (char *) &imsg, sizeof(imsg_t));

    gettimeofday(&start, NULL);
    imgdb_sendimage(sd, &client, mss, bsize, trate, &image, img_size);
    gettimeofday(&end, NULL);

    /* compute elapsed time */
    usecs = USECSPERSEC-start.tv_usec+end.tv_usec;
    secs = end.tv_sec - start.tv_sec - 1;
    if (usecs > USECSPERSEC) {
      secs++;
      usecs -= USECSPERSEC;
    }
    fprintf(stderr, "\n%s: Elapsed time (m:s:ms:us): %d:%d:%d:%d\n", argv[0], secs/60, secs%60, usecs/1000, usecs%1000);

  }
    
#ifdef _WIN32
  WSACleanup();
#endif
  exit(0);
}
