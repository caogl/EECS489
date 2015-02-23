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
#ifndef __DHTN_H__
#define __DHTN_H__

#include "hash.h"
#include "netimg.h"
#include "imgdb.h"

#define DHTN_UNITTESTING 0

#define DHTN_UNINIT_SD -1
#define DHTN_FINGERS 8  // reaches half of 2^8-1
                        // with integer IDs, fingers[0] is immediate successor

#define DHTM_VERS  0x2
#define DHTM_TTL   10
#define DHTM_QRY NETIMG_QRY  // 0x01
#define DHTM_RPY NETIMG_RPY  // 0x02
#define DHTM_JOIN  0x08
#define DHTM_WLCM  0x04
#define DHTM_REID  0x0c
#define DHTM_REDRT 0x40
#define DHTM_ATLOC 0x80

// the following are used in PA2
#define DHTM_SRCH 0x10   // image search on the DHT
#define DHTM_RPLY 0x20   // reply to image search on the DHT
#define DHTM_MISS 0x22   // image not found on the DHT 

typedef struct {            // inherit from struct sockaddr_in
  unsigned char dhtn_rsvd;  // == sizeof(sin_len)
  unsigned char dhtn_ID;    // == sizeof(sin_family)
  u_short dhtn_port;        // port#, always stored in network byte order
  struct in_addr dhtn_addr; // IPv4 address
} dhtnode_t;

typedef struct {
  unsigned char dhtm_vers;  // must be DHTM_VERS
  unsigned char dhtm_type;  // one of DHTM_{REDRT,JOIN,REID}
  u_short dhtm_ttl;         // currently used only by JOIN message
  dhtnode_t dhtm_node;      // REDRT: new successor
                            // JOIN: node attempting to join DHT
} dhtmsg_t;

typedef struct {
  unsigned char dhtm_vers;  // must be DHTM_VERS
  unsigned char dhtm_type;  // DHTM_WLCM
  u_short dhtm_ttl;         // reserved
  dhtnode_t dhtm_node;      // WLCM: successor node
  dhtnode_t dhtm_pred;      // WLCM: predecessor node 
} dhtwlcm_t;

typedef struct {            // PA2
  dhtmsg_t dhts_msg;                
  unsigned char dhts_imgID;
  char dhts_name[NETIMG_MAXFNAME];
} dhtsrch_t;                // used by QUERY, REPLY, and MISS

class dhtn {
  char *kenname;      // known node's address
  u_short kenport;    // known host's port
  dhtnode_t self;
  char sname[NETIMG_MAXFNAME];

  dhtnode_t finger[DHTN_FINGERS+1]; // finger[0] is immediate successor
                       // finger[DHTN_FINGERS] is immediate predecessor

  void first(); // first node on ring
  void join();
  void setID(int ID);
  void reID();
  void handlejoin(int sender, dhtmsg_t *dhtmsg);

  void forward(unsigned char id, dhtmsg_t *dhtmsg, int size);


public:
  int sd;             // dht network listen socket
  imgdb dhtn_imgdb;
  
  dhtn(int id, char *name, u_short port); // default constructor
  void handlepkt();
  void printIDs() {
    // prints out node's, successor's, and predecessor's IDs
    fprintf(stderr, "Node ID: %d, pred: %d, succ: %d\n", self.dhtn_ID,
            finger[DHTN_FINGERS].dhtn_ID, finger[0].dhtn_ID);
  }
};  

#endif /* __DHTN_H__ */
