#ifndef PEER_H
#define PEER_H

#include <stdio.h>         // fprintf(), perror(), fflush()
#include <stdlib.h>        // atoi()
#include <assert.h>        // assert()
#include <string.h>        // memset(), memcmp(), strlen(), strcpy(), memcpy()
#include <unistd.h>        // getopt(), STDIN_FILENO, gethostname()
#include <limits.h>        // LONG_MAX
#include <signal.h>        // signal()
#include <netdb.h>         // gethostbyname(), gethostbyaddr()
#include <arpa/inet.h>     // htons(), inet_ntoa()
#include <sys/socket.h>    // socket API, setsockopt(), getsockname()
#include <sys/select.h>    // select(), FD_*
#include <algorithm>       // max(), min()
#include <string>
#include <iostream>
#include <vector>
#include <unordered_set>
#ifdef __APPLE__
#include <GLUT/glut.h>
#include <OpenGL/gl.h>
#else
#include <GL/glut.h>
#include <GL/gl.h>
#endif

#include "ptype.h"
#include "ltga.h"

#define PM_VERS         0x1
#define PM_WELCOME      0x1       // Welcome peer
#define PM_RDIRECT      0x2       // Redirect peer
#define PM_SEARCH       0x4       // Search image       
#define PR_PORTSEP     ':'
#define PR_UNINIT_SD    -1
#define PR_MAXFQDN      255
#define PR_QLEN         10
#define PR_NUMSEG       50
#define PR_MSS        1440
#define PR_USLEEP   500000        // 500 ms
#define PR_LINGER       2
#define PR_HEADER       4         
#define PR_MAXPEER      6         // Max peer number
#define PR_TIMER        90        // Search image for 90 seconds
#define PR_WIDTH    1280
#define PR_HEIGHT    800

class PEER {
public:
    PEER(); // default constructor
    virtual ~PEER(); // the destructor  
    /* peer_args: parses command line args. On success, the provided 
     * peer FQDN:port, if any, is first translated to IPv4+port address 
     * and copied to memory in "pte"; "maxpeers" are updated if -n option
     * is supplied, otherwise, assign it with PR_MAXPEER; fimage, qimage
     * is updated if corresponding argument provided*/
    int peer_args(int argc, char *argv[], pte_t* pte); 
    /* peer_setup: sets up a TCP socket listening for connection.
     * The initial "port" is 0, in which case, the call to bind() 
     * obtains an ephemeral port. listens on the port bound to.  
     * Terminates process on error. Returns the bound socket id. Note: 
     * if flag == true, it's for listening to peer connection, otherwise
     * it's for listening to image transfer */
    int peer_setup(bool flag);
    /* peer_accept: accepts connection on the given socket, sd.
     * On connection, stores the descriptor of the connected socket and
     * the address+port# of the new peer in the space pointed to by the
     * "pte" argument, which must already be allocated by caller. Set
     * the linger option for PR_LINGER to allow data to be delivered 
     * to client. Terminates process on error. */
    int peer_accept(int sd, pte_t *pte);
    /* peer_ack: marshalls together a pmsg_t+k*peer_t message, 
     * or query_t message, depending on the type. If there's any error 
     * in sending, closes the socket td. In all cases, returns the 
     * error message returned by send(). */
    int peer_ack(pte_t* pte, char type, query_t* query);
    /* peer_connect: creates a new socket to connect to the provided peer.
     * The provided peer's address and port number is stored in the argument
     * of type pte_t or query_t when (pte == NULL). The newly created socket 
     * must be stored in the same pte passed in. On success, returns 0. 
     * On error, terminates process. */
    int peer_connect(pte_t *pte, query_t* query);
    /* peer_recv: receives data_size message and stores it in the memory 
     * pointed to by the "pos" argument, this memory must be pre-allocated 
     * by the caller. If there's error in receiving or if the connection 
     * has been terminated by the peer, closes td and returns the error. 
     * Otherwise, returns the amount of data received. */
    void peer_recv(int imgsd, int i);
    /* set all the descriptors in the peer table to select() on, 
     * return the largest socket descriptor */    
    int set_fdset(fd_set* r);
    /* check if a connection is made to this host via socket sd */
    void check_connect(int sd, fd_set* r);
    /* check if any peer in the peer table has sent you any message */
    void check_peer(int imgsd, fd_set* r);
    /* send the img named "fname */   
    void img_send(int td);

private:
    void peer_usage(char* progname);
    void print_info(std::string mes, pte_t* pte);
    int recv_help(int i, int data_size, char* buf);    
    bool check_join(pte_t* pte);
    bool check_buffer(query_t* query);

    int maxpeers;                   // the size of the peer table of this host
    struct sockaddr_in self;        // the internet address of this host
    struct sockaddr_in img_self;    // the internet address used for image transfer
    std::string fimage;		    // the image this host is holding
    std::string qimage;		    // the image this host is querying for			
    std::vector<pte_t> ptable; 	    // the peer table of this host
    std::vector<pte_t> dtable;	    // the decline table of this host
    std::unordered_set<int> buffer; // the circular buffer of this host
};

#endif 

