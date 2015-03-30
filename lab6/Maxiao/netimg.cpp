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
#include <algorithm>       // min()
#endif
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#include "netimg.h"
#include "fec.h"

int sd;                   /* socket descriptor */
imsg_t imsg;
int img_size;
unsigned char *image;
unsigned short mss;       // receiver's maximum segment size, in bytes
unsigned char rwnd;       // receiver's window, in packets (of size <= mss)
unsigned char fwnd;       // receiver's FEC window < rwnd, in packets
unsigned int fec_lost;	  // the first lost packet of current FEC window
unsigned int fec_head; 	  // the start position of current FEC window
unsigned int fec_exp;	  // the expected next packet of current FEC window 
int fec_count;	  	  // the number of received packets of current FEC window 

void
netimg_usage(char *progname)
{
  fprintf(stderr, "Usage: %s -s serverFQDN.port -q <imagename.tga> -w <rwnd [1, 255]> -m <mss (>40)>\n", progname); 
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
 * variables mss and rwnd are initialized.
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
  
  rwnd = NETIMG_NUMSEG;
  mss = NETIMG_MSS;
  fec_lost = fec_head = fec_exp = 0; // init FEC window parameters
  fec_count = 0;

  while ((c = getopt(argc, argv, "s:q:w:m:")) != EOF) {
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
  /* Lab 5: YOUR CODE HERE */
  sd = socket(AF_INET, SOCK_DGRAM, 0);

  /* obtain the server's IPv4 address from sname and initialize the
     socket address with server's address and port number . */
  memset((char *) &server, 0, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_port = port;
  sp = gethostbyname(sname);
  net_assert((sp == 0), "netimg_sockinit: gethostbyname");
  memcpy(&server.sin_addr, sp->h_addr, sp->h_length);

  /* 
   * set socket receive buffer size to be at least mss*rwnd bytes.
  */
  /* Lab 5: YOUR CODE HERE */
  bufsize = mss*rwnd;
  setsockopt(sd, SOL_SOCKET,SO_RCVBUF,&bufsize,sizeof(int));

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
 * On send error, close socket, return 0, else return 1
 */
int
netimg_sendquery(char *imagename)
{
  int bytes;
  iqry_t iqry;

  iqry.iq_vers = NETIMG_VERS;
  iqry.iq_type = NETIMG_SYN;
  iqry.iq_mss = htons(mss);   // global
  iqry.iq_rwnd = rwnd;        // global
  iqry.iq_fwnd = fwnd = NETIMG_FECWIN >= rwnd ? rwnd-1 : NETIMG_FECWIN;  // Lab 6
  strcpy(iqry.iq_name, imagename); 
  bytes = send(sd, (char *) &iqry, sizeof(iqry_t), 0);
  if (bytes != sizeof(iqry_t)) {
    //close(sd);
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
 * packet, close the socket and return -1.  If packet successfully
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
  int format;

  /* receive imsg packet and check its version and type */
  bytes = recv(sd, (char *) &imsg, sizeof(imsg_t), 0);   // imsg global
  if (bytes <= 0) {
    //close(sd);
    return(-1);
  }
  net_assert((bytes != sizeof(imsg_t)), "netimg_recvimsg: malformed header");
  net_assert((imsg.im_vers != NETIMG_VERS), "netimg_recvimg: wrong imsg version");
  net_assert((imsg.im_type != NETIMG_DIM), "netimg_recvimg: wrong imsg type");

  if (imsg.im_found) {
    imsg.im_height = ntohs(imsg.im_height);
    imsg.im_width = ntohs(imsg.im_width);
    imsg.im_format = ntohs(imsg.im_format);
    
    /* compute image size */
    img_dsize = (double) (imsg.im_height*imsg.im_width*(u_short)imsg.im_depth);
    net_assert((img_dsize > (double) NETIMG_MAXSEQ), "netimg_recvimsg: image too large");
    img_size = (int) img_dsize;                 // global

    /* allocate space for image */
    image = (unsigned char *)malloc(img_size*sizeof(unsigned char));

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

    return (1);
  }

  return (0);
}

/* Callback functions for GLUT */

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
   * Once a copy of the header is made, check that it has the version
   * number and that it is of type NETIMG_DAT.  Convert the size and
   * sequence number in the header to host byte order.
   */
  /* Lab 5: YOUR CODE HERE */
  int err, datasize, num, rem;
  unsigned int snd_next;
  unsigned short segsize;
  ihdr_t ihdr;
  
  err = recv(sd, &ihdr, sizeof(ihdr_t), MSG_PEEK);
  if (err == -1 || ihdr.ih_vers != NETIMG_VERS || 
      (ihdr.ih_type != NETIMG_DAT && ihdr.ih_type != NETIMG_FEC)){
    return;  
  }
   
  segsize = ntohs(ihdr.ih_size);
  snd_next = ntohl(ihdr.ih_seqn);  
  datasize = mss - sizeof(ihdr_t) - NETIMG_UDPIPSIZE; // maximum bytes of a data or FEC packet
  num = (snd_next-fec_head)/(fwnd*datasize); // num and rem is used to check if the packet is in the current window  
  rem = (snd_next-fec_head)%(fwnd*datasize);

  /* Task 2
   * Populate a struct msghdr with a pointer to a struct iovec
   * array.  The iovec array should be of size NETIMG_NUMIOVEC.  The
   * first entry of the iovec should be initialized to point to the
   * header above, which should be re-used for each chunk of data
   * received.
   *
   * This is the same code from Lab 5, we're just pulling 
   * the parts common to both NETIMG_DAT and NETIMG_FEC packets
   * out of the two code branches.
   */
  /* Lab 5: YOUR CODE HERE */
  struct iovec iov[NETIMG_NUMIOVEC];
  iov[0].iov_base = &ihdr;
  iov[0].iov_len = sizeof(ihdr_t);

  struct msghdr mh;
  mh.msg_name = NULL;
  mh.msg_namelen = 0;
  mh.msg_iov = iov;
  mh.msg_iovlen = NETIMG_NUMIOVEC;
  mh.msg_control = NULL;
  mh.msg_controllen = 0;

  if (ihdr.ih_type == NETIMG_DAT) {
    /* 
     * Task 2
     * Now that we have the offset/seqno information from the packet
     * header, point the second entry of the iovec to the correct offset from
     * the start of the image buffer pointed to by the global variable
     * "image".  Both the offset/seqno and the size of the data to be
     * received into the image buffer are recorded in the packet header
     * retrieved above. Receive the segment by calling recvmsg().
     * Convert the size and sequence number in the header to host byte order.
     */
    /* Lab 5: YOUR CODE HERE */
    iov[1].iov_base = image+snd_next;
    iov[1].iov_len = segsize;

    if (recvmsg(sd, &mh, 0) == -1){
      close(sd);
      return;      
    }

    fprintf(stderr, "netimg_recvimage: received offset %d, %d bytes\n",
            snd_next, segsize);
 
    /* Task 2
     *
     * You should handle the case when the FEC data packet itself may be
     * lost, and when multiple packets within an FEC window are lost, and
     * when the first few packets from the subsequent FEC window following a
     * lost FEC data packet are also lost.  Thus in In addition to relying on
     * fwnd and the count of total packets received within an FEC
     * window, you may want to rely on the sequence numbers in arriving
     * packets to determine when you have received an FEC-window full of data
     * bytes.
     *
     * To that end, in addition to keeping track of lost packet offset
     * below, every time a data packet arrives, first check whether
     * you have received an FEC-window full (or more) of data bytes
     * without receiving any FEC packet.  In which case, you need to
     * reposition your FEC window by computing the start of the
     * current FEC window, reset your count of packets received, and
     * determine the next expected packet.
     *
     * Check whether the arriving data packet is the next data packet
     * you're expecting.  If not, you've lost a packet, mark the
     * location of the first lost packet in an FEC window.  If more
     * than one packet is lost, you don't need to mark subsequent
     * losses, just keep a count of the total number of packets received.
     */
    /* YOUR CODE HERE */
    if (num)
    { // at least one previous FEC packet has been lost, 
      fec_head += (num*fwnd*datasize);
      fec_count = 0; 
      if (rem) 
        fec_lost = fec_head;
    } 
    else if (snd_next != fec_exp) 
    { 
      fec_lost = fec_exp;
    } 
    
    fec_exp = snd_next + segsize;
    fec_count++;

  } else { // FEC pkt

    /* Task 2
     *
     * Re-use the same struct msghdr above to receive an FEC packet.
     * Point the second entry of the iovec to your FEC data buffer and
     * update the size accordingly.
     * Receive the segment by calling recvmsg().
     *
     * Convert the size and sequence number in the header to host byte order.
     *
     * This is an adaptation of your Lab 5 code.
     */
    /* YOUR CODE HERE */
    unsigned char* FEC = new unsigned char[datasize]; // FEC window
    iov[1].iov_base = FEC;
    iov[1].iov_len = datasize;

    if (recvmsg(sd, &mh, 0) == -1){
      close(sd);
      return;      
    }
  
    /* Task 2
     * Check if you've lost only one packet within the FEC window, if so,
     * reconstruct the lost packet.  Remember that we're using the image data
     * buffer itself as our FEC buffer and that you've noted above the
     * sequence number that marks the start of the current FEC window.  To
     * reconstruct the lost packet, use fec.cpp:fec_accum() to XOR
     * the received FEC data against the image data buffered starting from
     * the start of the current FEC window, one <tt>datasize</tt> at a time,
     * skipping over the lost segment, until you've reached the end of the
     * FEC window.  If fec_accum() has been coded correctly, it
     * should be able to correcly handle the case when the last segment of
     * the FEC-window is smaller than datasize *(but you must still do the
     * detection for short last segment here and provide fec_accum() with the
     * appropriate segsize)*.
     *
     * Once you've reconstructed the lost segment, copy it from the FEC data buffer to
     * correct offset on the image buffer.  You must be careful that if the
     * lost segment is the last segment of the image data, it may be of size
     * smaller than datasize, in which case, you should copy only
     * the correct amount of bytes from the FEC data buffer to the image data
     * buffer.  If no packet was lost in the current FEC window, or if more
     * than one packets were lost, there's nothing further to do with the
     * current FEC window, just move on to the next one.
     *
     * Before you move on to the next FEC window, you may want to
     * reset your FEC-window related variables to prepare for the
     * processing of the next window.
     */
    /* YOUR CODE HERE */
    if (num > 1 || (num == 1 && rem)){ 
      fec_head += (fwnd * datasize * (num + (rem > 0) -1)); 
      fec_lost = fec_head;
      fec_count = 0;
    } else if (snd_next != fec_exp){
      fec_lost = fec_exp;
    }
  
    fprintf(stderr, "netimg_recvimage: received FEC offset %d, start segment %d, segment count: %d\n",
            snd_next, fec_head, fec_count);
  
    num = (snd_next-fec_head)/datasize;
    rem = (snd_next-fec_head)%datasize;
    if (fec_count == num + (rem > 0) -1){ // rem is used to detect the last short data packet
      for (unsigned int j=fec_head; j<snd_next; j += datasize){
	if (j != fec_lost){
          fec_accum(FEC, image+j, datasize, std::min(datasize, img_size-(int)j));
	}
      }
      memcpy(image+fec_lost, FEC, std::min(datasize, img_size-(int)fec_lost));
    }

    fec_count = 0;
    fec_lost = 0;
    fec_head = snd_next;
    fec_exp = snd_next;
    delete[] FEC; // release the memory of FEC window 
  }

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
    if (err == 1) { // if image found
      netimg_glutinit(&argc, argv, netimg_recvimage);
      netimg_imginit();
      
      /* set socket non blocking */
      /* Lab 5: YOUR CODE HERE */
      int nonblocking = 1;
      ioctl(sd, FIONBIO, &nonblocking);

      /* start the GLUT main loop */
      glutMainLoop();

    } else if (err < 0) {
      fprintf(stderr, "%s: server busy, please try again later.\n", argv[0]);

    } else {
      fprintf(stderr, "%s: %s image not found.\n", argv[0], imagename);
    }
  }

  return(0);
}
