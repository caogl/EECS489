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
#ifndef __HASH_H__
#define __HASH_H__

#ifdef __APPLE__
#include <CommonCrypto/CommonCrypto.h>
#define SHA1_MDLEN CC_SHA1_DIGEST_LENGTH
#define SHA1(data, len, md) CC_SHA1(data, len, md)
#else
#include <openssl/sha.h>
#define SHA1_MDLEN 20
#endif

#define HASH_IDMAX  255  // 2^8 - 1

#define BFIDX1  0
#define BFIDX2  7
#define BFIDX3 13
#define BFIDXN  7

extern char bfIDX(int start, unsigned char *md);
extern unsigned char ID(unsigned char *md);
extern int ID_inrange(unsigned char ID, unsigned char begin, unsigned char end);

#endif /* __HASH_H */
