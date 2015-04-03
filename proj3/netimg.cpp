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
#include <algorithm>
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
using namespace std;

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

int fec_count; // how many data segments has been received in this fec window
unsigned int fec_start; // starting byte position of current fec window
unsigned int fec_next; // starting byte position of next expected data segment
unsigned int fec_lost; // the first lost data segment in this fec window

// PA3: for ACKs
float pdrop;
bool mode; // whether the client is in go back N mode or not

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

  if (imsg.im_type == NETIMG_FOUND) 
  {
    imsg.im_height = ntohs(imsg.im_height);
    imsg.im_width = ntohs(imsg.im_width);
    imsg.im_format = ntohs(imsg.im_format);

    imgdsize = (double) (imsg.im_height*imsg.im_width*(u_short)imsg.im_depth);
    net_assert((imgdsize > (double) LONG_MAX), "netimg_recvimsg: image too big");
    img_size = (long) imgdsize;                 // global

    ihdr_t ack;
    ack.ih_vers = NETIMG_VERS;
    ack.ih_type = NETIMG_ACK;
    ack.ih_seqn = htonl(NETIMG_SYNSEQ); 
    bytes=send(sd, &ack, sizeof(ihdr_t), 0);
    net_assert(bytes<0, "netimg_recvims: send ACK error");
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
  ihdr_t ihdr;  // memory to hold packet header
  int err = recv(sd, &ihdr, sizeof(ihdr_t), MSG_PEEK);
  if (err == -1 || ihdr.ih_vers != NETIMG_VERS)
    return;  

  int segsize = ntohs(ihdr.ih_size);
  unsigned int snd_next = ntohl(ihdr.ih_seqn);

  int datasize = mss - sizeof(ihdr_t) - NETIMG_UDPIP; // maximum bytes of a data or FEC packet
  int fec_num=(snd_next-fec_start)/(fwnd*datasize); // the number of FEC windows passed

  struct iovec iov[NETIMG_NUMIOV];
  iov[0].iov_base = &ihdr;
  iov[0].iov_len = sizeof(ihdr_t);
  struct msghdr mh;
  mh.msg_name = NULL;
  mh.msg_namelen = 0;
  mh.msg_iov = iov;
  mh.msg_iovlen = NETIMG_NUMIOV;
  mh.msg_control = NULL;
  mh.msg_controllen = 0;

  ihdr_t ack;
  ack.ih_vers = NETIMG_VERS;
  ack.ih_type = NETIMG_ACK;

  if (ihdr.ih_type == NETIMG_DATA)
  {
    iov[1].iov_base = image+snd_next;;
    iov[1].iov_len = segsize;

    fprintf(stderr, "netimg_recvimg: received offset 0x%x, %d bytes, waiting for 0x%x\n",
                                       snd_next, segsize, fec_next);     
    if (recvmsg(sd, &mh, 0) == -1)
    {
      close(sd);
      fprintf(stderr, "recv img error");
      exit(1);
    }
    if(mode)
    {
      if(fec_next==snd_next) // take the client out of go back N mode
      {
        fprintf(stderr, "netimg_recvims: out of gbn.\n");
        mode=false;

        fec_count++;
        fec_next=snd_next+segsize;
        fec_lost=fec_next;
      }
    }

    else
    {
      /* --------------- at least one FEC window lost --------------- */
      if(fec_num>0) 
      {
        //(1): if we have received FEC window full of consecutive data 
        if(fec_count==fwnd)
        {
          fec_start+=fwnd*datasize; 
          fec_lost=fec_start;

          if(snd_next==fec_next) // no data segment missing within next FEC window
          { 
            fec_next=snd_next+segsize;
            fec_lost=fec_next;
            fec_count=1;
          }
          else if(snd_next==fec_next+segsize) // missing the first data segment in the next FEC window       
          {
            fec_next=snd_next+segsize;
            fec_count=1;
          }
          else // missing more than one consecutive data segement in the next FEC window, trigger go back N mode
          {
            fprintf(stderr, "in gbn: consecutive losses or fec loss.\n");
            mode=true;

            fec_count=0;
            fec_next=fec_lost;
          }
        }
        //(2): the previous FEC window lost at least one data segment, unable to recover, trigger go back N mode
        else
        {
          fprintf(stderr, "in gbn: consecutive losses or fec loss.\n");
          mode=true;

          fec_start=fec_lost;
          fec_next=fec_lost;
          fec_count=0;
        }
      }
      /* --------------- no indication of FEC window lost, within same FEC window --------------- */
      else
      {
        if(snd_next==fec_next) // if it is the expected data segment
        {
          fec_next=snd_next+segsize;
          fec_count++;
          if(snd_next==fec_lost)
            fec_lost=fec_next;
        }
        else if(snd_next==fec_next+segsize) // if only one consecutive data segment loss
        {
          fec_next=snd_next+segsize;
          fec_count++;
        }
        else // if more than one consecutive data segment loss
        {
          fprintf(stderr, "in gbn: consecutive losses or fec loss.\n");
          mode=true;

          fec_start=fec_lost;
          fec_next=fec_lost;
          fec_count=0;
        }
      }
    }
    ack.ih_seqn=htonl(fec_lost);
  } 

  else if (ihdr.ih_type == NETIMG_FEC) // FEC pkt
  { 
    unsigned char FEC[datasize];
    iov[1].iov_base = FEC;
    iov[1].iov_len = datasize;

    if(recvmsg(sd, &mh, 0)==-1)
    {
      close(sd);
      fprintf(stderr, "recv img error");
      exit(1);
    }
    fprintf(stderr, "netimg_recvimg: received FEC offset: 0x%x, start: 0x%x, lost: 0x%x, count: %d\n", snd_next, fec_start, (fec_count>=fwnd) ? snd_next:fec_lost, fec_count);

    if(!mode)
    {
      //(1): lost one singel packet within this FEC window range, patch the lost one
      if(fec_count==fwnd-1) // reconstruct the lost packet
      {
        for (unsigned int j=fec_start; j<snd_next; j += datasize)
          if (j!=fec_lost)
            fec_accum(FEC, image+j, datasize, std::min(datasize, (int)img_size-(int)j));
        memcpy(image+fec_lost, FEC, min(datasize, (int)img_size-(int)fec_lost));
        fprintf(stderr, "netimg_recvimg: FEC patched offset: 0x%x, start: 0x%x, count: %d\n", snd_next, fec_start, fec_count);

        fec_start=snd_next;
        fec_next=snd_next;
        fec_count = 0;
        fec_lost=fec_start;
        ack.ih_seqn=htonl(fec_lost);
      }
      //(2): lost no packet within this FEC window range, throw away the FEC data, no ACK
      else if(fec_count==fwnd)
      {
        fec_start=snd_next;
        fec_next=snd_next;
        fec_count = 0;
        fec_lost=fec_start;
        return;
      }
      //(3): lost more than one packet within this FEC window lost, trigger go back N mode
      else
      {
        mode=true;
        fprintf(stderr, "netimg_recvimg: in gbn: multiple losses per fwnd.\n");

        fec_start=fec_lost;
        fec_next=fec_start;
        fec_count=0;
        ack.ih_seqn=htonl(fec_lost);
 
        return;
      } 
    }

    else // if in go back N mode and receives a FEC window packet, do nothing
      return;
  } 
  else 
  {  // NETIMG_FIN pkt
    /* must recv here because of MSG_PEEK recv before!!, or would be to infinite loop!! */
    recv(sd, &ihdr, sizeof(ihdr_t), 0); 
    ack.ih_seqn=htonl(NETIMG_FINSEQ);
  }

  if (((float) random())/INT_MAX < pdrop)
    fprintf(stderr, "netimg_recvimg: ack dropped 0x%x\n", ntohl(ack.ih_seqn));
  else
  {
    err = send(sd, &ack , sizeof(ihdr_t), 0);
    net_assert(err<0, "send ACK error");
    fprintf(stderr, "netimg_recvimg: ack sent 0x%x\n", ntohl(ack.ih_seqn));
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
  fec_count=0;
  fec_start=0;
  fec_next=0;
  fec_lost=0;
  mode=false;

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
      int nonblocking = 1;
      ioctl(sd, FIONBIO, &nonblocking);

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
