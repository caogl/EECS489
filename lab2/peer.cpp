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
*/
#include <stdio.h>         // fprintf(), perror(), fflush()
#include <stdlib.h>        // atoi()
#include <assert.h>        // assert()
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
#include <sys/select.h>    // select(), FD_*
#endif

#ifdef _WIN32
#define close(sockdesc) closesocket(sockdesc)
#define perror(errmsg) { fprintf(stderr, "%s: %d\n", (errmsg), WSAGetLastError()); }
#endif

#define net_assert(err, errmsg) { if ((err)) { perror(errmsg); assert(!(err)); } }

#define PR_PORTSEP   ':'
#define PR_UNINIT_SD  -1
#define PR_MAXPEERS  2
#define PR_MAXFQDN   256    // including terminating '\0'
#define PR_QLEN      10
#define PR_LINGER    2

#define PM_VERS      0x1
#define PM_WELCOME   0x1    // Welcome peer
#define PM_RDIRECT   0x2    // Redirect per

typedef struct {            // peer address structure
  struct in_addr peer_addr; // IPv4 address
  u_short peer_port;        // port#, always stored in network byte order
  u_short peer_rsvd;        // reserved field
} peer_t;

// Message format:              8 bit  8 bit     16 bit
typedef struct {            // +------+------+-------------+
  char pm_vers, pm_type;    // | vers | type |   #peers    |
  u_short pm_npeers;        // +------+------+-------------+
  peer_t pm_peer;           // |     peer ipv4 address     | 
} pmsg_t;                   // +---------------------------+
                            // |  peer port# |   reserved  |
                            // +---------------------------+

typedef struct {            // peer table entry
  int pte_sd;               // socket peer is connected at
  char *pte_pname;          // peer's fqdn
  peer_t pte_peer;          // peer's address+port#
} pte_t;                    // ptbl entry

void
peer_usage(char *progname)
{
  fprintf(stderr, "Usage: %s [ -p peerFQDN.port ]\n", progname); 
  exit(1);
}

/*
 * peer_args: parses command line args.
 *
 * Returns 0 on success or 1 on failure.  On successful return, the
 * provided peer FQDN, if any, is copied to memory pointed to by
 * "pname". Then "port" points to the port to connect at peer, in
 * network byte order.  Both "pname" and "port" must be allocated 
 * by caller.  The buffer pname points to must be of size PR_MAXFQDN+1.
 * Nothing else is modified.
 */
int
peer_args(int argc, char *argv[], char *pname, u_short *port)
{
  char c, *p;
  extern char *optarg;

  net_assert(!pname, "peer_args: pname not allocated");
  net_assert(!port, "peer_args: port not allocated");

  while ((c = getopt(argc, argv, "p:")) != EOF) {
    switch (c) {
    case 'p':
      for (p = optarg+strlen(optarg)-1;     // point to last character of addr:port arg
           p != optarg && *p != PR_PORTSEP; // search for ':' separating addr from port
           p--);
      net_assert((p == optarg), "peer_args: peer addressed malformed");
      *p++ = '\0';
      *port = htons((u_short) atoi(p)); // always stored in network byte order

      net_assert((p-optarg >= PR_MAXFQDN), "peer_args: FQDN too long");
      strcpy(pname, optarg);
      break;
    default:
      return(1);
      break;
    }
  }

  return (0);
}

/*
 * peer_setup: sets up a TCP socket listening for connection.
 * The argument "port" may be 0, in which case, the
 * call to bind() obtains an ephemeral port.  If "port"
 * is not 0, it is assumed to be in network byte order.
 * In either case, listens on the port bound to.  
 *
 * Terminates process on error.
 * Returns the bound socket id.
*/
int
peer_setup(u_short port)
{
  
  /* Task 1: YOUR CODE HERE 
   * Fill out the rest of this function.
   */
  /* create a TCP socket, store the socket descriptor in "sd" */
  /* YOUR CODE HERE */

  /* initialize socket address */
  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  self.sin_family = AF_INET;
  self.sin_addr.s_addr = INADDR_ANY;
  self.sin_port = port; // in network byte order

  /* reuse local address so that bind doesn't complain
     of address already in use. */
  /* YOUR CODE HERE */

  /* bind address to socket */
  /* YOUR CODE HERE */

  /* listen on socket */
  /* YOUR CODE HERE */

  /* return socket id. */
  return (sd);
}

