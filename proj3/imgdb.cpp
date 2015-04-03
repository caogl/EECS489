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
#include <iostream>
#include <algorithm>
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
#include "socks.h"
#include "netimg.h"
#include "imgdb.h"
#include "fec.h"

/*
 * imgdb_args: parses command line args.
 *
 * Returns 0 on success or 1 on failure.  On successful return,
 * the provided drop probability is copied to memory pointed to by
 * "pdrop", which must be allocated by caller.  
 *
 * Nothing else is modified.
 */
int imgdb::
args(int argc, char *argv[])
{
  char c;
  extern char *optarg;

  if (argc < 1) {
    return (1);
  }
  
  while ((c = getopt(argc, argv, "d:")) != EOF) {
    switch (c) {
    case 'd':
      pdrop = atof(optarg);
      if (pdrop > 0.0 && (pdrop > NETIMG_MAXPROB || pdrop < NETIMG_MINPROB)) {
        fprintf(stderr, "%s: recommended drop probability between %f and %f.\n", argv[0], NETIMG_MINPROB, NETIMG_MAXPROB);
      }
      break;
    default:
      return(1);
      break;
    }
  }

  srandom(NETIMG_SEED+(int)(pdrop*1000));

  return (0);
}

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
  int len = sizeof(struct sockaddr_in);
  int bytes = recvfrom(sd, iqry, sizeof(iqry_t), 0, (struct sockaddr*)&client, (socklen_t *)&len);
 
  if (bytes != sizeof(iqry_t)) {
    return(NETIMG_ESIZE);
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
 * to imgdb::client using sendto() and wait for an ACK packet.
 * If ACK doesn't return before retransmission timeout,
 * re-send the packet.  Keep on trying for NETIMG_MAXTRIES times.
 *
 * Upon success, i.e., pkt sent without error and ACK returned,
 * the ACK pkt is stored in the provided "ack" variable, which
 * memory must have been allocated by caller and return the
 * return value of sendto(). Otherwise, return 0 if ACK not
 * received or if the received ACK packet is malformed.
 *
 * Nothing else is modified.
*/
int imgdb::
sendpkt(int sd, char *pkt, int size, ihdr_t *ack)
{
  /* PA3 Task 2.1: sends the provided pkt to client as you did in
   * Lab5.  In addition, initialize a struct timeval timeout variable
   * to NETIMG_SLEEP sec and NETIMG_USLEEP usec and wait for read
   * event on socket sd up to the timeout value.  If no read event
   * occurs before the timeout, try sending the packet to client
   * again.  Repeat NETIMG_MAXTRIES times.  If read event occurs
   * before timeout, receive the incoming packet and make sure that it
   * is an ACK pkt as expected.
   */
  /* PA3: YOUR CODE HERE */

  fd_set rset;
  struct timeval tv;
  FD_ZERO(&rset);
  FD_SET(sd, &rset);  
  socklen_t len=sizeof(struct sockaddr_in);

  int try_count=0;
  while(try_count<NETIMG_MAXTRIES)
  {
    int bytes = sendto(sd, pkt, size, 0, (struct sockaddr*)&client, len);
    net_assert((bytes<0), "imgdb_sendpkt: send error"); 

    tv.tv_sec = NETIMG_SLEEP;
    tv.tv_usec = NETIMG_USLEEP;
    select(sd+1, &rset, NULL, NULL, &tv);
    if(FD_ISSET(sd, &rset))
    {
      int err=recvfrom(sd, ack, sizeof(ihdr_t), 0, (struct sockaddr*)&client, &len);
      net_assert(err<0, "imgdb_sendpkt: recv error");
      if(ack->ih_vers == NETIMG_VERS && ack->ih_type == NETIMG_ACK)
      {
        ack->ih_seqn = ntohl(ack->ih_seqn);
        return (bytes);
      }
    }
    try_count++;
  }
  return(0);
}

/*
 * sendimg:
 * Send the image contained in *image to the client pointed to by
 * *client. Send the image in chunks of segsize, not to exceed mss,
 * instead of as one single image. With probability pdrop, drop a
 * segment instead of sending it.  Lab6 and PA3: compute and send an
 * accompanying FEC packet for every "fwnd"-full of data.
 *
 * PA3: If received malformed ACK to imsg, assume client has exited,
 * and simply return to caller.
 *
 * Terminate process upon encountering any error.
 * Doesn't otherwise modify anything.
*/
void imgdb::
sendimg(int sd, imsg_t *imsg, unsigned char *image, long img_size, int numseg)
{
  int left, bytes, segsize;
  unsigned char *ip;
  ihdr_t ack;
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
  bytes = sendpkt(sd, (char *) imsg, sizeof(imsg_t), &ack);
  if ((bytes != sizeof(imsg_t)) || (ack.ih_seqn != NETIMG_SYNSEQ)) 
  {
    fprintf(stderr, "sendpkt failed");
    return;
  }

  if (image) 
  {
    ip = image; /* ip points to the start of image byte buffer */
    int datasize = mss - sizeof(ihdr_t) - NETIMG_UDPIP;

    unsigned int snd_next=0;
    int fec_count=0; // how many segmants in this fec block has sent

    /* Lab5 Task 1:
     * make sure that the send buffer is of size at least mss.
     */
    /* Lab5: YOUR CODE HERE */
    int bufsize = (int)mss;
    setsockopt(sd, SOL_SOCKET,SO_SNDBUF,&bufsize,sizeof(int));

    /* Lab5 Task 1:
     *
     * Populate a struct msghdr with information of the destination
     * client, a pointer to a struct iovec array.  The iovec array
     * should be of size NETIMG_NUMIOV.  The first entry of the iovec
     * should be initialized to point to an ihdr_t, which should be
     * re-used for each chunk of data to be sent.
     */
    /* Lab5: YOUR CODE HERE */
    ihdr_t ihdr;
    ihdr.ih_vers = NETIMG_VERS;
    ihdr.ih_type = NETIMG_DATA;

    struct iovec iov[NETIMG_NUMIOV];
    iov[0].iov_base = &ihdr;
    iov[0].iov_len = sizeof(ihdr_t);
    struct msghdr mh;
    mh.msg_name = &client;
    mh.msg_namelen = sizeof(struct sockaddr_in);
    mh.msg_iov = iov;
    mh.msg_iovlen = NETIMG_NUMIOV;
    mh.msg_control = NULL;
    mh.msg_controllen = 0; 

    /* PA3 Task 2.2 and Task 4.1: initialize any necessary variables
     * for your sender side sliding window and FEC window.
     */
    /* PA3: YOUR CODE HERE */
    unsigned char FEC[datasize]; // FEC window 
    unsigned int window_base=0;
    int usable=rwnd;

    do 
    {
      /* PA3 Task 2.2: estimate the receiver's receive buffer based on packets
       * that have been sent and ACKed, including outstanding FEC packet(s).
       * We can only send as much as the receiver can buffer.
       * It's an estimate, so it doesn't have to be exact, being off by
       * one or two packets is fine.
       */
      /* PA3: YOUR CODE HERE */
      
      /* PA3 Task 2.2: Send one usable window-full of data to client
       * using sendmsg() to send each segment as you did in Lab5.  As
       * in Lab 5, you probabilistically drop a segment instead of
       * sending it.  Basically, copy the code within the do{}while
       * loop in imgdb::sendimg() from Lab 5 here, but put it within
       * another loop such that a usable window-full of segments can
       * be sent out using the Lab 5 code.  Don't forget to decrement
       * your "usable" even if you drop a packet.
       *
       * PA3 Task 4.1: Before you send out each segment, update your
       * FEC variables and initialize or accumulate your FEC data
       * packet by copying your Lab 6 code here appropriately.
       *
       * PA3 Task 4.1: After you send out each segment, if you have
       * accumulated an FEC window full of segments or the last
       * segment has been sent, send your FEC data.  Again, copy your
       * Lab 6 code here to the appropriate place.  You should also
       * probabilistically drop your FEC data.  Don't forget
       * to decrement your "usable" regardless of whether your FEC
       * data is dropped.
       */
      /* PA3: YOUR CODE HERE */
      while(usable>0)
      {
        if(fec_count<fwnd && (int)snd_next<img_size)
        {
          left=img_size-snd_next;
          segsize=datasize>left ? left : datasize;

          if(fec_count>0)
            fec_accum(FEC, ip+snd_next, datasize, (int)segsize);
          else
            fec_init(FEC, ip+snd_next, datasize, (int)segsize);

          fec_count++;

          /* probabilistically drop a segment */
          if(((float) random())/INT_MAX < pdrop)
            fprintf(stderr, "imgdb_sendimg: DROPPED offset 0x%x, %d bytes\n", snd_next, segsize);
          else
          { 
            ihdr.ih_type = NETIMG_DATA;
            ihdr.ih_size = htons(segsize);
            ihdr.ih_seqn = htonl(snd_next);
            iov[1].iov_base = ip+snd_next;
            iov[1].iov_len = segsize;
            if(sendmsg(sd, &mh, 0) == -1)
            {
              fprintf(stderr, "image socket sending error");
              close(sd);
              exit(1);
            }
            fprintf(stderr, "imgdb_sendimg: sent offset 0x%x, %d bytes, unacked: 0x%x\n", snd_next, segsize, window_base);
          }

          snd_next+=segsize;     
          usable--;
        }
        else if(fec_count>0 && (fec_count==fwnd || (int)snd_next>=img_size))
        {
          /* probabilistically drop a FEC packet */
          if (((float) random())/INT_MAX < pdrop)
            fprintf(stderr, "imgdb_sendimg: DROPFEC offset 0x%x, segment count: %d bytes\n", snd_next, fec_count);
          else
          {
            ihdr.ih_type = NETIMG_FEC;
            ihdr.ih_size = htons(datasize);
            ihdr.ih_seqn = htonl(snd_next);
            iov[1].iov_base = FEC;
            iov[1].iov_len = datasize;
            if(sendmsg(sd, &mh, 0) == -1)
            {
              fprintf(stderr, "image socket sending error");
              close(sd);
              exit(1);
            }
            fprintf(stderr, "imgdb_sendimg: sent FEC offset 0x%x, segment count: %d\n", snd_next, fec_count);
          }

          fec_count = 0;
          usable--;
        }
        else
        {
          // when fec_count is: 0, snd_next is: 0x111600, img_size is: 0x111600, enter this condition at the end of sending img
          break;
        }
      }

      /* PA3 Task 2.2: Next wait for ACKs for up to NETIMG_SLEEP secs
         and NETIMG_USLEEp usec. */
      /* PA3: YOUR CODE HERE */
      struct timeval tv;
      tv.tv_sec = NETIMG_SLEEP;
      tv.tv_usec = NETIMG_USLEEP;
      fd_set rset;
      FD_ZERO(&rset);
      FD_SET(sd, &rset);      
      
      /* PA3 Task 2.2: If an ACK arrived, grab it off the network and slide
       * our window forward when possible. Continue to do so for all the
       * ACKs that have arrived.  Remember that we're using cumulative ACK.
       *
       * We have a blocking socket, but here we want to
       * opportunistically grab all the ACKs that have arrived but not
       * wait for the ones that have not arrived.  So, when we call
       * receive, we want the socket to be non-blocking.  However,
       * instead of setting the socket to non-blocking and then set it
       * back to blocking again, we simply set flags=MSG_DONTWAIT when
       * calling the receive function.
       */
      /* PA3: YOUR CODE HERE */
      select(sd+1, &rset, NULL, NULL, &tv);
      socklen_t len = sizeof(struct sockaddr_in);
      
      if(FD_ISSET(sd, &rset))
      {
        while(1)
        {
          int err = recvfrom(sd, &ack, sizeof(ihdr_t), MSG_DONTWAIT, (struct sockaddr*)&client, &len);  
          if(err == -1) 
            break;
          if (ack.ih_vers == NETIMG_VERS && ack.ih_type == NETIMG_ACK)
          {
            ack.ih_seqn=ntohl(ack.ih_seqn);
            fprintf(stderr, "imgdb_sendimg: received ack 0x%x, unacked was 0x%x, send next 0x%x\n",
                                              ack.ih_seqn, window_base, snd_next); 
            window_base=max(window_base, ack.ih_seqn);
            usable++;
          }
        }
      }
      
      /* PA3 Task 2.2: If no ACK returned up to the timeout time,
       * trigger Go-Back-N and re-send all segments starting from the
       * last unACKed segment.
       *
       * PA3 Task 4.1: If you experience RTO, reset your FEC window to
       * start at the segment to be retransmitted.
       */
      else
      {
        fprintf(stderr, "imgdb_sendimg: RTO unacked 0x%x, next offset 0x%x\n", window_base, snd_next);
        snd_next=window_base;
        fec_count=0;
        usable=rwnd;
      }
       
      /* PA3: YOUR CODE HERE */
    } while ((int)window_base<img_size); // PA3 Task 2.2: replace the '1' with your condition for detecting 
    // that all segments sent have been acknowledged
    
    /* PA3 Task 2.2: after the image is sent send a NETIMG_FIN packet
     * and wait for ACK, using imgdb::sendpkt().
     */
    /* PA3: YOUR CODE HERE */
    ihdr.ih_vers = NETIMG_VERS;
    ihdr.ih_type = NETIMG_FIN;
    ihdr.ih_seqn = htonl(NETIMG_FINSEQ);
    sendpkt(sd, (char*)&ihdr, sizeof(ihdr_t), &ack); 
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

  imsg.im_type = recvqry(sd, &iqry);
  if (!imsg.im_type) 
  {
    imsg.im_type = readimg(iqry.iq_name, 1);
    
    if (imsg.im_type == NETIMG_FOUND) 
    {
      mss = (unsigned short) ntohs(iqry.iq_mss);
      // Lab6:
      rwnd = iqry.iq_rwnd;
      fwnd = iqry.iq_fwnd;

      imgdsize = marshall_imsg(&imsg);
      net_assert((imgdsize > (double) LONG_MAX),
                 "imgdb: image too big");
      sendimg(sd, &imsg, (unsigned char *) curimg.GetPixels(),
              (long)imgdsize, 0);
    } else {
      sendimg(sd, &imsg, NULL, 0, 0);
    }
  }
  // else ignore bad iqry packet

  return;
}

int
main(int argc, char *argv[])
{ 
  socks_init();

  imgdb imgdb;
  
  // parse args, see the comments for imgdb::args()
  if (imgdb.args(argc, argv)) {
    fprintf(stderr, "Usage: %s [ -d <drop probability> ]\n",
            argv[0]); 
    exit(1);
  }

  while (1) {
    imgdb.handleqry();
  }
    
#ifdef _WIN32
  WSACleanup();
#endif // _WIN32
  
  exit(0);
}
