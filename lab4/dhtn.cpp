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

#include "netimg.h"
#include "socks.h"
#include "hash.h"
#include "dhtn.h"

void
dhtn_usage(char *progname)
{
  fprintf(stderr, "Usage: %s [ -p <nodename>:<port> -I nodeID ]\n", progname);
}

/*
 * dhtn_args: parses command line args.
 *
 * Returns 0 on success or 1 on failure.  On successful return, the
 * provided known node's FQDN, if any, is pointed to by "pname" and
 * "port" points to the port to connect at the known node, in
 * network byte order.  If the optional -I option is present, the
 * provided ID is copied into the space pointed to by "id".  The
 * variables "port" and "id" must be allocated by caller.
 * 
 * Nothing else is modified.
 */
int
dhtn_args(int argc, char *argv[], char **pname, u_short *port, int *id)
{
  char c, *p;
  extern char *optarg;

  net_assert(!pname, "dhtn_args: pname not allocated");
  net_assert(!port, "dhtn_args: port not allocated");
  net_assert(!id, "dhtn_args: id not allocated");

  *id = ((int) HASH_IDMAX)+1;

  while ((c = getopt(argc, argv, "p:I:")) != EOF) {
    switch (c) {
    case 'p':
      for (p = optarg+strlen(optarg)-1;     // point to last character
                                            // of addr:port arg
           p != optarg && *p != NETIMG_PORTSEP; // search for ':' separating
                                                // addr from port
           p--);
      net_assert((p == optarg), "dhtn_args: peer address malformed");
      *p++ = '\0';
      *port = htons((u_short) atoi(p)); // always stored in network
                                        // byte order

      net_assert((p-optarg > NETIMG_MAXFNAME), "dhtn_args: node name too long");
      *pname = optarg;
      break;
    case 'I':
      *id = atoi(optarg);
      net_assert((*id < 0 || *id > ((int) HASH_IDMAX)),
                 "dhtn_args: id out of range");
      break;
    default:
      return(1);
      break;
    }
  }

  return (0);
}

/*
 * setID: sets up a TCP socket listening for connection.  Let the call
 * to bind() assign an ephemeral port to this listening socket.
 * Determine and print out the assigned port number to screen so that
 * user would know which port to use to connect to this server.  Store
 * the host address and assigned port number to the member variable
 * "self".  If "id" given is valid, i.e., in [0, 255], store it as
 * self's ID, else compute self's id from SHA1.
 *
 * Terminates process on error.
 * Returns the bound socket id.
*/
void dhtn::
setID(int id)
{
#define SIZEOFADDRPORT 6
  char addrport[SIZEOFADDRPORT+1];
  unsigned char md[SHA1_MDLEN];

  sd = socks_servinit((char *) "dhtn", (struct sockaddr_in *) &self, sname);

  /* if id is not valid, compute id from SHA1 hash of address+port */
  if (id < 0 || id > (int) HASH_IDMAX) {
    memcpy(addrport, (char *) &self.dhtn_port, SIZEOFADDRPORT*sizeof(char));
    addrport[SIZEOFADDRPORT]='\0';
    SHA1((unsigned char *) addrport, SIZEOFADDRPORT*sizeof(char), md);
    self.dhtn_ID = ID(md);
  } else {
    self.dhtn_ID = (unsigned char) id;
  }

  /* inform user this node's ID */
  fprintf(stderr, "     node ID %d\n", self.dhtn_ID);

  return;
}

/*
 * reID: called when the dht tells us that our ID collides with that
 * of an existing node.  We simply closes the listen socket and call
 * setID() to grab a new ephemeral port and a corresponding new ID
*/
void dhtn::
reID()
{
  socks_close(sd);
  setID(((int) HASH_IDMAX)+1);
  return;
}

/*
 * first: node is the first node in the ID ring.  Set both predecessor
 * and successor (finger[0]) to be "self".
 */
void dhtn::
first()
{
  finger[DHTN_FINGERS] = finger[0] = self;
  dhtn_imgdb.loaddb();
}

/*
 * join: called ONLY by a node upon start up if name:port is specified
 * in the command line.  It sends a join message to the provided host.
 */
void dhtn::
join()
{
  int sd_tmp, err;
  dhtmsg_t dhtmsg;

  sd_tmp = socks_clntinit(NULL, kenname, kenport);

  /* send join message */
  dhtmsg.dhtm_vers = DHTM_VERS;
  dhtmsg.dhtm_type = DHTM_JOIN;
  dhtmsg.dhtm_ttl = htons(DHTM_TTL);
  // dhtmsg.dhtm_node = self;
  memcpy((char *) &dhtmsg.dhtm_node, (char *) &self, sizeof(dhtnode_t));

  err = send(sd_tmp, (char *) &dhtmsg, sizeof(dhtmsg_t), 0);
  net_assert((err != sizeof(dhtmsg_t)), "dhtn::join: send");
  
  socks_close(sd_tmp);

  return;
}

