/* 
 * Copyright (c) 1997, 2014 University of Michigan, Ann Arbor.
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
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define net_assert(err, errmsg) { if ((err)) { perror(errmsg); assert(!(err)); } }

#define SERVER "localhost"
#define PORT 4897
#define BLEN 128

int main(int argc, char *argv[])
{
	struct sockaddr_in server;
	struct hostent *sp;
  	int sd;
	int n;
  	char buf[BLEN];

	sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	memset((char *) &server, 0, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons((u_short) PORT);
 	sp = gethostbyname(SERVER);
	memcpy(&server.sin_addr, sp->h_addr, sp->h_length);

	connect(sd, (struct sockaddr *) &server, sizeof(struct sockaddr_in));
    
	n = recv(sd, buf, sizeof(buf), 0);
	while (n > 0)
	{
		write(1, buf, n);
		n = recv(sd, buf, sizeof(buf), 0);
 	} 

	close(sd);

	exit(0);
}
