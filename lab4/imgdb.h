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
#ifndef __IMGDB_H__
#define __IMGDB_H__

#include "ltga.h"
#include "hash.h"

#ifdef _WIN32
#define IMGDB_DIRSEP "\\"
#else
#define IMGDB_DIRSEP "/"
#endif
#define IMGDB_FOLDER    "images"
#define IMGDB_FILELIST  "FILELIST.txt"
#define IMGDB_IDRBEG 0
#define IMGDB_IDREND 1
#define IMGDB_MAXDBSIZE 1024 // DB can only hold 1024 images max

// used in PA2
#define IMGDB_FOUND    1
#define IMGDB_FALSE   -1
#define IMGDB_MISS     0
#define IMGDB_NETMISS -2

typedef struct {
  unsigned char img_ID;
  char img_name[NETIMG_MAXFNAME];
} image_t;
   
class imgdb {
  struct sockaddr_in self;
  char sname[NETIMG_MAXFNAME];
  unsigned char IDrange[2];     // (start, end]
  unsigned long bloomfilter;    // 64-bit bloom filter
  int nimages;
  image_t db[IMGDB_MAXDBSIZE];
  LTGA curimg;

public:
  int sd;  // image socket

  imgdb(); // default constructor

  int args(int argc, char *argv[]);

  void loaddb();
  void reloaddb(unsigned char begin, unsigned char end);
  int searchdb(char *imgname);
  char *getimage() { return((char * ) curimg.GetPixels()); }

  // For cache maintenance (PA2)
  // loadimg: load a single, cached image to imagedb
  void loadimg(unsigned char id, unsigned char *md, char *fname);
  // readimg: after the image is loaded to the imagdb, actually read
  //          the image from file to memory, given pathname relative
  //          to current working directory.
  void readimg(char *imgname) {
    string pathname=IMGDB_FOLDER;
    curimg.LoadFromFile(pathname+IMGDB_DIRSEP+imgname);
  }

  // image query-reply
  void handleqry();
  char recvqry(int td, iqry_t *iqry);
  double marshall_imsg(imsg_t *imsg);
  void sendimg(int td, imsg_t *imsg, char *image, long imgsize, int numseg);
};  

#endif /* __IMGDB_H__ */
