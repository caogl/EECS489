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
#include <math.h>          // ceil()
#include <algorithm>
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
#include "fec.h"           // Lab 6

int sd;                    // socket descriptor
imsg_t imsg;
unsigned char *image;
int img_size;              // Lab 6
float pdrop;               // Lab 6: drop probability
unsigned short mss;        // receiver's maximum segment size, in bytes
unsigned char rwnd;        // receiver's window, in packets, of size <= mss
unsigned char fwnd;        // Lab 6: receiver's FEC window < rwnd, in packets
unsigned int next_seqn;    // Lab 6
unsigned int fec_head;     // the start position of current FEC window
int fec_count;             // the number of received packets of current FEC window 
int mode; 		   // FEC: 1; GO-BACK-N: 0
int datasize; 		   // the maximum size of a data or FEC packet 
unsigned int snd_una;	   // This viariable corresponds the snd_una on server side; 
			   // here it equals to the largest undropped ACK seqn + datasize
unsigned int fec_last;	   // used only when ihdr.ih_seqn > next_seqn


void
netimg_usage(char *progname)
{
  fprintf(stderr, "Usage: %s -s serverFQDN.port -q <imagename.tga> -d <drop probability [0.011, 0.11]> -w <rwnd [1, 255]> -m <mss (>40)>\n", progname); 
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
 * variables mss, rwnd, and pdrop are initialized.
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
  
  pdrop = NETIMG_PDROP;
  rwnd = NETIMG_NUMSEG;
  mss = NETIMG_MSS;
  snd_una = fec_head = fec_last = fec_count = 0; // init FEC window parameters
  mode = 1;

  while ((c = getopt(argc, argv, "s:q:w:m:d:")) != EOF) {
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
      if (arg < NETIMG_MINSS || arg > NETIMG_MSS) {
        return(1);
      }
      mss = (unsigned short) arg;
      break;
    case 'd':
      pdrop = atof(optarg);  // global
      if (pdrop > 0.0 && (pdrop > 0.11 || pdrop < 0.051)) {
        fprintf(stderr, "%s: recommended drop probability between 0.011 and 0.51.\n", argv[0]);
      }
      break;
    default:
      return(1);
      break;
    }
  }

  datasize = mss - sizeof(ihdr_t) - NETIMG_UDPIPSIZE;
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
  WSADATA wsa;
  
  err = WSAStartup(MAKEWORD(2,2), &wsa);  // winsock 2.2
  net_assert(err, "netimg_sockinit: WSAStartup");
#endif

  /* 
   * create a new UDP socket, store the socket in the global variable sd
  */
  sd = socket(AF_INET, SOCK_DGRAM, 0);

  /* obtain the server's IPv4 address from sname and initialize the
     socket address with server's address and port number . */
  memset((char *) &server, 0, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_port = port;
  sp = gethostbyname(sname);
  net_assert((sp == 0), "netimg_sockinit: gethostbyname");
  memcpy(&server.sin_addr, sp->h_addr, sp->h_length);

  /* set socket receive buffer size */
  bufsize = mss*rwnd;
  setsockopt(sd, SOL_SOCKET,SO_RCVBUF,&bufsize,sizeof(int));

  fprintf(stderr, "netimg_sockinit: socket receive buffer set to %d bytes\n", bufsize);
  
  /* since this is a UDP socket, connect simply "remembers" server's address+port# */
  err = connect(sd, (struct sockaddr *) &server, sizeof(struct sockaddr_in));
  net_assert(err, "netimg_sockinit: connect");

  return;
}

/*
 * netimg_sendquery: send a query for provided imagename to connected
 * server.  Query is of type iqry_t, defined in netimg.h.  The query
 * packet must be of version NETIMG_VERS and of type NETIMG_SYN both
 * also defined in netimg.h. In addition to the filename of the image
 * the client is searching for, the query message also carries the
 * receiver's FEC window size, receive window size (rwnd) and maximum
 * segment size (mss).  Both rwnd and mss are global variables.
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
  iqry.iq_mss = htons(mss);   // global
  iqry.iq_rwnd = rwnd;        // global
  iqry.iq_fwnd = fwnd = NETIMG_FECWIN >= rwnd ? rwnd-1 : NETIMG_FECWIN;  // Lab 6
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
 * packet, close the socket and return -1.  If packet successfully
 * received, convert the integer fields of imsg back to host byte
 * order.  If the received imsg has im_found field == 0, it indicates
 * that no image is sent back, most likely due to image not found.  In
 * which case, return 0, otherwise return 1.
 */
