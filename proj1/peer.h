#include <stdio.h>         // fprintf(), perror(), fflush()
#include <stdlib.h>        // atoi(), srand(), rand()
#include <assert.h>        // assert()
#include <string.h>        // memset(), memcmp(), strlen(), strcpy(), memcpy()
#include <unistd.h>        // getopt(), STDIN_FILENO, gethostname()
#include <signal.h>        // signal()
#include <netdb.h>         // gethostbyname(), gethostbyaddr()
#include <netinet/in.h>    // struct in_addr
#include <arpa/inet.h>     // htons(), inet_ntoa()
#include <sys/types.h>     // u_short
#include <sys/socket.h>    // socket API, setsock
#include <time.h>          // time
#include <vector>
#include <algorithm>
#include <iostream>
#include "imgdb.h"

#define net_assert(err, errmsg) { if ((err)) { perror(errmsg); assert(!(err)); } }
#define PR_PORTSEP   ':'
#define PR_UNINIT_SD  -1
#define PR_MAXPEERS  6      // at most send 6 peer infor in pmsg_t for join
#define PR_MAXFQDN   256    // including terminating '\0'
#define PR_QLEN      10
#define PR_LINGER    2

#define PM_VERS      0x1
#define PM_WELCOME   0x1    // Welcome peer
#define PM_RDIRECT   0x2    // Redirect peer
#define PM_SEARCH    0x4    // Search peer

using namespace std;

typedef struct {            // peer address structure
  struct in_addr peer_addr; // IPv4 address
  u_short peer_port;        // port#, always stored in network byte order
  u_short peer_rsvd;        // reserved field
} peer_t;

// Peer Message format          8 bit  8 bit     16 bit
typedef struct {            // +------+------+-------------+
  char pm_vers, pm_type;    // | vers | type |   #peers    |
  u_short pm_npeers;        // +------+------+-------------+
  //peer_t pm_peer;         // |     peer ipv4 address     | 
} pmsg_t;                   // +---------------------------+
                            // |  peer port# |   reserved  |
                            // +---------------------------+
                            // |        more pm_peers      |
                            // |        .............      |
                            
// Search Message format            8 bit  8 bit     16 bit
typedef struct {            	// +------+------+-------------+
  char pm_vers, pm_type;    	// | vers | type |  search ID  |
  u_short search_ID;        	// +------+------+-------------+
  peer_t img_peer;		// | originator peer ipv4 addr | 
  char fname[NETIMG_MAXFNAME+1];// +---------------------------+
} schmsg_t; 	                // |img sockport#|   reserved  |
                            	// +---------------------------+
                            	// |        image name         |
                            	// |        ..........         |
                            	// +---------------------------+

typedef struct {            // peer table entry
  int pte_sd;               // socket peer is connected at
  bool pending;
  peer_t pte_peer;          // peer's address+port#
} pte_t;                    // ptbl entry

class peer
{
  public:
    peer(int argc, char* argv[]);	// set up the peer node
    void peer_accept(pte_t &pte);
    void peer_ack(pte_t &pte, char type);
    void peer_recv(int index);
    void peer_sendqry(pte_t &pte, schmsg_t* schmsg, u_short id);

    int sd;                  		// listen socket
    imgdb peer_imgdb;
    vector<pte_t> peer_table;		// peer table
    vector<pte_t> peer_decline;		// decline table
    pte_t redirected;        		// the redirected peer node
    int MAXPEERS;            		// the upper limit of the peer table size      	
    vector<u_short> searched_ID;	// circular hash_set to check duplicate image search

  private:
    int peer_args(int argc, char *argv[]);
    void server_init();
    void client_init(pte_t &pte);
    bool check_join(peer_t &pte);

    char* kenname;           		// join peer's address
    u_short kenport;         		// join peer's port
    sockaddr_in self;        		// the socket host address + port#
};


