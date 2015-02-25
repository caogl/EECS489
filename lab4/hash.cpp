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
 * Authors: Linhao Peng (hdfdisk@umich.edu), Sugih Jamin (jamin@eecs.umich.edu)
 *
*/
#include <stdio.h>
#include <string.h>
#include <assert.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>      // socklen_t
#endif // _WIN32

#include "netimg.h"
#include "hash.h"

/*
 * bfIDX(start, md): given a SHA1 output in md, compute a bloom filter index
 * starting at the "start+BFIDXN" element of md.
 */
char
bfIDX(int start, unsigned char *md)
{
  int i, n;
  unsigned char idx = 0;

  net_assert((start+BFIDXN > SHA1_MDLEN), "bfhash: start index too large");

  for (i=start, n = start+BFIDXN; i < n; i++) {
    idx ^= md[i];    // simply XOR the BFIDXN elements together
  }

  return(idx & 0x3f);  // trim the result to 6 lower bits
}

/*
 * ID(md): given a SHA1 output in md, compute an object ID
 * modulo HASH_IDMAX+1.
 */
unsigned char
ID(unsigned char *md)
{
  int i;
  unsigned char ID = 0;

  for (i = 0; i < SHA1_MDLEN; i++) {
    ID ^= md[i];   // simply XOR all the unsigned chars,
                   // assuming HASH_IDMAX is 255.
  }

  return(ID);
}

/*
 * Task 1:
 *
 * ID_inrange(ID, begin, end), return true (1) if ID is in the range
 * (begin, end], i.e., begin < ID <= end.  The variables are all
 * modulo HASH_IDMAX+1.  For example, ID=6 for begin=250, end=10
 * should return true (1).  For an example of how this function is
 * used, see imgdb::loaddb().
 */
int
ID_inrange(unsigned char ID, unsigned char begin, unsigned char end)
{
  /* YOUR CODE HERE */
  //1. when begin == end --> whole range
  //2. when begin < end
  //3. when begin > end	
  return ((begin == end)||(begin< ID && ID<= end) ||(begin > end &&(begin<ID || ID<= end)));
}

/*
 * Remove the "#if 0" and "#endif" lines if you want to compile this
 * file by itself to play with SHA1 and the other functions here and
 * to test your ID_inrange function.
*/
#if 0
int 
main(int argc, const char * argv[])
{
  int i;
  unsigned char md[SHA1_MDLEN] = { 0 };
  unsigned char id;

  if (argc < 2) {
    printf("Usage: %s <string>\n", argv[0]);
    exit(-1);
  }

  SHA1(argv[1], strlen(argv[1]), md);

  printf("SHA1 MD: ");
  for(i = 0; i < SHA1_MDLEN; i++) printf("%02x", md[i]);
  printf("\n");

  id = ID(md);
  printf("ID: 0x%02x (%d)\n", id, (int) id);
  printf("BF: 0x%lx\n", (1L << (int) bfIDX(BFIDX1, md)) |
                        (1L << (int) bfIDX(BFIDX2, md)) |
                        (1L << (int) bfIDX(BFIDX3, md)));

  printf("ID in range? %s\n", ID_inrange(250, 252, 8) ? "yes" : "no");
}
#endif
