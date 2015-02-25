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
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>        // assert()
#include <limits.h>        // LONG_MAX
#include <iostream>
#include <iomanip>         // setw()
#include <fstream>
using namespace std;
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>      // socklen_t
#include "wingetopt.h"
#else
#include <string.h>
#include <unistd.h>
#include <signal.h>        // signal()
#include <sys/types.h>     // u_short
#include <sys/socket.h>    // socket API, setsockopt(), getsockname()
#include <netinet/in.h>
#endif
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#include "ltga.h"
#include "netimg.h"
#include "socks.h"
#include "hash.h"
#include "imgdb.h"

imgdb::
imgdb()
{
  IDrange[IMGDB_IDRBEG] = 0;
  IDrange[IMGDB_IDREND] = 0;
  nimages = 0;
  bloomfilter = 0L;

  sd = socks_servinit((char *) "imgdb", &self, sname);
}

/*
 * args: parses command line interface (CLI) arguments.
 *
 * Returns 0 on success or 1 on failure.  On successful return,
 * IDrange[IMGDB_IDRBEG], and/or IDrange[IMGDB_IDREND]
 * would have been initialized to the values specified in the command
 * line.  Nothing else is modified.
 */
int imgdb::
args(int argc, char *argv[])
{
  char c;
  int val;
  extern char *optarg;

  while ((c = getopt(argc, argv, "b:e:")) != EOF) {
    switch (c) {
    case 'b':
      val = (unsigned char ) atoi(optarg);
      net_assert((val < 0 || val > HASH_IDMAX),
                 "imgdb::args: beginID out of range");
      IDrange[IMGDB_IDRBEG] = (unsigned char) val;
      break;
    case 'e':
      val = (unsigned char ) atoi(optarg);
      net_assert((val < 0 || val > HASH_IDMAX),
                 "imgdb::args: endID out of range");
      IDrange[IMGDB_IDREND] = (unsigned char) val;
      break;
    default:
      return(1);
      break;
    }
  }

  return(0);
}

/*
 * loadimg:
 * load the image associate with "fname" into db.
 * "md" is the SHA1 output computed over fname and 
 * "id" is the id computed from md.
 * The Bloom Filter is also updated after the image is loaded.
*/
void imgdb::
loadimg(unsigned char id, unsigned char *md, char *fname)
{
  string pathname;
  fstream img_fs;

  /* first check if the file can be opened, to that end, we need to constuct
     the path name first, e.g., "images/ShipatSea.tga".
  */
  pathname = IMGDB_FOLDER;
  pathname = pathname+IMGDB_DIRSEP+fname;
  img_fs.open(pathname.c_str(), fstream::in);
  net_assert(img_fs.fail(), "imgdb::loadimg: fail to open image file");
  img_fs.close();

  /* if the file can be opened, store the image name, without the folder name,
     into the database */
  strcpy(db[nimages].img_name, fname);

  /* store its ID also */
  db[nimages].img_ID = id;

  /* update the bloom filter to record the presence of the 
     image in the DB. The function bfIDX() is defined in hash.cpp. */
  bloomfilter |= (1L << (int) bfIDX(BFIDX1, md)) |
    (1L << (int) bfIDX(BFIDX2, md)) | (1L << (int) bfIDX(BFIDX3, md));

  nimages++;

  return;
}

/*
 * loaddb(): load the image database with the ID and name of all
 * images whose ID are within the ID range of this node.  See inline
 * comments below
 */
