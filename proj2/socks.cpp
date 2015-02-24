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
  WSADATA wsa;

  err = WSAStartup(MAKEWORD(2,2), &wsa);  // winsock 2.2
  net_assert(err, "socks_init: WSAStartup");
#else // _WIN32
  signal(SIGPIPE, SIG_IGN);    /* don't die if peer is dead */
#endif // _WIN32

  return;
}

/*
 * socks_clntinit: creates a new socket to connect to the provided
 * server.  One of the server's address or its FQDN must be provided.
 * The other may be NULL.  The port number provided is assumed to
 * already be in network byte order.
 *
 * On success, return the newly created socket descriptor.
 * On error, terminate process.
 */
int
socks_clntinit(struct in_addr *saddr, char *sname, u_short port)
{
  int err, sd;
  struct sockaddr_in server;
  struct hostent *sp;

  /* create a new TCP socket */
  sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  net_assert((sd < 0), "socks_clntinit: socket");

  /* initialize the socket address with server's address and port
     number.  If provided "saddr" is not NULL, use it as the server's
     address, else obtain the server's address from sname. */
  memset((char *) &server, 0, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_port = port;
  if (saddr) {
    memcpy(&server.sin_addr, saddr, sizeof(struct in_addr));
  } else {
    net_assert((!sname), "socks_clntinit: NULL sname");
    sp = gethostbyname(sname);
    net_assert((sp == 0), "socks_clntinit: gethostbyname");
    memcpy(&server.sin_addr, sp->h_addr, sp->h_length);
  }

  /* connect to server */
  err = connect(sd, (struct sockaddr *) &server, sizeof(struct sockaddr_in));
  net_assert(err, "socks_clntinit: connect");

  return(sd);
}

/*
 * sock_servinit: sets up a TCP socket listening for connection.
 * Let the call to bind() assign an ephemeral port to this listening socket.
 * Determine and print out the assigned port number to screen so that user
 * would know which port to use to connect to this server.
 *
 * Terminate process on error.
 * Return the bound socket id, update the provided "self" sockaddr_in with
 * the bound socket's in_addr and port, in network byte order, and
 * update the provided "sname" with this host's fqdn.  "sname" must be
 * of size NETIMG_MAXFNAME.
*/
int
socks_servinit(char *progname, struct sockaddr_in *self, char *sname)
{
  int sd;
  int err, len;
  struct hostent *sp;
  unsigned int localaddr;

  /* create a TCP socket */
  sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  net_assert((sd < 0), "socks_servinit: socket");
  
  memset((char *) self, 0, sizeof(struct sockaddr_in));
  self->sin_family = AF_INET;
  self->sin_addr.s_addr = INADDR_ANY;
  self->sin_port = 0;

  /* bind address to socket */
  err = bind(sd, (struct sockaddr *) self, sizeof(struct sockaddr_in));
  net_assert(err, "socks_servinit: bind");

  /* listen on socket */
  err = listen(sd, NETIMG_QLEN);
  net_assert(err, "socks_servinit: listen");

  /*
   * Obtain the ephemeral port assigned by the OS kernel to this
   * socket and store it in the local variable "self".
   */
  len = sizeof(struct sockaddr_in);
  err = getsockname(sd, (struct sockaddr *) self, (socklen_t *) &len);
  net_assert(err, "socks_servinit: getsockname");

  /* Find out the FQDN of the current host and store it in the 
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
 * socks_accept: accepts connection on the given socket, sd.
 *
 * On connection, set the linger option for NETIMG_LINGER to
 * allow data to be delivered to client.  Return the descriptor
 * of the connected socket.
 * Terminates process on error.
*/
int
socks_accept(int sd, int verbose)
{
  int td;
  int err, len;
  struct linger linger_opt;
  struct sockaddr_in client;
  struct hostent *cp;

  /* Accept the new connection.
   * Use the variable "td" to hold the new connected socket.
  */
  len = sizeof(struct sockaddr_in);
  td = accept(sd, (struct sockaddr *) &client, (socklen_t *)&len);
  net_assert((td < 0), "socks_accept: accept");

  /* make the socket wait for NETIMG_LINGER time unit to make sure
     that all data sent has been delivered when closing the socket */
  linger_opt.l_onoff = 1;
  linger_opt.l_linger = NETIMG_LINGER;
  err = setsockopt(td, SOL_SOCKET, SO_LINGER,
                   (char *) &linger_opt, sizeof(struct linger));
  net_assert(err, "socks_accpet: setsockopt SO_LINGER");
  
  if (verbose) {
    /* inform user of connection */
    cp = gethostbyaddr((char *) &client.sin_addr, sizeof(struct in_addr), AF_INET);
    fprintf(stderr, "Connected from client %s:%d\n",
            ((cp && cp->h_name) ? cp->h_name : inet_ntoa(client.sin_addr)),
            ntohs(client.sin_port));
  }

  return(td);
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
