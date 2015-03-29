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
#endif
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "ltga.h"
#include "netimg.h"

void
imgdb_usage(char *progname)
{  
  fprintf(stderr, "Usage: %s [-d <drop probability>]\n", progname); 
  exit(1);
}

/*
 * imgdb_args: parses command line args.
 *
 * Returns 0 on success or 1 on failure.  On successful return,
 * the provided drop probability is copied to memory pointed to by
 * "pdrop", which must be allocated by caller.  
 *
 * Nothing else is modified.
 */
int
imgdb_args(int argc, char *argv[], float *pdrop)
{
  char c;
  extern char *optarg;

  if (argc < 1) {
    return (1);
  }
  
  *pdrop = NETIMG_PDROP;

  while ((c = getopt(argc, argv, "d:")) != EOF) {
    switch (c) {
    case 'd':
      *pdrop = atof(optarg);
      if (*pdrop > 0.0 && (*pdrop > 0.11 || *pdrop < 0.011)) {
        fprintf(stderr, "%s: recommended drop probability between 0.011 and 0.11.\n", argv[0]);
      }
      break;
    default:
      return(1);
      break;
    }
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

  /* Task 1:
   * Fill out the rest of this function.
  */
  /* create a UDP socket */
  /* YOUR CODE HERE */
  sd = socket(AF_INET, SOCK_DGRAM, 0);
 
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
 * Task 1:
 * imgdb_recvquery: receive an iqry_t packet and store
 * the client's address and port number in the provided
 * "client" argument.  Check that the incoming iqry_t packet
 * is of version NETIMG_VERS and of type NETIMG_SYN.  If so,
 * save the mss and rwnd and image name information into the
 * provided "mss", "rwnd", and "fname" arguments respectively.
 * The provided arguments must all point to pre-allocated space.
 *
 * Terminate process if error encountered when receiving packet
 * or if packet is of the wrong version or type.
 *
 * Nothing else is modified.
*/
void
imgdb_recvquery(int sd, struct sockaddr_in *client, unsigned short *mss, unsigned char *rwnd, char *fname)
{
  /* YOUR CODE HERE */
  iqry_t iqry;
  int len = sizeof(struct sockaddr_in);
  int bytes = recvfrom(sd, (char*)&iqry, sizeof(iqry_t), 0, 
              (struct sockaddr*)client, (socklen_t *)&len);
  if (bytes != sizeof(iqry_t) || iqry.iq_vers != NETIMG_VERS || iqry.iq_type != NETIMG_SYN){
    close(sd);
    return;
  } 
  *mss = ntohs(iqry.iq_mss);
  *rwnd = iqry.iq_rwnd;
  strcpy(fname, iqry.iq_name);

  return;
}
  
/* 
 * Task 1:
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
  /* YOUR CODE HERE */
  int bytes = sendto(sd, pkt, size, 0, 
              (struct sockaddr*)client, sizeof(struct sockaddr_in));
  if (bytes != size){
    close(sd);
    return(1);
  }
  return(0);
}

/*
 * imgdb_sendimage: send the image to the client
 * Send the image contained in *image to the client
 * pointed to by *client. Send the image in
 * chunks of segsize, not to exceed mss, instead of
 * as one single image. With probability pdrop, drop
 * a segment instead of sending it.
 *
 * Terminate process upon encountering any error.
 * Doesn't otherwise modify anything.
*/
void
imgdb_sendimage(int sd, struct sockaddr_in *client, unsigned short mss,
                unsigned char rwnd, LTGA *image, int img_size, float pdrop)
{
  char *ip;

  unsigned short segsize;
  int left, datasize;
  unsigned int snd_next;

  ip = (char *) image->GetPixels();    /* ip points to the start of byte buffer holding image */

  segsize = datasize = mss - sizeof(ihdr_t) - NETIMG_UDPIPSIZE;

  snd_next = 0;

  /* Task 1:
   * make sure that the send buffer is of size at least mss.
  */
  /* YOUR CODE HERE */
  int bufsize = (int)mss;
  setsockopt(sd, SOL_SOCKET,SO_SNDBUF,&bufsize,sizeof(int)); 

  /* Task 1:
   * populate a struct msghdr with information of the destination client,
   * a pointer to a struct iovec array.  The iovec array should be of size
   * NETIMG_NUMIOVEC.  The first entry of the iovec should be initialized
   * to point to an ihdr_t, which should be re-use for each chunk of data
   * to be sent.
  */
  /* YOUR CODE HERE */
  ihdr_t ihdr;
  ihdr.ih_vers = NETIMG_VERS;
  ihdr.ih_type = NETIMG_DAT;

  struct iovec iov[NETIMG_NUMIOVEC];
  iov[0].iov_base = &ihdr;
  iov[0].iov_len = sizeof(ihdr_t); 

  struct msghdr mh;
  mh.msg_name = client;
  mh.msg_namelen = sizeof(struct sockaddr_in);
  mh.msg_iov = iov;
  mh.msg_iovlen = NETIMG_NUMIOVEC;
  mh.msg_control = NULL;
  mh.msg_controllen = 0;    

  do {
    /* probabilistically drop a segment */
    if (((float) random())/INT_MAX < pdrop) {
      fprintf(stderr, "imgdb_sendimage: DROPPED offset 0x%x, %d bytes\n",
              snd_next, segsize);
      snd_next += datasize;
      continue;
    } 

    /* size of this segment */
    left = img_size - snd_next;
    segsize = datasize > left ? left : datasize;

    /* Task 1: 
     * Send one segment of data of size segsize at each iteration.
     * Point the second entry of the iovec to the correct offset
     * from the start of the image.  Update the sequence number
     * and size fields of the ihdr_t header to reflect the byte
     * offset and size of the current chunk of data.  Send
     * the segment off by calling sendmsg().
     */
    /* YOUR CODE HERE */
    ihdr.ih_size = htons(segsize);
    ihdr.ih_seqn = htonl(snd_next); 
    iov[1].iov_base = ip+snd_next;
    iov[1].iov_len = segsize;

    if (sendmsg(sd, &mh, 0) == -1){
      close(sd);
      return;   
    }       

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
  float pdrop=NETIMG_PDROP;
  LTGA image;
  imsg_t imsg;
  int img_size;
  struct sockaddr_in client;
  unsigned short mss;
  unsigned char rwnd;
  char fname[NETIMG_MAXFNAME] = { 0 };

#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);    /* don't die if peer is dead */
#endif

  // parse args, see the comments for imgdb_args()
  if (imgdb_args(argc, argv, &pdrop)) {
    imgdb_usage(argv[0]);
  }

  srandom(48914+(int)(pdrop*100));
  sd = imgdb_sockinit();
  
  while (1) {
    imgdb_recvquery(sd, &client, &mss, &rwnd, fname);
    imgdb_imginit(fname, &image, &imsg, &img_size);
    imgdb_sendpkt(sd, &client, (char *) &imsg, sizeof(imsg_t));
    imgdb_sendimage(sd, &client, mss, rwnd, &image, img_size, pdrop);
  }
    
  close(sd);
#ifdef _WIN32
  WSACleanup();
#endif
  exit(0);
}
