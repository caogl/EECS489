/*
 * Copyright (c) 2015 University of Michigan, Ann Arbor.
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
 * Authors: Sugih Jamin (jamin@eecs.umich.edu)
 *
*/
#include <stdio.h>         // fprintf(), perror(), fflush()
#include <stdlib.h>        // atoi()
#include <assert.h>        // assert()
#include <limits.h>        // LONG_MAX
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

/*
 * imgdb_loadimg: load TGA image from file *fname to *image.
 * Store size of image, in bytes, in *img_size.
 * Initialize *imsg with image's specifics.
 * All four variables must point to valid memory allocated by caller.
 * Terminate process on encountering any error.
 * Returns NETIMG_FOUND if *fname found, else returns NETIMG_NFOUND.
 */
int
imgdb_loadimg(char *fname, LTGA *image, imsg_t *imsg, long *img_size)
{
  int alpha, greyscale;
  double img_dsize;
  
  imsg->im_vers = NETIMG_VERS;

  image->LoadFromFile(fname);

  if (!image->IsLoaded()) {
    imsg->im_found = NETIMG_NFOUND;
  } else {
    imsg->im_found = NETIMG_FOUND;

    cout << "Image: " << endl;
    cout << "     Type   = " << LImageTypeString[image->GetImageType()] 
         << " (" << image->GetImageType() << ")" << endl;
    cout << "     Width  = " << image->GetImageWidth() << endl;
    cout << "     Height = " << image->GetImageHeight() << endl;
    cout << "Pixel depth = " << image->GetPixelDepth() << endl;
    cout << "Alpha depth = " << image->GetAlphaDepth() << endl;
    cout << "RL encoding = " << (((int) image->GetImageType()) > 8) << endl;
    /* use image->GetPixels()  to obtain the pixel array */
    
    img_dsize = (double) (image->GetImageWidth()*image->GetImageHeight()*(image->GetPixelDepth()/8));
    net_assert((img_dsize > (double) LONG_MAX), "imgdb: image too big");
    *img_size = (long) img_dsize;
    
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
    
  return(imsg->im_found);
}
  
/*
 * imgdb_sockinit: sets up a TCP socket listening for connection.
 * Let the call to bind() assign an ephemeral port to this listening socket.
 * Determine and print out the assigned port number to screen so that user
 * would know which port to use to connect to this server.
 *
 * Terminates process on error.
 * Returns the bound socket id.
*/
int imgdb_sockinit()
{
  int sd;
  struct sockaddr_in self;
  char sname[NETIMG_MAXFNAME+1] = { 0 };

  /* Task 2: YOUR CODE HERE
   * Fill out the rest of this function.
  */
  /* create a TCP socket, store the socket descriptor in global variable "sd" */
  /* YOUR CODE HERE */
  sd = socket(AF_INET, SOCK_STREAM,IPPROTO_TCP);
  if(sd<0)
  {
    perror("error in creating socket");
    exit(1);
  }

  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  self.sin_family = AF_INET;
  self.sin_addr.s_addr = INADDR_ANY;
  self.sin_port = 0;

  /* bind address to socket */
  /* YOUR CODE HERE */
  bind(sd, (struct sockaddr *) &self, sizeof(struct sockaddr_in));

  /* listen on socket */
  /* YOUR CODE HERE */
  listen(sd, NETIMG_QLEN);

  /*
   * Obtain the ephemeral port assigned by the OS kernel to this
   * socket and store it in the local variable "self".
   */
  /* YOUR CODE HERE */
  struct sockaddr_in tmp;
  int len = sizeof(sockaddr_in);
  int status = getsockname(sd, (struct sockaddr *)&tmp, (socklen_t *)&len);
  if(status<0){
    perror("error getsockname");
    exit(1);
  }
  self.sin_port = tmp.sin_port;

  /* Find out the FQDN of the current host and store it in the local
     variable "sname".  gethostname() is usually sufficient. */
  /* YOUR CODE HERE */
  status = gethostname(sname, NETIMG_MAXFNAME+1);
  if(status<0){
    perror("error gethostname");
    exit(1);
  }

  /* inform user which port this peer is listening on */
  fprintf(stderr, "imgdb address is %s:%d\n", sname, ntohs(self.sin_port));

  return sd;
}

/*
 * imgdb_accept: accepts connection on the given socket, sd.
 *
 * On connection, set the linger option for NETIMG_LINGER to
 * allow data to be delivered to client.  Return the descriptor
 * of the connected socket.
 * Terminates process on error.
*/
int imgdb_accept(int sd)
{
  int td;
  struct sockaddr_in client;
  struct hostent *cp;

  /* Task 2: YOUR CODE HERE
   * Fill out the rest of this function.
   * Accept the new connection.
   * Use the variable "td" to hold the new connected socket.
  */
  /* YOUR CODE HERE */
  int len = sizeof(struct sockaddr_in);
  td = accept(sd, (struct sockaddr *)&client, (socklen_t *)&len);
  if(td<0)
  {
    perror("error in accepting");
    exit(1);
  }

  /* make the socket wait for NETIMG_LINGER time unit to make sure
     that all data sent has been delivered when closing the socket */
  /* YOUR CODE HERE */
  struct linger linger_setup;
  linger_setup.l_onoff = 1;
  linger_setup.l_linger = NETIMG_LINGER;
  int status = setsockopt(td, SOL_SOCKET, SO_LINGER, &linger_setup, sizeof(struct linger));
  if(status<0)
  {
    perror("error in setsockopt");
    exit(1);
  }

  /* inform user of connection */
  cp = gethostbyaddr((char *) &client.sin_addr, sizeof(struct in_addr), AF_INET);
  fprintf(stderr, "Connected from client %s:%d\n",
          ((cp && cp->h_name) ? cp->h_name : inet_ntoa(client.sin_addr)),
          ntohs(client.sin_port));

  return td;
}

/* 
 * Task 2:
 * imgdb_recvqry: receive an iqry_t packet and check that the incoming
 * iqry_t packet is of version NETIMG_VERS.  If so, copy the image name
 * to the "fname" argument, which must point to pre-allocated space.
 * If the version number is wrong, the content of "*fname" is not modified.
 *
 * Terminate process if error encountered when receiving packet
 * or if packet is of the wrong version.
 *
 * Nothing else is modified.
*/
void
imgdb_recvqry(int td, char *fname)
{
  /* YOUR CODE HERE */
  int bytes;
  iqry_t iqry;

  bytes = recv(td, (char *)&iqry, sizeof(iqry_t), 0);
  if (bytes<0 || iqry.iq_vers!= NETIMG_VERS){
    perror("error in recv or wrong vers");
    exit(1);
  }
  strcpy(fname, iqry.iq_name);

  return;
}

/*
 * imgdb_sendimg: send the image to the client
 * First send the specifics of the image (width, height, etc.)
 * contained in *imsg to the client.  *imsg must have been
 * initialized by caller.
 * Then send the image contained in *image, but for future
 * debugging purposes we're going to send the image in
 * chunks of segsize instead of as one single image.
 * We're going to send the image slowly, one chunk for every
 * NETIMG_USLEEP microseconds.
 *
 * Terminate process upon encountering any error.
 * Doesn't otherwise modify anything.
*/
void imgdb_sendimg(int td, imsg_t *imsg, LTGA *image, long img_size)
{
  int segsize;
  char *ip;
  int bytes;
  long left;

  /* Task 2: YOUR CODE HERE
   * Send the imsg packet to client connected to socket td.
   */
  /* YOUR CODE HERE */
  bytes = send(td, imsg, sizeof(imsg_t), 0);
  if(bytes<0)
  {
    perror("error in sending");
    exit(1);
  }

  if (image) 
  {
    segsize = img_size/NETIMG_NUMSEG;                     /* compute segment size */
    segsize = segsize < NETIMG_MSS ? NETIMG_MSS : segsize; /* but don't let segment be too small*/

    ip = (char *) image->GetPixels();    /* ip points to the start of byte buffer holding image */
    
    for (left = img_size; left; left -= bytes) {  // "bytes" contains how many bytes was sent
      // at the last iteration.

      /* Task 2: YOUR CODE HERE
       * Send one segment of data of size segsize at each iteration.
       * The last segment may be smaller than segsize
       */
      /* YOUR CODE HERE */
      if(segsize>left)
      {
        segsize = left;
      }
      bytes = send(td, ip+img_size-left, segsize, 0);
      if(bytes<0)
      {
        perror("error in sending");
        exit(1);
      }

      fprintf(stderr, "imgdb_send: size %d, sent %d\n", (int) left, bytes);
      usleep(NETIMG_USLEEP);
    }
  }

  return;
}

int main(int argc, char *argv[])
{ 
  int sd, td;
  LTGA image;
  imsg_t imsg;
  long img_size;
  char fname[NETIMG_MAXFNAME] = { 0 };

  sd = imgdb_sockinit();  // Task 2
  
  while (1) 
  {
    td = imgdb_accept(sd);  // Task 2
    imgdb_recvqry(td, fname); // Task 2
    if (imgdb_loadimg(fname, &image, &imsg, &img_size) == NETIMG_FOUND) {
      imgdb_sendimg(td, &imsg, &image, img_size); // Task 2
    } else {
      imgdb_sendimg(td, &imsg, NULL, 0);
    }
    close(td);
  }

  exit(0);
}