/*
 * peer_accept: accepts connection on the given socket, sd.
 *
 * On connection, stores the descriptor of the connected socket and
 * the address+port# of the new peer in the space pointed to by the
 * "pte" argument, which must already be allocated by caller. Set
 * the linger option for PR_LINGER to allow data to be delivered to client. 
 * Terminates process on error.
*/
int
peer_accept(int sd, pte_t *pte)
{
  struct sockaddr_in peer;
  
  /* Task 1: YOUR CODE HERE
     Fill out the rest of this function.
     Accept the new connection, storing the address of the connecting
     peer in the "peer" variable. Also store the socket descriptor
     returned by accept() in the pte */
  /* YOUR CODE HERE */

  /* make the socket wait for PR_LINGER time unit to make sure
     that all data sent has been delivered when closing the socket */
  /* YOUR CODE HERE */

  /* store peer's address+port# in pte */
  memcpy((char *) &pte->pte_peer.peer_addr, (char *) &peer.sin_addr, 
         sizeof(struct in_addr));
  pte->pte_peer.peer_port = peer.sin_port; /* stored in network byte order */

  return (pte->pte_sd);
}

/*
 * peer_ack: marshalls together a pmsg_t message of "type" as provided.
 * If the provided "peer" pointer is NULL, sets the number of peers to
 * 0, otherwise sets number of peers to 1 and copy the peer's address
 * and port number onto the message.  Then sends the message to the
 * peer connected at socket td.  (See the definition of pmsg_t at the
 * top of this file.)
 *
 * If there's any error in sending, closes the socket td.
 * In all cases, returns the error message returned by send().
*/
int
peer_ack(int td, char type, peer_t *peer)
{
  int err;

  /* Task 1: YOUR CODE HERE
   * Fill out the rest of this function.
   *
   * Marshall together a message of type pmsg_t.
  */
  /* YOUR CODE HERE */
  
  /* send msg to peer connected at socket td,
     close the socket td upon error in sending */
  /* YOUR CODE HERE */
  
  return(err);
}

/*
 * peer_connect: creates a new socket to connect to the provided peer.
 * The provided peer's address and port number is stored in the argument
 * of type pte_t (peer table entry type).  See the definition of pte_t
 * at the top of this file.  The newly created socket must be stored in
 * the same pte passed in.
 *
 * On success, returns 0.
 * On error, terminates process.
 */
int
peer_connect(pte_t *pte)
{

  /* Task 2: YOUR CODE HERE
   * Fill out the rest of this function.
  */
  /* create a new TCP socket, store the socket in the pte */
  /* YOUR CODE HERE */

  /* reuse local address so that the call to bind in peer_setup(), to
     bind to the same ephemeral address, doesn't complain of address
     already in use. */
  /* YOUR CODE HERE */

  /* initialize socket address with destination peer's IPv4 address and port number . */
  /* YOUR CODE HERE */

  /* connect to destination peer. */
  /* YOUR CODE HERE */

  return(err);
}  
  
/*
 * peer_recv: receives off socket td a pmsg_t message and stores it in
 * the memory pointed to by the "msg" argument, this memory must be
 * pre-allocated by the caller.  Be sure to receive all sizeof(pmgs_t)
 * worth of data.  If there's error in receiving or if the connection
 * has been terminated by the peer, closes td and returns the error.
 * Otherwise, returns the amount of data received, which should be
 * sizeof(pmgs_t) in all cases.
*/
int
peer_recv(int td, pmsg_t *msg)
{

  /* Task 2: YOUR CODE HERE
   *
   * Receive a pmsg_t message into the pre-allocated
   * memory pointed to by "msg"
  */
  /* YOUR CODE HERE */

  return (sizeof(pmsg_t));
}