/*
 * dhtn default constructor.  If given id is valid, i.e., in [0, 255],
 * set self's ID to the given id, otherwise, compute an id from SHA1
 * Initially, both predecessor (finger[DHTN_FINGERS]) and successor
 * (finger[0]) are uninitialized (dhtn_port == 0).  Initialize member
 * variables kenname and kenport to provided values.
 * Create listen socket and, if known name and port given, join the DHT.
 */
dhtn::
dhtn(int id, char *pname, u_short port)
{
  kenname = pname;
  kenport = port;
  setID(id);

  if (pname) {
    finger[DHTN_FINGERS].dhtn_port = 0;
    finger[0].dhtn_port = 0;
    join();      // join DHT if known host given
  } else {
    first();     // else this is the first node on ID ring
  }
}

/*
 * forward: forward the message in "dhtmsg" along to the next node.
 *
 * Used to forward both dhtmsg_t and dhtsrch_t message.  In the first
 * case, the provided id is node ID for a join message.  In the second
 * case, it is the image ID of the queried image. The second case is
 * used only in PA2.  Since the second argument could be a pointer to
 * either a dhtmsg_t or a dhtsrch_t, the third argument records the
 * message size.
 *
 * Forward packet only if dhtmsg->dhtm_ttl > 0 after decrement.
 */
void dhtn::
forward(unsigned char id, dhtmsg_t *dhtmsg, int size)
{
  /* Task 2: */
  /* First check whether we expect the joining node's ID, as contained
     in the JOIN message, to fall within the range (self.dhtn_ID,
     finger[0].dhtn_ID].  If so, we inform the node we are sending
     the JOIN message to that we expect it to be our successor.  We do
     this by setting the highest bit in the type field of the message
     using DHTM_ATLOC.
  */
  /* After we've forwarded the message along, we don't immediately close
     the connection as usual.  Instead, we wait for any DHTM_REDRT message
     telling us that we have overshot in our range expectation (see
     the third case in dhtn::handlejoin()).  Such a message comes with
     a suggested new successor in the dhtm_node field of the DHTM_REDRT
     message, we copy this suggested new successor to our finger[0] 
     and try to forward the JOIN message again to the new successor. 
     We repeat this until we stop getting DHTM_REDRT message, in which
     case the connection would be closed by the other end and our
     call to recv() returns 0.
  */
  /* YOUR CODE HERE */
  int sd_tmp;
  int err;  int bytes = 0;
  dhtmsg_t tmp;

  if (ID_inrange(id, self.dhtn_ID, finger[0].dhtn_ID)){
    dhtmsg->dhtm_type |= DHTM_ATLOC;
  }
  sd_tmp = socks_clntinit(&finger[0].dhtn_addr, 0, finger[0].dhtn_port);
  err = send(sd_tmp, (char*) dhtmsg, sizeof(dhtmsg_t), 0);
  fprintf(stderr, "Forwarding packet %d\n", finger[0].dhtn_ID);

  while (1){
    err = recv(sd_tmp, (char *) &tmp+bytes, sizeof(dhtmsg_t), 0);
    if (err <= 0){
      close(sd_tmp);
      break;
    }
    
    bytes += err;
    if (bytes < (int) sizeof(dhtmsg_t)){
      continue;
    }
    close(sd_tmp);

    bytes = 0; 
    finger[0] = tmp.dhtm_node;

    sd_tmp = socks_clntinit(&finger[0].dhtn_addr, 0, finger[0].dhtn_port);
    err = send(sd_tmp, (char*) dhtmsg, sizeof(dhtmsg_t), 0);
    net_assert((err != sizeof(dhtmsg_t)), "dhtn::forward error"); 
    fprintf(stderr, "Forwarding packet %d\n", finger[0].dhtn_ID);
  }
  return;
}


/*
 * handlejoin: "td" is the socket descriptor of the connection with
 * the node from which you received a JOIN message.  It may not be the
 * node who initiated the join request.  Close it as soon as possible
 * to prevent deadlock.  "dhtmsg" is the join message that contains
 * the dhtnode_t of the node initiating the join attempt (henceforth,
 * the "joining node").
 */