void imgdb::
loaddb()
{
  fstream list_fs, img_fs;
  char fname[NETIMG_MAXFNAME];
  string pathname;
  unsigned char id, md[SHA1_MDLEN];

  /* IMGDB_FOLDER contains the name of the folder where the image
     files are, e.g., "images".  We assume there's a file in that
     folder whose name is specified by IMGDB_FILELIST, e.g.,
     "FILELIST.txt".  To open and read this file, we first construct
     its path name relative to the current directory, e.g.,
     "images/FILELIST.txt".
  */
  pathname = IMGDB_FOLDER;
  pathname = pathname+IMGDB_DIRSEP+IMGDB_FILELIST;
  list_fs.open(pathname.c_str(), fstream::in);
  net_assert(list_fs.fail(), "imgdb::loaddb: fail to open FILELIST.txt.");

  /* After FILELIST.txt is open for reading, we parse it one line at a
     time, each line is assumed to contain the name of one image file.
  */
  cerr << "Loading DB IDs in (" << (int) IDrange[IMGDB_IDRBEG] <<
    ", " << (int) IDrange[IMGDB_IDREND] << "]\n";
  do {
    list_fs.getline(fname, NETIMG_MAXFNAME);
    if (list_fs.eof()) break;
    net_assert(list_fs.fail(),
               "imgdb::loaddb: image file name longer than NETIMG_MAXFNAME");

    /* for each image, we compute a SHA1 of its file name, without
       the image folder path. The SHA1() function is part of the
       cryto/openssl library */
    SHA1((unsigned char *) fname, strlen(fname), md);

    /* from the SHA1 message digest (md), we compute an object ID */
    id = ID(md);  // see hash.cpp
    cerr << "  (" << setw(3) << (int) id << ") " << fname;

    /* if the object ID is in the range of this node, add its ID and
       name to the database and update the Bloom Filter. */
    if (ID_inrange(id, IDrange[IMGDB_IDRBEG],
                   IDrange[IMGDB_IDREND])) {  // Task 1 in hash.cpp
      cerr << " *in range*";
      loadimg(id, md, fname);
    }
    cerr << endl;
  } while (nimages < IMGDB_MAXDBSIZE);

  cerr << nimages << " images loaded." << endl;
  if (nimages == IMGDB_MAXDBSIZE) {
    cerr << "Image DB full, some image could have been left out." << endl;
  }
  cerr << endl;
  
  list_fs.close();
  
  return;
}

/*
 * reloaddb:
 * reload the db with only images whose IDs are in (begin, end].
 * Clear the database of cached images and reset the Bloom Filter
 * to represent the new set of images.
 */
void imgdb::
reloaddb(unsigned char begin, unsigned char end)
{
  IDrange[IMGDB_IDRBEG] = begin;
  IDrange[IMGDB_IDREND] = end;
  nimages = 0;
  bloomfilter = 0L;
  loaddb();
}

/*
 * searchdb(imgname): search for imgname in the DB.  To search for the
 * imagename, first compute its SHA1, then compute its object ID from
 * its SHA1.  Next check whether there is a hit for the image in the
 * Bloom Filter.  If it is a miss, return 0.  Otherwise, search the
 * database for a match to BOTH the image ID and its name (so a hash
 * collision on the ID is resolved here).  If a match is found, return
 * IMGDB_FOUND, otherwise return IMGDB_MISS if there's a Bloom Filter
 * miss else IMGDB_FALSE.
*/
int imgdb::
searchdb(char *imgname)
{
  int i;
  unsigned char id = 0;

  if (!imgname || !imgname[0]) {
    return(0);
  }
  
  /* Task 2:
   * Compute SHA1 and object ID.
   * See how both are done in loaddb().
   * Then check Bloom Filter for a hit or miss.
   * See how this is done in loadimg().
   * If Bloom Filter misses, return IMGDB_MISS.
   */
  /* YOUR CODE HERE */
  unsigned char md[SHA1_MDLEN];
  SHA1((unsigned char *)imgname, strlen(imgname), md);
  id = ID(md); 
  if (!(bloomfilter & (1L << (int) bfIDX(BFIDX1, md))) || 
      !(bloomfilter & (1L << (int) bfIDX(BFIDX2, md))) ||
      !(bloomfilter & (1L << (int) bfIDX(BFIDX3, md))))
    return 0;
  
  /* To get here means that you've got a hit at the Bloom Filter.
   * Search the DB for a match to BOTH the image ID and name.
  */
  for (i = 0; i < nimages; i++) {
    if ((id == db[i].img_ID) && !strcmp(imgname, db[i].img_name)) {
      readimg(imgname);
      return(IMGDB_FOUND);  // found
    }
  }

  return(IMGDB_FALSE); // false positive
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

  return((double) (curimg.GetImageWidth() *
                   curimg.GetImageHeight() *
                   (curimg.GetPixelDepth()/8)));
}
  
/*
 * recvqry: receive an iqry_t packet and store it in the
 * provided variable "iqry".  The type iqry_t is defined in netimg.h.
 * Check incoming packet and if packet is of unexpected size or if one
 * of the field contains an unexpected value, return one of
 * NETIMG_ESIZE, NETIMG_EVERS, or NETIMG_ETYPE.  If the image name is
 * not null terminated, return NETIMG_ENAME.  Otherwise, return 0.
 * Nothing else is modified.
 */
char imgdb::
recvqry(int td, iqry_t *iqry)
{
  int bytes;
  
  bytes = recv(td, (char *) iqry, sizeof(iqry_t), 0);
  net_assert((bytes <= 0), "imgdb::recvqry: recv");

  if (bytes != sizeof(iqry_t)) {
    return (NETIMG_ESIZE);
  }
  if (iqry->iq_vers != NETIMG_VERS) {
    return(NETIMG_EVERS);
  }
  if (iqry->iq_type != NETIMG_QRY) {
    return(NETIMG_ETYPE);
  }
  if (strlen((char *) iqry->iq_name) >= NETIMG_MAXFNAME) {
    return(NETIMG_ENAME);
  }

  return(0);
}