int 
main(int argc, char *argv[])
{
  char c;
  fd_set rset;
  int i, err, sd, maxsd;
  struct hostent *phost;                              // the FQDN of this host
  struct sockaddr_in self;                            // the address of this host

  int npeers;
  pte_t pte[PR_MAXPEERS], redirected;                 // a 2-entry peer table
  char pnamebuf[PR_MAXPEERS*PR_MAXFQDN] = { 0 };  // space to hold 2 FQDNs 
  char *pname[PR_MAXPEERS];                           // pointers to above spaces
  pmsg_t msg;

  // init
  npeers=0;
  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  for (i=0; i < PR_MAXPEERS; i++) {
    pname[i] = &pnamebuf[i*PR_MAXFQDN];
    pte[i].pte_sd = PR_UNINIT_SD;
  }
  
  // parse args, see the comments for peer_args()
  if (peer_args(argc, argv, pname[0], &pte[0].pte_peer.peer_port)) {
    peer_usage(argv[0]);
  }

#ifdef _WIN32
  WSADATA wsa;
  
  err = WSAStartup(MAKEWORD(2,2), &wsa);  // winsock 2.2
  net_assert(err, "peer: WSAStartup");
#endif
  
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);    /* don't die if peer is dead */
#endif

  /* if pname is provided, connect to peer */
  if (*pname[0]) {

    pte[0].pte_pname = pname[0];

    /* Task 2: YOUR CODE HERE
     *
     * Given the peer whose FQDN is stored in pname[0],
     * get peer's address and stores it in the first entry
     * of the peer table (pte[0]).
    */
    /* YOUR CODE HERE */

    /* connect to peer in pte[0] */
    peer_connect(pte);  // Task 2: fill in the peer_connect() function above

    /* Task 2: YOUR CODE HERE
     *
     * Upon return from peer_connect(), the socket descriptor in
     * pte[0] should have been initialized and connected to the peer.
     * Obtain the ephemeral port assigned by the OS kernel to this
     * socket and store it in the variable "self" (along with the
     * peer's address).
    */
    /* YOUR CODE HERE */
    
    npeers++;

    /* inform user of connection to peer */
    fprintf(stderr, "Connected to peer %s:%d\n", pname[0],
            ntohs(pte[0].pte_peer.peer_port));
  }

  /* setup and listen on connection */
  sd = peer_setup(self.sin_port);  // Task 1: fill in the peer_setup() function above
  if (!self.sin_port) {
    /* Task 1: YOUR CODE HERE
       If a peer was not provided in the command line using "-p", the
       port number will be 0 and the socket sd would have been
       assigned an ephemeral port when calling bind() in peer_setup().
       Obtain the ephemeral port and store it in the variable "self"
       (along with peer's address) by copying the same chunk of code
       you just wrote at the end of the if statement block above. */
    /* YOUR CODE HERE */
  }
  
  /* Task 1: YOUR CODE HERE
     Find out the FQDN of the current host (use pname[1] as scratch
     space to put the name). Use gethostname(), it is sufficient most
     of the time. */
  /* YOUR CODE HERE */

  /* inform user which port this peer is listening on */
  fprintf(stderr, "This peer address is %s:%d\n",
          pname[1], ntohs(self.sin_port));

  do {
    /* determine the largest socket descriptor */
    maxsd = (sd > pte[0].pte_sd ? sd : pte[0].pte_sd);
    if (maxsd < pte[1].pte_sd) { maxsd = pte[1].pte_sd; }

    /* set all the descriptors to select() on */
    FD_ZERO(&rset);
#ifndef _WIN32
    FD_SET(STDIN_FILENO, &rset); // wait for input from std input,
        // Winsock only works with socket and stdin is not a socket
#endif
    FD_SET(sd, &rset);           // or the listening socket,
    for (i = 0; i < PR_MAXPEERS; i++) {
      if (pte[i].pte_sd > 0) {
        FD_SET(pte[i].pte_sd, &rset);  // or the peer connected sockets
      }
    }
    
    /* Task 1: YOUR CODE HERE
       Call select() to wait for any activity on any of the above
       descriptors. */
    /* YOUR CODE HERE */

#ifndef _WIN32
    if (FD_ISSET(STDIN_FILENO, &rset)) {
      // user input: if getchar() returns EOF or if user hits q, quit,
      // else flush input and go back to waiting
      if (((c = getchar()) == EOF) || (c == 'q') || (c == 'Q')) {
        fprintf(stderr, "Bye!\n");
        break;
      }
      fflush(stdin);
    }
#endif

    if (FD_ISSET(sd, &rset)) {
      // a connection is made to this host at the listened to socket
      if (npeers < PR_MAXPEERS) {
        /* Peer table is not full.  Accept the peer, send a welcome
         * message.  if we are connected to another peer, also sends
         * back the peer's address+port#
         */
        // Task 1: fill in the functions peer_accept() and peer_ack() above
        peer_accept(sd, &pte[npeers]);
        err = peer_ack(pte[npeers].pte_sd, PM_WELCOME,
                       (npeers > 0 ? &pte[0].pte_peer : 0));
        err = (err != sizeof(pmsg_t));
        net_assert(err, "peer: peer_ack welcome");
        pte[npeers].pte_pname = pname[npeers];

        /* log connection */
        /* get the host entry info on the connected host. */
        phost = gethostbyaddr((char *) &pte[npeers].pte_peer.peer_addr,
                            sizeof(struct in_addr), AF_INET);
        strcpy(pname[npeers], 
               ((phost && phost->h_name) ? phost->h_name:
                inet_ntoa(pte[npeers].pte_peer.peer_addr)));
        
        /* inform user of new peer */
        fprintf(stderr, "Connected from peer %s:%d\n",
                pname[npeers], ntohs(pte[npeers].pte_peer.peer_port));
        
        npeers++;

      } else {
        // Peer table full.  Accept peer, send a redirect message.
        // Task 1: the functions peer_accept() and peer_ack() you wrote
        //         must work without error in this case also.
        peer_accept(sd, &redirected);
        err = peer_ack(redirected.pte_sd, PM_RDIRECT,
                       (npeers > 0 ? &pte[0].pte_peer : 0));
        err = (err != sizeof(pmsg_t));
        net_assert(err, "peer: peer_ack redirect");

        /* log connection */
        /* get the host entry info on the connected host. */
        phost = gethostbyaddr((char *) &redirected.pte_peer.peer_addr,
                            sizeof(struct in_addr), AF_INET);

        /* inform user of peer redirected */
        fprintf(stderr, "Peer table full: %s:%d redirected\n",
               ((phost && phost->h_name) ? phost->h_name:
                inet_ntoa(redirected.pte_peer.peer_addr)),
                ntohs(redirected.pte_peer.peer_port));

        /* closes connection */
        close(redirected.pte_sd);
      } 
    }

    for (i = 0; i < PR_MAXPEERS; i++) {
      if (pte[i].pte_sd > 0 && FD_ISSET(pte[i].pte_sd, &rset)) {
        // a message arrived from a connected peer, receive it
        err = peer_recv(pte[i].pte_sd, &msg); // Task 2: fill in the functions peer_recv() above
        net_assert((err < 0), "peer: peer_recv");
        if (err == 0) {
          // if connection closed by peer, reset peer table entry
          pte[i].pte_sd = PR_UNINIT_SD;
        } else {
          // inform user
          fprintf(stderr, "Received ack from %s:%d\n", pte[i].pte_pname,
                  ntohs(pte[i].pte_peer.peer_port));
          
          if (msg.pm_vers != PM_VERS) {
            fprintf(stderr, "unknown message version.\n");
          } else {
            if (msg.pm_npeers) {
              // if message contains a peer address, inform user of
              // the peer two hops away
              phost = gethostbyaddr((char *) &msg.pm_peer.peer_addr,
                                    sizeof(struct in_addr), AF_INET);
              fprintf(stderr, "  which is peered with: %s:%d\n", 
                      ((phost && phost->h_name) ? phost->h_name :
                       inet_ntoa(msg.pm_peer.peer_addr)),
                      ntohs(msg.pm_peer.peer_port));
            }
            
            if (msg.pm_type == PM_RDIRECT) {
              // inform user if message is a redirection
              fprintf(stderr, "Join redirected, try to connect to the peer above.\n");
              exit(1);
            } 
          }
        }
      }
    }

  } while (1);

  for (i=0; i < PR_MAXPEERS; i++) {
    if (pte[i].pte_sd != PR_UNINIT_SD) {
      close(pte[i].pte_sd);
    }
  }
  close(sd);

#ifdef _WIN32
  WSACleanup();
#endif
  exit(0);
}
