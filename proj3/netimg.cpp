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
#include <stdlib.h>        // atoi()
#include <assert.h>        // assert()
#include <limits.h>        // LONG_MAX
#include <math.h>          // ceil()
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
#include "socks.h"
#include "fec.h"          // Lab6

int sd;                   // socket descriptor
imsg_t imsg;
long img_size;
unsigned char *image;
unsigned short mss;       // receiver's maximum segment size, in bytes
unsigned char rwnd;       // receiver's window, in packets, of size <= mss
unsigned char fwnd;       // Lab6: receiver's FEC window < rwnd, in packets
// PA3: for ACKs
float pdrop;
unsigned int next_seqn;

/*
 * netimg_args: parses command line args.
 *
 * Returns 0 on success or 1 on failure.  On successful return,
 * "*sname" points to the server's name, and "port" points to the port
 * to connect at server, in network byte order.  Both "*sname", and
 * "port" must be allocated by caller.  The variable "*imgname" points
 * to the name of the image to search for.  The global variables mss,
 * rwnd, and pdrop are initialized.
 *
 * Nothing else is modified.
 */
int
netimg_args(int argc, char *argv[], char **sname, u_short *port, char **imgname)
{
  char c, *p;
  extern char *optarg;
  int arg;

  if (argc < 5) {
    return (1);
  }
  
  pdrop = NETIMG_PDROP;
  rwnd = NETIMG_RCVWIN;
  mss = NETIMG_MSS;

  while ((c = getopt(argc, argv, "s:q:w:m:d:")) != EOF) {
    switch (c) {
    case 's':
      for (p = optarg+strlen(optarg)-1;  // point to last character of
                                         // addr:port arg
           p != optarg && *p != NETIMG_PORTSEP;
                                         // search for ':' separating
                                         // addr from port
           p--);
      net_assert((p == optarg), "netimg_args: server address malformed");
      *p++ = '\0';
      *port = htons((u_short) atoi(p)); // always stored in network byte order

      net_assert((p-optarg > NETIMG_MAXFNAME),
                 "netimg_args: server's name too long");
      *sname = optarg;
      break;
    case 'q':
      net_assert((strlen(optarg) >= NETIMG_MAXFNAME),
                 "netimg_args: image name too long");
      *imgname = optarg;
      break;
    case 'w':
      arg = atoi(optarg);
      if (arg < NETIMG_MINWIN || arg > NETIMG_MAXWIN) {
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
      if (pdrop > 0.0 && (pdrop > NETIMG_MAXPROB || pdrop < NETIMG_MINPROB)) {
        fprintf(stderr, "%s: recommended drop probability between %f and %f.\n",
                argv[0], NETIMG_MINPROB, NETIMG_MAXPROB);
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
 * netimg_sendqry: send a query for provided imgname to
 * connected server.  Query is of type iqry_t, defined in netimg.h.
 * The query packet must be of version NETIMG_VERS and of type
 * NETIMG_SYNQRY both also defined in netimg.h. In addition to the
 * filename of the image the client is searching for, the query
 * message also carries the receiver's window size (rwnd), maximum
 * segment size (mss), and FEC window size (used in Lab6).
 * All three are global variables.
 *
 * On send error, return 0, else return 1
 */
int
netimg_sendqry(char *imgname)
{
  int bytes;
  iqry_t iqry;

  iqry.iq_vers = NETIMG_VERS;
  iqry.iq_type = NETIMG_SYNQRY;
  iqry.iq_mss = htons(mss);      // global
  iqry.iq_rwnd = rwnd;           // global
  iqry.iq_fwnd = fwnd = NETIMG_FECWIN >= rwnd ? rwnd-1 : NETIMG_FECWIN;  // Lab6
  strcpy(iqry.iq_name, imgname); 
  bytes = send(sd, (char *) &iqry, sizeof(iqry_t), 0);
  if (bytes != sizeof(iqry_t)) {
    return(0);
  }

  return(1);
}
  
/*
 * netimg_recvimsg: receive an imsg_t packet from server and store it
 * in the global variable imsg.  The type imsg_t is defined in
 * netimg.h. Return NETIMG_EVERS if packet is of the wrong version.
 * Return NETIMG_ESIZE if packet received is of the wrong size.
 * Otherwise return the content of the im_type field of the received
 * packet. Upon return, all the integer fields of imsg MUST be in HOST
 * BYTE ORDER. If msg_type is NETIMG_FOUND, compute the size of the
 * incoming image and store the size in the global variable
 * "img_size".
 */
char
netimg_recvimsg()
{
  int bytes;
  double imgdsize;

  /* receive imsg packet and check its version and type */
  bytes = recv(sd, (char *) &imsg, sizeof(imsg_t), 0); // imsg global
  if (bytes != sizeof(imsg_t)) {
    return(NETIMG_ESIZE);
  }
  if (imsg.im_vers != NETIMG_VERS) {
    return(NETIMG_EVERS);
  }

  if (imsg.im_type == NETIMG_FOUND) {
    imsg.im_height = ntohs(imsg.im_height);
    imsg.im_width = ntohs(imsg.im_width);
    imsg.im_format = ntohs(imsg.im_format);

    imgdsize = (double) (imsg.im_height*imsg.im_width*(u_short)imsg.im_depth);
    net_assert((imgdsize > (double) LONG_MAX), 
               "netimg_recvimsg: image too big");
    img_size = (long) imgdsize;                 // global

    /* PA3 Task 2.1:
     *
     * Send back an ACK with ih_type = NETIMG_ACK and ih_seqn =
     * NETIMG_SYNSEQ.  Initialize any variable necessary to keep track
     * of ACKs.
     */
    /* PA3: YOUR CODE HERE */
  }

  return((char) imsg.im_type);
}

/* Callback function for GLUT.
 *
 * netimg_recvimg: called by GLUT when idle. On each call, receive a
 * chunk of the image from the network and store it in global variable
 * "image" at offset from the start of the buffer as specified in the
 * header of the packet.
 *
 * Terminate process on receive error.
 */
void
netimg_recvimg(void)
{
  ihdr_t hdr;  // memory to hold packet header
   
  /* 
   * Lab5 Task 2:
   * 
   * The image data packet from the server consists of an ihdr_t
   * header followed by a chunk of data.  We want to put the data
   * directly into the buffer pointed to by the global variable
   * "image" without any additional copying. To determine the correct
   * offset from the start of the buffer to put the data into, we
   * first need to retrieve the sequence number stored in the packet
   * header.  Since we're dealing with UDP packet, however, we can't
   * simply read the header off the network, leaving the rest of the
   * packet to be retrieved by subsequent calls to recv(). Instead, we
   * call recv() with flags == MSG_PEEK.  This allows us to retrieve a
   * copy of the header without removing the packet from the receive
   * buffer.
   *
   * Since our socket has been set to non-blocking mode, if there's no
   * packet ready to be retrieved from the socket, the call to recv()
   * will return immediately with return value -1 and the system
   * global variable "errno" set to EAGAIN or EWOULDBLOCK (defined in
   * errno.h).  In which case, this function should simply return to
   * caller.
   * 
   * Once a copy of the header is made to the local variable "hdr",
   * check that it has the correct version number and that it is of
   * type NETIMG_DATA (use bitwise '&' as NETIMG_FEC is also of type
   * NETIMG_DATA).  Terminate process if any error is encountered.
   * Otherwise, convert the size and sequence number in the header
   * to host byte order.
   */
  /* Lab5: YOUR CODE HERE */

  /* Lab5 Task 2
   *
   * Populate a struct msghdr with a pointer to a struct iovec
   * array.  The iovec array should be of size NETIMG_NUMIOV.  The
   * first entry of the iovec should be initialized to point to the
   * header above, which should be re-used for each chunk of data
   * received.
   */
  /* Lab5: YOUR CODE HERE */

  /* Task 2.3: initialize your ACK packet */

  if (hdr.ih_type == NETIMG_DATA) {
    /* 
     * Lab5 Task 2
     *
     * Now that we have the offset/seqno information from the packet
     * header, point the second entry of the iovec to the correct
     * offset from the start of the image buffer pointed to by the
     * global variable "image".  Both the offset/seqno and the size of
     * the data to be received into the image buffer are recorded in
     * the packet header retrieved above. Receive the segment "for
     * real" (as opposed to "peeking" as we did above) by calling
     * recvmsg().  We'll be overwriting the information in the "hdr"
     * local variable, so remember to convert the size and sequence
     * number in the header to host byte order again.
     */
    /* Lab5: YOUR CODE HERE */

    // PA3: added next_seqn
    fprintf(stderr, "netimg_recvimg: received offset 0x%x, %d bytes, waiting for 0x%x\n", hdr.ih_seqn, hdr.ih_size, next_seqn);
            
    /* Lab6 Task 2
     *
     * You should handle the case when the FEC data packet itself may be
     * lost, and when multiple packets within an FEC window are lost, and
     * when the first few packets from the subsequent FEC window following a
     * lost FEC data packet are also lost.  Thus in addition to relying on
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
     */
    /* Lab6: YOUR CODE HERE */

    /* PA3: Task 4.2: 
     *
     * Next check whether the arriving data packet is the next data
     * packet you're expecting.  If so, we are not in Go-Back-N
     * retransmission mode, so we should increment our next expected
     * packet within the FEC window and if we were in Go-Back-N
     * retransmission mode, take ourselves out of it.  If the gap
     * between the arriving data packet and the start of the FEC
     * window is larger than an FEC window, we have lost an FEC
     * packet, but we haven't lost any data packet, so just slide the
     * FEC window forward by updating the relevant FEC variables.
     *
     * If arrving data packet is not the expected packet, we've 
     * lost one or more segments, mark the location of the first
     * lost segment (i.e., the next packet you're expecting).
     * If more than one segments are lost, you don't need to
     * mark subsequent losses, just keep a count of the total number
     * of segments received.  If arriving data packet has sequence
     * number within the current fwnd, increment count, otherwise, pkt
     * was out of order, don't increment count.  If we have lost 
     * two or more consecutive data packets, immediately put the client
     * in Go-Back-N mode and reset the FEC window to start from the
     * expected next sequence number.
     *
     * If the gap between the start of the FEC window and the current
     * byte is larger than an FEC-widow full of data, we have lost an
     * FEC packet and at least one data segment.  Since the FEC packet
     * is lost, there's no way for us to recover the lost data
     * segment(s) and Go-Back-N will be triggered and we should ride
     * it out by putting ourselves into Go-Back-N mode and not rely on
     * FEC until the expected segment has been retransmitted and
     * received. Reset the FEC window to start from the
     * expected next sequence number.
     *
     * Everytime we slide forward or reset the FEC window, check whether
     * we're at the last FEC window of the transmission.  If so, update
     * the FEC window size (imgdb::fwnd member variable) and other
     * relevant variables accordingly.
     */
    /* PA3: YOUR CODE HERE */

    /* PA3 Task 4.2: If we're not in Go-Back-N mode, keep track of packet
       received within the current FEC window */
    /* PA3: YOUR CODE HERE */

    /* PA3 Task 2.3: If the incoming data packet carries the expected
     * sequence number, update our expected sequence number.  In all
     * cases, prepare to send back an ACK packet with the next
     * expected sequence number, to ensure that the sender knows what
     * our current expectation is.
     */
    /* PA3: YOUR CODE HERE */
      
  } else if (hdr.ih_type == NETIMG_FEC) { // FEC pkt

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
    /* Lab 6: YOUR CODE HERE */

    /* 
     * PA3 Task 4.2: If you're not in Go-Back-N mode, do as in Lab6:
     *
     * Check if you've lost only one packet within the FEC window, if
     * so, reconstruct the lost packet.  Remember that we're using the
     * image data buffer itself as our FEC buffer and that you've
     * noted above the sequence number that marks the start of the
     * current FEC window.  To reconstruct the lost packet, use
     * fec.cpp:fec_accum() to XOR the received FEC data against the
     * image data buffered starting from the start of the current FEC
     * window, one <tt>datasize</tt> at a time, skipping over the lost
     * segment, until you've reached the end of the FEC window.  If
     * fec_accum() has been coded correctly, it should be able to
     * correcly handle the case when the last segment of the
     * FEC-window is smaller than datasize *(but you must still do the
     * detection for short last segment here and provide fec_accum()
     * with the appropriate segsize)*.
     *
     * Once you've reconstructed the lost segment, copy it from the
     * FEC data buffer to correct offset on the image buffer.  You
     * must be careful that if the lost segment is the last segment of
     * the image data, it may be of size smaller than datasize, in
     * which case, you should copy only the correct amount of bytes
     * from the FEC data buffer to the image data buffer.
     *
     * PA3 Task 4.2: After you've patched the lost packet, send back
     * an ACK for the last byte received within this FEC window.
     *
     * If no packet was lost in the current FEC window, there's
     * nothing further to do with the current FEC window, just move on
     * to the next one.
     *
     * PA3 Task 4.2: If more than 1 pkts were lost within the current
     * FEC window, put yourself in Go-Back-N mode.
     *
     * Before you move on to the next FEC window, reset your
     * FEC-window related variables to prepare for the processing of
     * the next window. Remember to check if the next FEC window is
     * the last window of the transmission.
     */
    /* PA3: YOUR CODE HERE */  // modifying Lab6 code

  } else {  // NETIMG_FIN pkt

    /* PA3 Task 2.3: else it's a NETIMG_FIN packet, prepare to send
       back an ACK with NETIMG_FINSEQ as the sequence number */
    /* PA3 YOUR CODE HERE */ 
  }

  /* PA3 Task 2.3:
   * If we're to send back an ACK, send it now.  Probabilistically
   * drop the ACK instead of sending it back (see how this is done in
   * imgdb.cpp).
   */ 
  /* PA3: YOUR CODE HERE */
  
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
  int err;
  char *sname, *imgname;
  u_short port;

  // parse args, see the comments for netimg_args()
  if (netimg_args(argc, argv, &sname, &port, &imgname)) {
    fprintf(stderr, "Usage: %s -s <server>%c<port> -q <image>.tga [ -d <drop probability [0.011, 0.11]> -w <rwnd [1, 255]> -m <mss (>40)> ]\n", argv[0], NETIMG_PORTSEP); 
    exit(1);
  }

  srandom(NETIMG_SEED+(int)(pdrop*1000));

  socks_init();

  sd = socks_clntinit(sname, port, rwnd*mss);  // Lab5 Task 2

  if (netimg_sendqry(imgname)) {
    err = netimg_recvimsg();

    if (err == NETIMG_FOUND) { // if image received ok
      netimg_glutinit(&argc, argv, netimg_recvimg);
      netimg_imginit(imsg.im_format);
      
      /* Lab5 Task 2: set socket non blocking */
      /* Lab5: YOUR CODE HERE */

      glutMainLoop(); /* start the GLUT main loop */
    } else if (err == NETIMG_NFOUND) {
      fprintf(stderr, "%s: %s image not found.\n", argv[0], imgname);
    } else if (err == NETIMG_EVERS) {
      fprintf(stderr, "%s: wrong version number.\n", argv[0]);
    } else if (err == NETIMG_EBUSY) {
      fprintf(stderr, "%s: image server busy.\n", argv[0]);
    } else if (err == NETIMG_ESIZE) {
      fprintf(stderr, "%s: wrong size.\n", argv[0]);
    } else {
      fprintf(stderr, "%s: image receive error %d.\n", argv[0], err);
    }
  }

  socks_close(sd); // optional, but since we use connect(), might as well.
  return(0);
}