int
netimg_recvimsg()
{
  int i, bytes;
  double img_dsize;
  int format;

  /* receive imsg packet and check its version and type */
  bytes = recv(sd, (char *) &imsg, sizeof(imsg_t), 0);   // imsg global
  if (bytes <= 0) {
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

    /* Send back an ACK with ih_type = NETIMG_ACK.
     * Initialize any variable necessary to keep track of ACKs */
    next_seqn = 0; // init the next sequence number of ACK

    ihdr_t ihdr;
    ihdr.ih_vers = NETIMG_VERS;
    ihdr.ih_type = NETIMG_ACK;
    ihdr.ih_seqn = htonl(NETIMG_DIMSEQ); 
    bytes = send(sd, (char *) &ihdr, sizeof(ihdr_t), 0);
    if (bytes != sizeof(ihdr_t)) {
      return(-1);
    }

    return (1);
  }

  return (0);
}

/* 
 * netimg_recvimage: called by GLUT when idle.
 * On each call, receive a chunk of the image from the network and
 * store it in global variable "image" at offset from the
 * start of the buffer as specified in the header of the packet.
 *
 * Terminate process on receive error.
 */
void
netimg_recvimage(void)
{
  ihdr_t ihdr;
  
  /* The image data packet from the server consists of an ihdr_t header
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
  int err, num ,rem;

  err = recv(sd, &ihdr, sizeof(ihdr_t), MSG_PEEK);
  if (err == -1 || ihdr.ih_vers != NETIMG_VERS || 
      (ihdr.ih_type != NETIMG_DAT && ihdr.ih_type != NETIMG_FEC
       && ihdr.ih_type != NETIMG_FIN)){
    return;  
  }

  ihdr.ih_size = ntohs(ihdr.ih_size);
  ihdr.ih_seqn = ntohl(ihdr.ih_seqn);
  num = (ihdr.ih_seqn-fec_head)/datasize;
  rem = (ihdr.ih_seqn-fec_head)%datasize;

  /* Populate a struct msghdr with a pointer to a struct iovec
   * array.  The iovec array should be of size NETIMG_NUMIOVEC.  The
   * first entry of the iovec should be initialized to point to the
   * header above, which should be re-used for each chunk of data
   * received.
   *
   * This is the same code from Lab 5, we're just pulling 
   * the parts common to both NETIMG_DAT and NETIMG_FEC packets
   * out of the two code branches.
   */
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

  /* Task 2.3: initialize your ACK packet */
  ihdr_t ack;
  ack.ih_vers = NETIMG_VERS;
  ack.ih_type = NETIMG_ACK;
  bool flag = false; // indicate whether we need sending an ACK
  
  if (ihdr.ih_type == NETIMG_DAT) {
    fprintf(stderr, "netimg_recvimage: received offset 0x%x, %d bytes, waiting for 0x%x\n",
            ihdr.ih_seqn, ihdr.ih_size, next_seqn); 
    /* 
     * Now that we have the offset/seqno information from the packet
     * header, point the second entry of the iovec to the correct offset from
     * the start of the image buffer pointed to by the global variable
     * "image".  Both the offset/seqno and the size of the data to be
     * received into the image buffer are recorded in the packet header
     * retrieved above. Receive the segment by calling recvmsg().
     * Convert the size and sequence number in the header to host byte order.
     */
     iov[1].iov_base = image+ihdr.ih_seqn;;
     iov[1].iov_len = ihdr.ih_size;
  
     if (recvmsg(sd, &mh, 0) == -1){
       return; 
     }
 
     ihdr.ih_size = ntohs(ihdr.ih_size);
     ihdr.ih_seqn = ntohl(ihdr.ih_seqn);
       
    /* Task 2.3: If the incoming data packet carries the expected
     * sequence number, update our expected sequence number and
     * prepare to send back an ACK packet.  Otherwise, if the packet
     * arrived out-of-order and the sequence number is larger than the
     * expected one, don't send back an ACK, per Go-Back-N.  If the
     * sequence number is smaller than the expected sequence number,
     * however, do send back an ACK, tagged with the expected sequence
     * number, just to ensure that the sender knows what our current
     * expectation is.
     */
       
    /* You should handle the case when the FEC data packet itself may be
     * lost, and when multiple packets within an FEC window are lost, and
     * when the first few packets from the subsequent FEC window following a
     * lost FEC data packet are also lost.  Thus in In addition to relying on
     * fwnd and the count of total packets received within an FEC
     * window, you want to rely on the sequence numbers in arriving
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
     */
    /* Lab 6: YOUR CODE HERE */
    if (num >= (int)fwnd){ // NOT receiving a FEC packet for last window
      if (mode){ // consider only when mode is FEC
 	if (fec_count == fwnd){ // the last window is all set
          fec_count = 0;
          fec_head += fwnd*datasize; // move FEC window one step forward
	  net_assert((fec_head != next_seqn), "netimg: wrong next_seqn");         
        } else { // at least a data packet and FEC packet are lost for last window
          mode = 0;
	  fec_count = 0;
          fec_head = snd_una;
     	}   
      } 
    }

    /* Task 4.2: Next check whether the arriving data packet is the
     * next data packet you're expecting.  If so, we are not in
     * Go-Back-N retransmission mode, so we should increment our next
     * expected packet within the FEC window and if we were in
     * Go-Back-N retransmission mode, take ourselves out of it.
     *
     * If packet is smaller than next_seqn, just send an ACK
     *
     */
    /* YOUR CODE HERE */
    if (ihdr.ih_seqn == next_seqn){
      next_seqn += ihdr.ih_size;
      ack.ih_seqn = htonl(next_seqn);
      flag = true;
      if (mode){
        fec_count++; 
      } else {
        mode = 1; // Back to mode "FEC"
        fec_count = (next_seqn - fec_head)/datasize; // re-adjust the count 
      }
    } else if (ihdr.ih_seqn < next_seqn){ // client is RTOing
      ack.ih_seqn = htonl(next_seqn);
      flag = true;
    } else if (ihdr.ih_seqn > next_seqn){
      if (mode){ // consider only FEC mode
        if (ihdr.ih_seqn <= fec_last || 
            (++fec_count < num && ihdr.ih_seqn+ihdr.ih_size == (unsigned int)img_size )){ 
        // switch to GO-BACK-N mode when 
	// (1) the current byte is smaller than last received byte
	// (2) fec_count (include this one) indicates lost two packets, and it's the end of image
          mode = 0;
	  fec_head = snd_una;
          fec_count = 0;
        } 
      } 
    } 

    fec_last = ihdr.ih_seqn; 

  } else if (ihdr.ih_type == NETIMG_FEC) { // FEC pkt

    /* 
     * Re-use the same struct msghdr above to receive an FEC packet.
     * Point the second entry of the iovec to your FEC data buffer and
     * update the size accordingly.
     * Receive the segment by calling recvmsg().
     *
     * Convert the size and sequence number in the header to host byte order.
     *
     * This is an adaptation of your Lab 5 code.
     */
    unsigned char* FEC = new unsigned char[datasize]; // FEC data
    iov[1].iov_base = FEC;
    iov[1].iov_len = datasize;

    if (recvmsg(sd, &mh, 0) == -1){
      return; 
    }

    ihdr.ih_size = ntohs(ihdr.ih_size);
    ihdr.ih_seqn = ntohl(ihdr.ih_seqn);

    fprintf(stderr, "netimg_recvimage: received FEC offset 0x%x, start: 0x%x, " 
	    "lost: 0x%x, count: %d\n", ihdr.ih_seqn, fec_head, next_seqn, fec_count);

    if (mode){
     if (num > (int)fwnd || fec_count < num+(rem>0)-1){ 
      // an entire window is missing or current window lost more than 2 packets
      // set mode to GO BACK N 
	mode = 0; 
        fec_head = snd_una;
      } else if (fec_count == num+(rem>0)-1){ // lost only one packet
        for (unsigned int j=fec_head; j<ihdr.ih_seqn; j += datasize){
	  if (j != next_seqn){
            fec_accum(FEC, image+j, datasize, std::min(datasize, img_size-(int)j));
          }
        }
        memcpy(image+next_seqn, FEC, std::min(datasize, img_size-(int)next_seqn));
        ack.ih_seqn = htonl(ihdr.ih_seqn);
	next_seqn = ihdr.ih_seqn;
        flag = true;
        fec_head = ihdr.ih_seqn;
      } else {
        fec_head = ihdr.ih_seqn;
      } 
      fec_count = 0;
    }

    delete[] FEC;

  } else {  // NETIMG_FIN pkt
    err = recv(sd, &ihdr, sizeof(ihdr_t), 0); 
    if (err == -1) return;
    ack.ih_seqn = htonl(NETIMG_FINSEQ);
    flag = true;  
  }
  
  if (flag){ // send an ACK packet
    if (((float) random())/INT_MAX < pdrop) {
      fprintf(stderr, "netimg_recvimage: ack dropped 0x%x\n", ntohl(ack.ih_seqn));
    } else {
      err = send(sd, (char*)&ack , sizeof(ihdr_t), 0);
      if (err == -1) return; 
      if (ntohl(ack.ih_seqn) != NETIMG_FINSEQ){
	snd_una = ntohl(ack.ih_seqn);
      }
      fprintf(stderr, "netimg_recvimage: ack sent 0x%x\n", ntohl(ack.ih_seqn));
    } 
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
  
  srandom(48914+(int)(pdrop*100));

  netimg_sockinit(sname, port);

  if (netimg_sendquery(imagename)) {

    err = netimg_recvimsg();
    if (err == 1) { // if image found
      netimg_glutinit(&argc, argv, netimg_recvimage);
      netimg_imginit();
      
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
