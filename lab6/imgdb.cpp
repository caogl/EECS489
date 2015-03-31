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

#define MINPROB 0.011
#define MAXPROB 0.11

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
      if (pdrop > 0.0 && (pdrop > MAXPROB || pdrop < MINPROB)) {
        fprintf(stderr, "%s: recommended drop probability between %f and %f.\n", argv[0], MINPROB, MAXPROB);
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
  /*
   * Lab5 Task 1: Call recvfrom() to receive the iqry_t packet from
   * client.  Store the client's address and port number in the
   * imgdb::client member variable and store the return value of
   * recvfrom() in local variable "bytes".
  */
  /* Lab5: YOUR CODE HERE */
  int len = sizeof(struct sockaddr_in);
  int bytes = recvfrom(sd, iqry, sizeof(iqry_t), 0, (struct sockaddr*)&client, (socklen_t *)&len);  
  
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
 * Lab5 Task 1:
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
  /* Lab5: YOUR CODE HERE */
  // update the return value to the correct value.
  int bytes = sendto(sd, pkt, size, 0, (struct sockaddr*)&client, sizeof(struct sockaddr_in));
  if (bytes <0)
  {
    fprintf(stderr, "send pkt error");
    exit(1);
  }

  return(0);
}

/*
 * sendimg:
 * Send the image contained in *image to the client
 * pointed to by *client. Send the image in
 * chunks of segsize, not to exceed mss, instead of
 * as one single image. With probability pdrop, drop
 * a segment instead of sending it.
 * Lab6: compute and send an accompanying FEC packet
 * for every "fwnd"-full of data.
 *
 * Terminate process upon encountering any error.
 * Doesn't otherwise modify anything.
*/
void imgdb::
sendimg(int sd, imsg_t *imsg, unsigned char *image, long img_size, int numseg)
{
  int bytes;
  unsigned short segsize; // transport layer actual send size
  unsigned short datasize; // maximal transport layer send size (after substracting UDP/IP header)
  unsigned char *ip;
  long left;
  int snd_next=0; //  the starting byte position of next image segment

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
  if(bytes<0)
  {
    fprintf(stderr, "sendpkt failed");
    exit(1);
  }

  if (image) 
  {
    ip = image; /* ip points to the start of image byte buffer */
    datasize = mss - sizeof(ihdr_t) - NETIMG_UDPIP;

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

    unsigned char FEC[datasize]; // FEC window 
    memset(FEC, 0, datasize);

    do 
    {
      /* size of this segment */
      left = img_size - snd_next;
      segsize = datasize > left ? left : datasize;
      
      /* Lab6 Task 1:
       *
       * If this is your first segment in an FEC window, use it to
       * initialize your FEC data.  Subsequent segments within the FEC
       * window should be XOR-ed with the content of your FEC data.
       *
       * You MUST use the two helper functions fec.cpp:fec_init()
       * and fec.cpp:fec_accum() to encapsulate your FEC computation
       * for the first and subsequent segments of the FEC window.
       * You are to write these two functions.
       *
       * You need to figure out how to:
       * 1. maintain your FEC window,
       * 2. keep track of your progress over each FEC window, and
       * 3. compute your FEC data across the multiple data segments.
       */
      /* Lab6: YOUR CODE HERE */
      
      if (fec_count)      
        fec_accum(FEC, ip+snd_next, datasize, (int)segsize);
      else
        fec_init(FEC, ip+snd_next, datasize, (int)segsize);
  
      fec_count++;

      /* probabilistically drop a segment */
      if (((float) random())/INT_MAX < pdrop) 
        fprintf(stderr, "imgdb_sendimg: DROPPED offset 0x%x, %d bytes\n", snd_next, segsize);
      else 
      {     
        /* Lab5 Task 1: 
         * Send one segment of data of size segsize at each iteration.
         * Point the second entry of the iovec to the correct offset
         * from the start of the image.  Update the sequence number
         * and size fields of the ihdr_t header to reflect the byte
         * offset and size of the current chunk of data.  Send
         * the segment off by calling sendmsg().
         */
        /* Lab5: YOUR CODE HERE */
        ihdr.ih_type = NETIMG_DATA;
        ihdr.ih_size = htons(segsize);
        ihdr.ih_seqn = htonl(snd_next);
        iov[1].iov_base = ip+snd_next;
        iov[1].iov_len = segsize;
        if (sendmsg(sd, &mh, 0) == -1)
        {
          fprintf(stderr, "image socket sending error");
          close(sd);
          exit(1);
        } 

        fprintf(stderr, "imgdb_sendimg: sent offset 0x%x, %d bytes\n",
                snd_next, segsize);     
      }

      snd_next += (int)datasize;
          
      /* Lab6 Task 1
       *
       * If one fwnd-full of fec has been accumulated or last chunk
       *   of data has just been sent, send fec
       *
       * Point the second entry of the iovec to your FEC data.
       * The sequence number of the FEC packet MUST be the sequence
       * number of the first image data byte beyond the FEC window.
       * The size of an FEC packet MUST be "datasize".
       * Don't forget to use network byte order.
       * Send the FEC packet off by calling sendmsg().
       *
       * After you've sent off your FEC packet, you may want to
       * reset your FEC-window related variables to prepare for the
       * processing of the next window.  If you re-use the same header
       * for sending image data and FEC data, make sure you reset the
       * ih_type field of the header also.
       */
      /* Lab6: YOUR CODE HERE */
       if (fec_count == fwnd || (int)snd_next >= img_size)
       {
         /* probabilistically drop a FEC packet */
         if (((float) random())/INT_MAX < pdrop)
           fprintf(stderr, "imgdb_sendimage: DROPFEC offset 0x%x, segment count: %d bytes\n", snd_next, fec_count);
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
           fprintf(stderr, "imgdb_sendimage: sent FEC offset 0x%x, segment count: %d\n", snd_next, fec_count);
        }
        fec_count = 0;
      } 
            
    } while ((int) snd_next < img_size);
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
  if (imsg.im_type) {
    sendimg(sd, &imsg, NULL, 0, 0);
  } else {
    
    imsg.im_type = readimg(iqry.iq_name, 1);
    
    if (imsg.im_type == NETIMG_FOUND) {

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
