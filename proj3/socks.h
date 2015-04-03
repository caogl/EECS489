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
#ifndef __SOCKS_H__
#define __SOCKS_H__

#define SOCKS_UNINIT_SD -1

extern void socks_init();
extern int socks_servinit(char *progname, struct sockaddr_in *self, char *sname);
extern int socks_clntinit(char *sname, u_short port, int rcvbuf);
extern void socks_close(int td);

#endif /* __SOCKS_H__ */
