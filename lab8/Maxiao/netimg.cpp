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
#include <stdlib.h>        // atoi()
#include <assert.h>        // assert()
#include <limits.h>        // LONG_MAX
#include <errno.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>      // socklen_t
#include "wingetopt.h"
#else
#include <string.h>        // memset(), memcmp(), strlen(), strcpy(), memcpy()
#include <unistd.h>        // getopt(), STDIN_FILENO, gethostname()
#include <signal.h>        // signal()
#include <netdb.h>         // gethostbyname()
#include <netinet/in.h>    // struct in_addr
#include <arpa/inet.h>     // htons()
#include <sys/types.h>     // u_short
#include <sys/socket.h>    // socket API
#include <sys/ioctl.h>     // ioctl(), FIONBIO
#endif
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#include "netimg.h"

int sd;                   /* socket descriptor */
imsg_t imsg;
unsigned char *image;
unsigned short mss;       // receiver's maximum segment size, in bytes
unsigned char rwnd;       // receiver's window, in packets, of size <= mss
unsigned short frate;     // flow rate, in Kbps

void
netimg_usage(char *progname)
{
  fprintf(stderr, "Usage: %s -s serverFQDN.port -q <imagename.tga> -w <rwnd [1, 255]> -m <mss (>40)> -r <flow rate [10, 10240]>\n", progname); 
  exit(1);
}

/*
 * netimg_args: parses command line args.
 *
 * Returns 0 on success or 1 on failure.  On successful return, *sname
 * points to the server's FQDN, and "port" points to the port to
 * connect at server, in network byte order.  Both "*sname", and
 * "port" must be allocated by caller.  The variable "*imagename"
 * points to the name of the image to search for.  The global
 * variables mss, rwnd, and frate are initialized.
 *
 * Nothing else is modified.
 */
int
netimg_args(int argc, char *argv[], char **sname, u_short *port, char **imagename)
{
  char c, *p;
  extern char *optarg;
  int arg;

  if (argc < 5) {
    return (1);
  }
  
  rwnd = NETIMG_RCVWIN;
  mss = NETIMG_MSS;
  frate = NETIMG_FRATE;

  while ((c = getopt(argc, argv, "s:q:w:m:r:")) != EOF) {
    switch (c) {
    case 's':
      for (p = optarg+strlen(optarg)-1;      // point to last character of addr:port arg
           p != optarg && *p != NETIMG_PORTSEP;  // search for ':' separating addr from port
           p--);
      net_assert((p == optarg), "netimg_args: server address malformed");
      *p++ = '\0';
      *port = htons((u_short) atoi(p)); // always stored in network byte order

      net_assert((p-optarg > NETIMG_MAXFNAME), "netimg_args: FQDN too long");
      *sname = optarg;
      break;
    case 'q':
      net_assert((strlen(optarg) >= NETIMG_MAXFNAME), "netimg_args: image name too long");
      *imagename = optarg;
      break;
    case 'w':
      arg = atoi(optarg);
      if (arg < 1 || arg > NETIMG_MAXWIN) {
        return(1);
      }
      rwnd = (unsigned char) arg; 
      break;
    case 'm':
      arg = atoi(optarg);
      if (arg < NETIMG_MINSS) {
        return(1);
      }
      mss = (unsigned short) arg;
      break;
    case 'r':
      arg = atoi(optarg);
      if (arg < 10 || arg > NETIMG_LINKRATE) {
        return(1);
      }
      frate = (unsigned short) arg;
      break;
    default:
      return(1);
      break;
    }
  }

  return (0);
}

/*
 * netimg_sockinit: creates a new socket to connect to the provided server.
 * The server's FQDN and port number are provided.  The port number
 * provided is assumed to already be in network byte order.
 *
 * On success, the global socket descriptor sd is initialized.
 * On error, terminates process.
 */
