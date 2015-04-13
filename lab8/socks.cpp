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
#include <assert.h>        // assert()
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>      // socklen_t
#else
#include <string.h>        // memset(), memcmp(), strlen(), strcpy(), memcpy()
#include <unistd.h>        // getopt(), STDIN_FILENO, gethostname()
#include <signal.h>        // signal()
#include <netdb.h>         // gethostbyname(), gethostbyaddr()
#include <netinet/in.h>    // struct in_addr
#include <arpa/inet.h>     // htons(), inet_ntoa()
#include <sys/types.h>     // u_short
#include <sys/socket.h>    // socket API, setsockopt(), getsockname()
#endif

#include "netimg.h"

void
socks_init()
{
#ifdef _WIN32
  int err;
  WSADATA wsa;

  err = WSAStartup(MAKEWORD(2,2), &wsa);  // winsock 2.2
  net_assert(err, "socks_init: WSAStartup");
#else // _WIN32
  signal(SIGPIPE, SIG_IGN);    /* don't die if peer is dead */
#endif // _WIN32

  return;
}

/*
 * sock_servinit: sets up a UDP server socket: Let the call to bind()
 * assign an ephemeral port to the socket, store the assigned socket
 * in the sin_port field of the provided "self" argument.  Next find
 * out the FQDN of the current host and store it in the provided
 * variable "sname". Caller must ensure that "sname" be of size
 * NETIMG_MAXFNAME.  Determine and print out the assigned port number
 * to screen so that user would know which port to use to connect to
 * this server.
 *
 * Terminates process on error.
 * Returns the bound socket id.
*/
int
socks_servinit(char *progname, struct sockaddr_in *self, char *sname)
{
  int sd=-1;
  int err, len;
  struct hostent *sp;
  unsigned int localaddr;

  /* create a UDP socket */
  sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  net_assert((sd < 0), "socks_servinit: socket");
  
  memset((char *) self, 0, sizeof(struct sockaddr_in));
  self->sin_family = AF_INET;
  self->sin_addr.s_addr = INADDR_ANY;
  self->sin_port = 0;

  /* bind address to socket */
  err = bind(sd, (struct sockaddr *) self,
             sizeof(struct sockaddr_in));
  net_assert(err, "socks_servinit: bind");

  /*
   * Obtain the ephemeral port assigned by the OS kernel to this
   * socket and store it in the local variable "self".
   */
  len = sizeof(struct sockaddr_in);
  err = getsockname(sd, (struct sockaddr *) self, (socklen_t *) &len);
  net_assert(err, "socks_servinit: getsockname");

  /* Find out the FQDN of the current host and store it in the local
     variable "sname".  gethostname() is usually sufficient. */
  err = gethostname(sname, NETIMG_MAXFNAME);
  net_assert(err, "socks_servinit: gethostname");

  /* Check if the hostname is a valid FQDN or a local alias.
     If local alias, set sname to "localhost" and addr to 127.0.0.1 */
  sp = gethostbyname(sname);
  if (sp) {
    memcpy(&self->sin_addr, sp->h_addr, sp->h_length);
  } else {
    localaddr = (unsigned int) inet_addr("127.0.0.1");
    memcpy(&self->sin_addr, (char *) &localaddr, sizeof(unsigned int));
    strcpy(sname, "localhost");
  }
  
  /* inform user which port this peer is listening on */
  fprintf(stderr, "%s address is %s:%d\n",
          progname, sname, ntohs(self->sin_port));

  return sd;
}

/*
 * socks_clntinit: creates a new socket to connect to the provided
 * server.  The server's name and port number are provided.  The port
 * number provided is assumed to already be in network byte order.
 *
 * On success, return the newly created socket descriptor.
 * On error, terminates process.
 */
int
socks_clntinit(char *sname, u_short port, int rcvbuf)
{
  int sd, err;
  struct sockaddr_in server;
  struct hostent *sp;
  socklen_t optlen;

  /* create a new UDP socket. */
  sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);   // sd global
  net_assert((sd < 0), "netimg_sockinit: socket");

  /* obtain the server's IPv4 address from sname and initialize the
     socket address with server's address and port number . */
  memset((char *) &server, 0, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_port = port;
  sp = gethostbyname(sname);
  net_assert((sp == 0), "netimg_sockinit: gethostbyname");
  memcpy(&server.sin_addr, sp->h_addr, sp->h_length);

  /* 
   * set socket receive buffer size to be at least rcvbuf bytes.
  */
  err = setsockopt(sd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(int));
  net_assert((err < 0), "socks_clntinit: setsockopt RCVBUF");
  optlen = sizeof(int);
  err = getsockopt(sd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, &optlen);
  net_assert((err < 0), "socks_clntinit: getsockopt RCVBUF");

  fprintf(stderr, "socks_clntinit: socket receive buffer set to %d bytes\n", rcvbuf);
  
  /* since this is a UDP socket, connect simply "remembers"
     server's address+port# */
  err = connect(sd, (struct sockaddr *) &server,
                sizeof(struct sockaddr_in));
  net_assert(err, "netimg_sockinit: connect");

  return(sd);
}

void
socks_close(int td)
{
#ifdef _WIN32
  closesocket(td);
#else
  close(td);
#endif // _WIN32
  return;
}