/*
 * sendimg: send the image to the client. First send the
 * specifics of the image (width, height, etc.) in an imsg_t packet to
 * the client.  The data type imsg_t is defined in netimg.h.  Assume
 * that other than im_vers and im_type, the other fields of imsg_t are
 * already correctly filled, but integers are still in host byte
 * order, so must convert integers to network byte order before
 * transmission.  If "image" is not NULL, send the image.  Otherwise,
 * send only the imsg_t packet (assuming that im_found field has been
 * properly set).  For debugging purposes, if image is not NULL, it is
 * sent in chunks of imgsize/numseg instead of as one single image,
 * one chunk for every NETIMG_USLEEP microseconds.  If image is not
 * NULL, "numseg" must be >= 1.
 *
 * Terminate process upon encountering any error.
 * Doesn't modify anything else.
*/
void imgdb::
sendimg(int td, imsg_t *imsg, char *image, long imgsize, int numseg)
{
  int segsize;
  char *ip;
  int bytes;
  long left;

  imsg->im_vers = NETIMG_VERS;
  imsg->im_type = NETIMG_RPY;
  imsg->im_width = htons(imsg->im_width);
  imsg->im_height = htons(imsg->im_height);
  imsg->im_format = htons(imsg->im_format);

  bytes = send(td, (char *) imsg, sizeof(imsg_t), 0);
  net_assert((bytes != sizeof(imsg_t)), "imgdb::sendimg: send imsg");

  if (image) {
    /* 
     * Send the imsg packet to client connected to socket td.
     */
    net_assert((numseg < 1), "imgdb::sendimg: numseg < 1");
    segsize = imgsize/numseg;       /* compute segment size */
    segsize = segsize < NETIMG_MSS ? NETIMG_MSS : segsize;
    /* but don't let segment be too small */

    ip = image;   /* ip points to the start of image byte buffer */
    
    for (left = imgsize; left; left -= bytes) {
      // "bytes" contains how many bytes was sent at the last
      // iteration.

      /* Send one segment of data of size segsize at each iteration.
       * The last segment may be smaller than segsize
       */
      bytes = send(td, (char *) ip, segsize > left ? left : segsize, 0);
      net_assert((bytes < 0), "imgdb::sendimg: send image");
      ip += bytes;
      
      fprintf(stderr, "imgdb::sendimg: size %d, sent %d\n", (int) left, bytes);
      usleep(NETIMG_USLEEP);
    }
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
  char err;
  int td, found;
  iqry_t iqry;
  imsg_t imsg;
  double imgdsize;

  td = socks_accept(sd, 1);
  err = recvqry(td, &iqry);
  if (err) {
    imsg.im_found = err;
    sendimg(td, &imsg, NULL, 0, 0);
  } else {
    
    imsg.im_found = NETIMG_NFOUND;
    found = searchdb(iqry.iq_name);
    if (found == IMGDB_FALSE) {
      cerr << "imgdb: " << iqry.iq_name <<
        ": Bloom filter false positive." << endl;
    } else if (found == IMGDB_MISS) {
      cerr << "imgdb: " << iqry.iq_name <<
        ": Bloom filter miss." << endl;
    } else { 
      imsg.im_found = NETIMG_FOUND;
      imgdsize = marshall_imsg(&imsg);
      net_assert((imgdsize > (double) LONG_MAX), "imgdb: image too big");
    }
    
    if (imsg.im_found == NETIMG_FOUND) {
      sendimg(td, &imsg, getimage(), (long)imgdsize, NETIMG_NUMSEG);
    } else {
      sendimg(td, &imsg, NULL, 0, 0);
    }
  }
  socks_close(td);

  return;
}

#ifdef LAB3
static void
usage(char *progname)
{
  fprintf(stderr, "Usage: %s [ -b <beginID> -e <endID> ]\n", progname); 
}

int
main(int argc, char *argv[])
{ 
  socks_init();
  
  imgdb imgdb;

  // parse args, see the comments for imgdb::args()
  if (imgdb.args(argc, argv)) {
    usage(argv[0]);
    exit(1);
  }
  imgdb.loaddb();

  while (1) {
    imgdb.handleqry();
  }

#ifdef _WIN32
  WSACleanup();
#endif // _WIN32

  exit(0);
}
#endif // LAB3