void
netimg_sockinit(char *sname, u_short port)
{
  int err;
  struct sockaddr_in server;
  struct hostent *sp;
  int bufsize=0;
  socklen_t optlen;

#ifdef _WIN32
  int sndbuf;
  socklen_t optlen;
  WSADATA wsa;
  
  err = WSAStartup(MAKEWORD(2,2), &wsa);  // winsock 2.2
  net_assert(err, "netimg_sockinit: WSAStartup");
#endif

  /* 
   * create a new UDP socket, store the socket in the global variable sd
  */
  sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);   // sd global
  net_assert((sd < 0), "netimg_sockinit: socket");

  /* obtain the server's IPv4 address from sname and initialize the
     socket address with server's address and port number . */
  memset((char *) &server, 0, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_port = port;
  sp = gethostbyname(sname);
  net_assert((sp == 0), "netimg_sockinit: gethostbyname");
  memcpy(&server.sin_addr, sp->h_addr, sp->h_length);

  /* set socket receive buffer size to be at least mss*rwnd bytes. */
  bufsize = mss*rwnd;  // rwnd and mss are global
  err = setsockopt(sd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(int));
  net_assert((err < 0), "netimg_sockinit: setsockopt RCVBUF");
  optlen = sizeof(int);
  err = getsockopt(sd, SOL_SOCKET, SO_RCVBUF, &bufsize, &optlen);
  net_assert((err < 0), "netimg_sockinit: getsockopt RCVBUF");

  fprintf(stderr, "netimg_sockinit: socket receive buffer set to %d bytes\n", bufsize);
  
  /* since this is a UDP socket, connect simply "remembers"
     server's address+port# */
  err = connect(sd, (struct sockaddr *) &server, sizeof(struct sockaddr_in));
  net_assert(err, "netimg_sockinit: connect");

  return;
}

/*
 * netimg_sendquery: send a query for provided imagename to
 * connected server.  Query is of type iqry_t, defined in netimg.h.
 * The query packet must be of version NETIMG_VERS and of type
 * NETIMG_SYN both also defined in netimg.h. In addition to the
 * filename of the image the client is searching for, the query
 * message also carries the receiver's window size (rwnd) and maximum
 * segment size (mss).  Both are global variables.
 *
 * On send error, return 0, else return 1
 */
int
netimg_sendquery(char *imagename)
{
  int bytes;
  iqry_t iqry;

  iqry.iq_vers = NETIMG_VERS;
  iqry.iq_type = NETIMG_SYN;
  iqry.iq_mss = htons(mss);     // global
  iqry.iq_frate = htons(frate); // global
  strcpy(iqry.iq_name, imagename); 
  bytes = send(sd, (char *) &iqry, sizeof(iqry_t), 0);
  if (bytes != sizeof(iqry_t)) {
    return(0);
  }

  return(1);
}
  
/*
 * netimg_recvimsg: receive an imsg_t packet from server and store it
 * in the global variable imsg.  The type imsg_t is defined in
 * netimg.h.  Check that received message is of the right version
 * number and of type NETIMG_DIM.  If message is of the wrong version
 * number or the wrong type, terminate process. For error in receiving
 * packet return -1.  If packet successfully
 * received, convert the integer fields of imsg back to host byte
 * order.  If the received imsg has im_found field == 0, it indicates
 * that no image is sent back, most likely due to image not found.  In
 * which case, return 0, otherwise return 1.
 */
int
netimg_recvimsg()
{
  int i;
  int bytes;
  double img_dsize;
  int img_size, format;

  /* receive imsg packet and check its version and type */
  bytes = recv(sd, (char *) &imsg, sizeof(imsg_t), 0);   // imsg global
  if (bytes <= 0) {
    return(-1);
  }
  net_assert((bytes != sizeof(imsg_t)), "netimg_recvimsg: malformed header");
  net_assert((imsg.im_vers != NETIMG_VERS), "netimg_recvimg: wrong imsg version");
  net_assert((imsg.im_type != NETIMG_DIM), "netimg_recvimg: wrong imsg type");

  if (imsg.im_found == NETIMG_FOUND) {
    imsg.im_height = ntohs(imsg.im_height);
    imsg.im_width = ntohs(imsg.im_width);
    imsg.im_format = ntohs(imsg.im_format);
    
    /* compute image size */
    img_dsize = (double) (imsg.im_height*imsg.im_width*(u_short)imsg.im_depth);
    net_assert((img_dsize > (double) NETIMG_MAXSEQ), "netimg_recvimsg: image too large");
    img_size = (int) img_dsize;                 // global

    /* allocate space for image */
    image = (unsigned char *)calloc(img_size, sizeof(unsigned char));

    /* determine pixel format */
    switch(imsg.im_format) {
    case GL_RGBA:
      format = 4;
      break;
    case GL_RGB:
      format = 3;
      break;
    case GL_LUMINANCE_ALPHA:
      format = 2;
      break;
    default:
      format = 1;
      break;
    }

    /* paint the image texture background red if color, white otherwise
     to better visualize lost segments */
    for (i = 0; i < img_size; i += format) {
      image[i] = (unsigned char) 0xff;
    }
  }

  return (imsg.im_found);
}

/*
 * netimg_recvimage: called by GLUT when idle
 * On each call, receive a chunk of the image from the network and
 * store it in global variable "image" at offset from the
 * start of the buffer as specified in the header of the packet.
 *
 * Terminate process on receive error.
 */