void dhtn::
handlejoin(int td, dhtmsg_t *dhtmsg)
{
  /* Task 1: */
  /* First check if the joining node's ID collides with predecessor or
     self.  If so, send back to joining node a REID message.  See
     dhtn.h for packet format.
  */
  
  /* Otherwise, next check if ID is in range (finger[DHTN_FINGERS].dhtn_ID,
     self.dhtn_ID].  If so, send a welcome message to joining node,
     with the current node as the joining node's successor and the
     current node's predecessor as the joining node's predecessor.
     Again, see dhtn.h for packet format.  Next make the joining node
     the current node's new predecessor.  At this point, the current
     node's old predecessor is still pointing to the current node,
     instead of the joining node, as its successor.  This will be
     fixed "on demand" in the next case, in conjunction with the
     dhtn::forward() function.  If the current node were the
     first/only node in the identifier ring, as indicated by its ID
     being the same as that of its successor's ID, set both its
     successor and predecessor to the new joining node.

     With the joining node as the current node's predecessor, it means
     that the ID range of the current node has changed.  Call
     imgdb::reloaddb() with the new ID range to reload the image database.
  */
  /* Otherwise, next check if sender expects the joining node's ID to
     be in range even though it failed our own test.  Sender indicates
     its expectation by setting the highest order bit of the type
     field.  Thus whereas normal join message is of type DHTM_JOIN, if
     the sender expects the ID to be in our range, it will set the
     type to be (DHTM_ATLOC | DHTM_JOIN).  If so, sender expects the
     ID to be in our range, but it is not, it probably means that the
     sender's successor information has become inconsistent due to
     node being added to the DHT, in which case, send a DHTM_REDRT
     message to the SENDER.  Note that in the first two cases, we send
     the message to the joining node, but in this case, we send the
     message to the sender of the current JOIN packet.  Again, see
     dhtn.h for packet format.
  */
  /* Finally, if none of the above applies, we forward the JOIN
     message to the next node, which in Lab 4 is just the successor
     node.  For programming assignment 2, we'll use the finger table
     to determine the next node to forward a JOIN request to.  You
     should call dhtn::forward() to perform the forwarding task.
     Don't forget to close the sender socket when you don't need it anymore.
  */
  /* YOUR CODE HERE */
  int sd_tmp;
  int ID = dhtmsg->dhtm_node.dhtn_ID;
  
  if(self.dhtn_ID == ID || finger[DHTN_FINGERS].dhtn_ID == ID){
    close(td);
    sd_tmp = socks_clntinit(&dhtmsg->dhtm_node.dhtn_addr, 0, dhtmsg->dhtm_node.dhtn_port);
    dhtmsg->dhtm_type = DHTM_REID;
    send(sd_tmp, (char*) dhtmsg, sizeof(dhtmsg_t), 0);
    close(sd_tmp);
  }
  else if (ID_inrange(ID, finger[DHTN_FINGERS].dhtn_ID, self.dhtn_ID)){
    close(td);
    dhtnode_t tmp = dhtmsg->dhtm_node;
    sd_tmp = socks_clntinit(&dhtmsg->dhtm_node.dhtn_addr, 0, dhtmsg->dhtm_node.dhtn_port);
    

    dhtmsg->dhtm_type = DHTM_WLCM;

    memcpy((char *) &dhtmsg->dhtm_node, (char *) &self, sizeof(dhtnode_t)); 
    send(sd_tmp, (char*) dhtmsg, sizeof(dhtmsg_t), 0); //send new dhtmsg with new successor updated.
    send(sd_tmp, (char*) &finger[DHTN_FINGERS], sizeof(dhtnode_t), 0); //
    close(sd_tmp);
    
    finger[DHTN_FINGERS] = tmp;
    if (self.dhtn_ID == finger[0].dhtn_ID){
	finger[0] = tmp;
    }
  }
  else if (dhtmsg->dhtm_type & DHTM_ATLOC){
    dhtmsg->dhtm_type = DHTM_REDRT;
    memcpy((char *) &dhtmsg->dhtm_node, (char *) &finger[DHTN_FINGERS], sizeof(dhtnode_t));
    send(td, (char*) dhtmsg, sizeof(dhtmsg_t), 0);
    close(td);
  }
  else {
    close(td);
    forward(dhtmsg->dhtm_node.dhtn_ID, dhtmsg, 0);
  }

  return;
}

/*
 * handlepkt: accept connection, then receive and parse packet.  First
 * accept connection on "sd", then receive a packet from the accept
 * socket. Depending on the packet type, call the appropriate packet
 * handler.
 */
