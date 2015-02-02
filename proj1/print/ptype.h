#ifndef __PTYPE_H__
#define __PTYPE_H__

#include <sys/types.h>     // u_short
#include <netinet/in.h>    // struct in_addr

#define net_assert(err, errmsg) { if ((err)) { perror(errmsg); assert(!(err)); } }
#define PR_MAXIMGNAME   127       // including the NULL terminator

// peer address structure
typedef struct {            
  struct in_addr peer_addr; // IPv4 address
  u_short peer_port;        // port#, always stored in network byte order
  u_short peer_rsvd;        // reserved field
 } peer_t;


// Message format               8 bit  8 bit     16 bit
typedef struct {            // +------+------+-------------+
   char pm_vers, pm_type;   // | vers | type |   #peers    |
   u_short pm_npeers;       // +------+------+-------------+
   peer_t pm_peer;          // |     peer ipv4 address     | 
} pmsg_t;                   // +---------------------------+
                            // |  peer port# |   reserved  |
                            // +---------------------------+

// peer table entry
typedef struct {            
  int pte_sd;               // socket peer is connected at
  bool pending;             // pending status of this peer
  peer_t pte_peer;          // peer's address+port#
} pte_t;                    // ptbl entry


// peer image information
typedef struct {
  unsigned char pm_vers;
  unsigned char pm_depth;   // in bytes, not in bits as returned by LTGA.GetPixelDepth()
  unsigned short pm_format;
  unsigned short pm_width;
  unsigned short pm_height;
} imsg_t;

/* Query packet
//   8 bit  8 bit     16 bit
// +------+------+-----------------+
// | vers | type |   peer port#    |
// +------+------+-----------------+
// |     peer ipv4 address         | 
// +-------------------------------+
// |  image name length |          |
// +---------------------	   +
// |	     image name		   |
// +------+------+-----------------+
*/
typedef struct {          	  			
  char pm_vers, pm_type;    	  			
  u_short peer_port; // always in network byte order    
  struct in_addr peer_addr; 	  			
  char pm_length;              
  char pm_name[PR_MAXIMGNAME] = {0};    
} query_t;     			  
				
#endif /* __PTYPE_H__ */