void
netimg_recvimage(void)
{
  int bytes;
  ihdr_t hdr;
  struct msghdr msg;
  struct iovec iov[NETIMG_NUMIOVEC];
   
  /* 
   * The image data packet from the server consists of an ihdr_t header
   * followed by a chunk of data.  We want to put the data directly into
   * the buffer pointed to by the global variable "image" without any
   * additional copying. To determine the correct offset from the start of
   * the buffer to put the data into, we first need to retrieve the
   * sequence number stored in the packet header.  Since we're dealing with
   * UDP packet, however, we can't simply read the header off the network,
   * leaving the rest of the packet to be retrieved by subsequent calls to
   * recv(). Instead, what we need to do is call recv() with flags == MSG_PEEK.
   * This allows us to retrieve a copy of the header without removing the packet
   * from the receive buffer.
   *
   * Since our socket has been set to non-blocking mode, if there's no packet
   * ready to be retrieved from the socket, the call to recv() will return
   * immediately with return value -1 and the system global variable "errno"
   * set to EAGAIN or EWOULDBLOCK (defined in errno.h).  In which case, 
   * this function should simply return to caller.
   * 
   * Once a copy of the header is made, check that it has the version number and
   * that it is of type NETIMG_DAT.  Convert the size and sequence number in the
   * header to host byte order.
   */
  bytes = recv(sd, (char *) &hdr, sizeof(ihdr_t), MSG_PEEK);
  if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    return;
  }
  net_assert((bytes != sizeof(ihdr_t)), "netimg_recvimage: recv bad header size");
  net_assert((hdr.ih_vers != NETIMG_VERS), "netimg_recvimage: wrong version");
  net_assert((!(hdr.ih_type & NETIMG_DAT)), "netimg_recvimage: wrong type");
  hdr.ih_size = ntohs(hdr.ih_size);
  hdr.ih_seqn = ntohl(hdr.ih_seqn);

  fprintf(stderr, "netimg_recvimage: received offset 0x%x, %d bytes\n",
          hdr.ih_seqn, hdr.ih_size);

  /*
   * Now that we have the offset/seqno information from the packet
   * header, populate a struct msghdr with a pointer to a struct iovec
   * array.  The iovec array should be of size NETIMG_NUMIOVEC.  The
   * first entry of the iovec should be initialized to point to the
   * header above, which should be re-used for each chunk of data
   * received.
   * 
   * Point the second entry of the iovec to the correct offset from
   * the start of the image buffer pointed to by the global variable
   * "image".  Both the offset/seqno and the size of the data to be
   * received into the image buffer are recorded in the packet header
   * retrieved above. Receive the segment by calling recvmsg().
  */
  memset((char *) &msg, 0, sizeof(struct msghdr));
  msg.msg_iov = iov;
  msg.msg_iovlen = NETIMG_NUMIOVEC;
  iov[0].iov_base = &hdr;
  iov[0].iov_len = sizeof(ihdr_t);
  iov[1].iov_base = image+hdr.ih_seqn;
  iov[1].iov_len = hdr.ih_size;
  
  bytes = recvmsg(sd, &msg, 0);
  net_assert((bytes != (int)(sizeof(ihdr_t)+ntohs(hdr.ih_size))), "netimg_recvimage: recv bad packet size");
  
  /* give the updated image to OpenGL for texturing */
  glTexImage2D(GL_TEXTURE_2D, 0, (GLint) imsg.im_format,
               (GLsizei) imsg.im_width, (GLsizei) imsg.im_height, 0,
               (GLenum) imsg.im_format, GL_UNSIGNED_BYTE, image);
  /* redisplay */
  glutPostRedisplay();

  return;
}

int
main(int argc, char *argv[])
{
  char *sname, *imagename;
  u_short port;
  int err;
  int nonblock=1;

  // parse args, see the comments for netimg_args()
  if (netimg_args(argc, argv, &sname, &port, &imagename)) {
    netimg_usage(argv[0]);
  }

#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);    /* don't die if peer is dead */
#endif
  
  srandom(48914);

  netimg_sockinit(sname, port);

  if (netimg_sendquery(imagename)) {

    err = netimg_recvimsg();
    if (err == NETIMG_FOUND) { // if image found
      netimg_glutinit(&argc, argv, netimg_recvimage);
      netimg_imginit();
      
      /* set socket non blocking */
      ioctl(sd, FIONBIO, &nonblock);

      /* start the GLUT main loop */
      glutMainLoop();

    } else if (err == NETIMG_FULL) {
      fprintf(stderr, "%s: server busy, please try again later.\n", argv[0]);

    } else {
      fprintf(stderr, "%s: %s image not found.\n", argv[0], imagename);
    }
  }

  return(0);
}