void dhtn::
handlepkt()
{
  int td, err, bytes;
  dhtmsg_t dhtmsg;

  td = socks_accept(sd, 0);
  
  bytes = 0;
  do {
    /* receive packet from td */
    err = recv(td, ((char *) &dhtmsg)+bytes, sizeof(dhtmsg_t)-bytes, 0);
    if (err <= 0) { // connection closed or error
      socks_close(td);
      break;
    } 
    bytes += err;
  } while (bytes < (int) sizeof(dhtmsg_t));
  
  if (bytes == sizeof(dhtmsg_t)) {

    net_assert((dhtmsg.dhtm_vers != DHTM_VERS), "dhtn::join: bad version");

    if (dhtmsg.dhtm_type == DHTM_REID) {
      /* packet is of type DHTM_REID: an ID collision has occurred and
         we, the newly joining node, has been told to generate a new
         ID. We close the connection to the sender, calls reID(),
         which will close our listening socket and create a new one
         with a new ephemeral port.  We then generate a new ID from the
         new ephemeral port and try to join again. */
      net_assert(!kenname, "dhtn::handlepkt: received reID but no known node");
      fprintf(stderr, "Received REID from node ID %d\n",
              dhtmsg.dhtm_node.dhtn_ID);
      socks_close(td);
      reID();
      join();

    } else if (dhtmsg.dhtm_type & DHTM_JOIN) {
      net_assert(!(finger[DHTN_FINGERS].dhtn_port && finger[0].dhtn_port),
                 "dhtn::handlpkt: receive a JOIN when not yet integrated into the DHT.");
      fprintf(stderr, "Received JOIN (ttl: %d) from node ID %d\n",
              ntohs(dhtmsg.dhtm_ttl), dhtmsg.dhtm_node.dhtn_ID);
      handlejoin(td, &dhtmsg);

    } else if (dhtmsg.dhtm_type & DHTM_WLCM) {
      fprintf(stderr, "Received WLCM from node ID %d\n",
              dhtmsg.dhtm_node.dhtn_ID);

      // store successor node
      finger[0] = dhtmsg.dhtm_node;
      // store predecessor node
      err = recv(td, (char *) &finger[DHTN_FINGERS], sizeof(dhtnode_t), 0);
      net_assert((err <= 0), "dhtn::handlepkt: welcome recv pred");
      socks_close(td);
      
      /* reload database with new ID range */
      dhtn_imgdb.reloaddb(finger[DHTN_FINGERS].dhtn_ID, self.dhtn_ID);

    } else {
      net_assert((dhtmsg.dhtm_type & DHTM_REDRT),
                 "dhtn::handlepkt: overshoot message received out of band");
      socks_close(td);
    }
  }

  return;
}

int
main(int argc, char *argv[])
{
  char *name = NULL;
  u_short port;
  char c;
  int id, err, maxsd;
  fd_set rset;
 
  // parse args, see the comments for dhtn_args()
  if (dhtn_args(argc, argv, &name, &port, &id)) {
    dhtn_usage(argv[0]);
    exit(1);
  }
 
  socks_init();
  dhtn node(id, name, port);
 
  maxsd = (node.sd > node.dhtn_imgdb.sd ? node.sd : node.dhtn_imgdb.sd);
  do {
    /* set up and call select */
    FD_ZERO(&rset);
    FD_SET(node.dhtn_imgdb.sd, &rset);
    FD_SET(node.sd, &rset);
#ifndef _WIN32
    FD_SET(STDIN_FILENO, &rset); // wait for input from std input,
    // Winsock only works with socket and stdin is not a socket
#endif
     
    err = select(maxsd+1, &rset, 0, 0, 0);
    net_assert((err <= 0), "dhtn: select error");
     
#ifndef _WIN32
    if (FD_ISSET(STDIN_FILENO, &rset)) {
      // user input: if getchar() returns EOF or if user hits q, quit,
      // if user hits 'p', prints out node's, successor's, and
      // predecessor's IDs, flushes input.
      if (((c = getchar()) == EOF) || (c == 'q') || (c == 'Q')) {
        fprintf(stderr, "Bye!\n");
        return(0);
      } else if (c == 'p') {
        node.printIDs();
      }
      fflush(stdin);
    }
#endif
     
    if (FD_ISSET(node.sd, &rset)) {
      node.handlepkt();
    }
 
    if (FD_ISSET(node.dhtn_imgdb.sd, &rset)) {
      node.dhtn_imgdb.handleqry();
    }
  } while(1);
   
#ifdef _WIN32
  WSACleanup();
#endif
  exit(0);
}
