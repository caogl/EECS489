#include <stdio.h>         // fprintf(), perror(), fflush()
#include <stdlib.h>        // atoi()
#include <assert.h>        // assert()
#include <string.h>        // memset(), memcmp(), strlen(), strcpy(), memcpy()
#include <unistd.h>        // getopt(), STDIN_FILENO, gethostname()
#include <signal.h>        // signal()
#include <netdb.h>         // gethostbyname(), gethostbyaddr()
#include <netinet/in.h>    // struct in_addr
#include <arpa/inet.h>     // htons(), inet_ntoa()
#include <sys/types.h>     // u_short
#include <sys/socket.h>    // socket API, setsockopt(), getsockname()
#include <sys/select.h>    // select(), FD_*
#include <iostream>
#include <vector>
#include <algorithm>
#define net_assert(err, errmsg) { if ((err)) { perror(errmsg); assert(!(err)); } }
#define PR_PORTSEP   ':'
#define PR_UNINIT_SD  -1
#define PR_MAXFQDN   256    // including terminating '\0'
#define PR_QLEN      10
#define PR_LINGER    2
#define PM_VERS      0x1
#define PM_WELCOME   0x1    // Welcome peer
#define PM_RDIRECT   0x2    // Redirect per

#include "ltga.h"
#include "netimg.h"
using namespace std;

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
  bool pending;             // pending or not
  char *pte_pname;          // peer's fqdn
  peer_t pte_peer;          // peer's address+port#
} pte_t;                    // ptbl entry

void peer_usage(char *progname)
{
  fprintf(stderr, "Usage: %s [ -p peerFQDN.port ]\n", progname); 
  exit(1);
}

bool check_join(vector<pte_t>& pte, vector<pte_t>& dte, peer_t& peer);
int peer_args(int argc, char *argv[], char *pname, u_short& port, int& PR_MAXPEERS);
int peer_setup(u_short port);
int peer_accept(int sd, pte_t &pte);
int peer_ack(int td, vector<pte_t>& pte, char type);
int peer_connect(pte_t& pte, sockaddr_in& self);
int peer_recv(char* packet, vector<pte_t>& pte, vector<pte_t>& dte, int i, int PR_MAXPEERS, sockaddr_in& self);
int imgdb_loadimg(char *fname, LTGA *image, imsg_t *imsg, long *img_size);
int imgdb_sockinit();
int imgdb_accept(int sd);
void imgdb_recvqry(int td, char *fname);
void imgdb_sendimg(int td, imsg_t *imsg, LTGA *image, long img_size);

int main(int argc, char *argv[])
{
  /* peer socket */
  fd_set rset;                                        // the prelaunched set to manage by select()
  int maxsd, err, sd;
  struct hostent *phost;                              // the FQDN of this host
  struct sockaddr_in self;                            // the address of this host
  int PR_MAXPEERS=6;                                  // if not specified by command line argument, default to be 6

  /* image socket */
  int sd_img, td_img;
  LTGA image;
  imsg_t imsg;
  long img_size;
  char fname[NETIMG_MAXFNAME] = { 0 };
    
  char pnameTmp[PR_MAXFQDN];
  memset(pnameTmp, '\0', sizeof(pnameTmp));
  u_short peer_portTmp;

  if(peer_args(argc, argv, pnameTmp, peer_portTmp, PR_MAXPEERS))
    peer_usage(argv[0]);

  vector<pte_t> pte;		      		      // peer table
  vector<pte_t> dte;                                 // decline table
  pte_t redirected;                 		      // redirected peer
  char packet[sizeof(pmsg_t)+5*sizeof(peer_t)];          // the "which is peered with" info buffer

  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  
  /* if pname is provided, connect to peer */
  if (strlen(pnameTmp)) 
  {
    pte_t pteTmp;
    pte.push_back(pteTmp);
    pte[0].pte_pname=new char[PR_MAXFQDN+1];
    pte[0].pte_peer.peer_port=peer_portTmp;
    memcpy(pte[0].pte_pname, pnameTmp, PR_MAXFQDN+1);
    struct hostent *sp;
    sp = gethostbyname(pnameTmp);
    memcpy(&pte[0].pte_peer.peer_addr, sp->h_addr, sp->h_length);
    peer_connect(pte[0], self);
    int len = sizeof(struct sockaddr_in);
    getsockname(pte[0].pte_sd, (struct sockaddr *)&self, (socklen_t *)&len); // copy the addr of socket to second arg
    fprintf(stderr, "Connected to peer %s:%d\n", pte[0].pte_pname, ntohs(pte[0].pte_peer.peer_port));
  }

  /* setup the server socket */
  sd = peer_setup(self.sin_port);  // peer socket
  sd_img=imgdb_sockinit();         // image socket

  if (!self.sin_port) 
  {
    struct sockaddr_in ephemeral;
    int len = sizeof(struct sockaddr_in);
    getsockname(sd, (struct sockaddr *)&ephemeral, (socklen_t *)&len);
    self.sin_port = ephemeral.sin_port;
  }

  memset(pnameTmp, '\0', PR_MAXFQDN+1);
  gethostname(pnameTmp, PR_MAXFQDN);
  fprintf(stderr, "Peering socket address %s:%d\n",pnameTmp, ntohs(self.sin_port));  /* peer server socket address */

  while(1)
  {
    /* determine the largest socket descriptor */
    maxsd=max(sd, sd_img);
    int pSize=pte.size();
    for(int i=0; i<pSize; i++)
      maxsd=max(maxsd, pte[i].pte_sd);

    /* set all the descriptors to select() on */
    FD_ZERO(&rset);
    FD_SET(sd, &rset);           // add the peer socket into the set
    FD_SET(sd_img, &rset);       // add the image socket into the set
    int n=pte.size();
    for(int i = 0; i < n; i++) 
    {
      if (pte[i].pte_sd > 0) 
        FD_SET(pte[i].pte_sd, &rset);  // add the peer connected sockets into the set
    }
    
    struct timeval t_value;
    t_value.tv_sec = 2;
    t_value.tv_usec = 500000;
    select(maxsd+1, &rset, NULL, NULL, &t_value); 

    /* if the listening peer socket sd is ready to recv */
    if (FD_ISSET(sd, &rset)) 
    {
      int pSize=pte.size();
      if (pSize<PR_MAXPEERS) // if peer table is not full, welcome
      {
        pte_t pteTmp;
        peer_accept(sd, pteTmp);
        err=peer_ack(pteTmp.pte_sd, pte, PM_WELCOME);
        net_assert(err<=0, "peer: peer_ack welcome");
        pte.push_back(pteTmp);

        pte.back().pte_pname=new char[PR_MAXFQDN+1];
        memset(pte.back().pte_pname, '\0', PR_MAXFQDN+1);
        phost = gethostbyaddr((char *) &pte.back().pte_peer.peer_addr, sizeof(struct in_addr), AF_INET);
        strcpy(pte.back().pte_pname, ((phost && phost->h_name) ? phost->h_name: inet_ntoa(pte.back().pte_peer.peer_addr)));
        
        fprintf(stderr, "Connected from peer %s:%d\n", pte.back().pte_pname, ntohs(pte.back().pte_peer.peer_port));        
      } 
      else // if peer table is full, redirect
      {
        peer_accept(sd, redirected);
        err=peer_ack(redirected.pte_sd, pte, PM_RDIRECT);
        net_assert(err<=0, "peer: peer_ack redirect");
        phost = gethostbyaddr((char *) &redirected.pte_peer.peer_addr, sizeof(struct in_addr), AF_INET);

        fprintf(stderr, "Peer table full: %s:%d redirected\n", ((phost && phost->h_name) ? phost->h_name:
                inet_ntoa(redirected.pte_peer.peer_addr)), ntohs(redirected.pte_peer.peer_port));

        close(redirected.pte_sd);
      } 
    }
    
    /* if the listening image socket sd is ready to recv */
    if(FD_ISSET(sd_img, &rset))
    {
      td_img = imgdb_accept(sd_img); // Task 2
      imgdb_recvqry(td_img, fname); // Task 2
      if(imgdb_loadimg(fname, &image, &imsg, &img_size) == NETIMG_FOUND)
        imgdb_sendimg(td_img, &imsg, &image, img_size); // Task 2
      else
        imgdb_sendimg(td_img, &imsg, NULL, 0);
      close(td_img);
    }

    /* if the client socket is ready to receive */
    n=pte.size();
    for (int i=0; i<n; i++) 
    {
      if (pte[i].pte_sd > 0 && FD_ISSET(pte[i].pte_sd, &rset))   
        peer_recv(packet, pte, dte, i, PR_MAXPEERS, self);
    }

  }
  for (int i=0; i < PR_MAXPEERS; i++) 
  {
    if (pte[i].pte_sd != PR_UNINIT_SD)
      close(pte[i].pte_sd);
  }
  close(sd);

  exit(0);
}

/* (1) not join with peers already in the peer table, include pending joins
 * (2) not join with peers which gives you a PR_RDIRECT                  */
bool check_join(vector<pte_t>& pte, vector<pte_t>& dte, peer_t& peer)
{
  int n=pte.size();
  for(int i=0; i<n; i++)
  {
    if(pte[i].pte_peer.peer_port==peer.peer_port && pte[i].pte_peer.peer_addr.s_addr==peer.peer_addr.s_addr)
      return false;
  }
  n=dte.size();
  for(int i=0; i<n; i++)
  {
    if(dte[i].pte_peer.peer_port==peer.peer_port && dte[i].pte_peer.peer_addr.s_addr==peer.peer_addr.s_addr)
      return false;
  }
  return true;
}

int peer_args(int argc, char *argv[], char *pname, u_short& port, int& PR_MAXPEERS)
{
  char c, *p;
  extern char *optarg;
  net_assert(!pname, "peer_args: pname not allocated");
  //net_assert(!port, "peer_args: port not allocated");
  while ((c = getopt(argc, argv, "p:n:")) != EOF)
  {
    switch (c) 
    {
    case 'p':
      for (p = optarg+strlen(optarg)-1;     // point to last character of addr:port arg
           p != optarg && *p != PR_PORTSEP; // search for ':' separating addr from port
           p--);
      net_assert((p == optarg), "peer_args: peer addressed malformed");
      *p++ = '\0';
      port = htons((u_short) atoi(p)); // always stored in network byte order

      net_assert((p-optarg >= PR_MAXFQDN), "peer_args: FQDN too long");
      strcpy(pname, optarg);
      break;
    case 'n':
      PR_MAXPEERS=atoi(optarg);
      if(PR_MAXPEERS<1)
	exit(1);
      break;
    default:
      return(1);
      break;
    }
  }
  return (0);
}

int peer_setup(u_short port)
{
  int sd;
  struct sockaddr_in self;
  sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  self.sin_family = AF_INET;
  self.sin_addr.s_addr = INADDR_ANY;
  self.sin_port = port; // in network byte order
  int on = 1;
  setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));

  bind(sd, (struct sockaddr *)&self, sizeof(struct sockaddr_in)); 
  listen(sd, PR_QLEN);
 
  return (sd);
}

int peer_accept(int sd, pte_t& pte)
{
  struct sockaddr_in peer;

  int len = sizeof(struct sockaddr_in);
  int td = accept(sd, (struct sockaddr *)&peer, (socklen_t *)&len);
  pte.pte_sd = td;
 
  struct linger linger_time;
  linger_time.l_onoff = 1;
  linger_time.l_linger = PR_LINGER;
  setsockopt(td, SOL_SOCKET, SO_LINGER, &linger_time, sizeof(linger_time));

  memcpy((char *) &pte.pte_peer.peer_addr, (char *) &peer.sin_addr, 
         sizeof(struct in_addr));
  pte.pte_peer.peer_port = peer.sin_port; /* stored in network byte order */

  return (pte.pte_sd);
}

int peer_ack(int td, vector<pte_t>& pte, char type)
{
  peer_t peer;
  pmsg_t msg;
  msg.pm_vers = PM_VERS;
  msg.pm_type = type;
  int npeers=pte.size();
  msg.pm_npeers = htons(npeers);
  char packet[sizeof(msg)+5*sizeof(peer)]; // to send to the requesting client, maximum 6 packets
  int packetSize=sizeof(msg)-sizeof(peer);
  memcpy(packet, &msg, packetSize);

  for(int i=0; i<min(npeers, 6); i++)
  {
    memset(&peer, 0, sizeof(peer));
    memcpy(&peer.peer_addr, &pte[i].pte_peer.peer_addr, sizeof(struct in_addr));
    peer.peer_port=pte[i].pte_peer.peer_port;
    memcpy(packet+sizeof(msg)+(i-1)*sizeof(peer), &peer, sizeof(peer));
    packetSize+=sizeof(peer);
  }

  int err = send(td, packet, packetSize, 0);
  if(err<= 0)
    close(td);
  return(err);
}

int peer_connect(pte_t& pte, sockaddr_in& self)
{
  pte.pte_sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  int on =1;

  setsockopt(pte.pte_sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  setsockopt(pte.pte_sd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
  bind(pte.pte_sd, (struct sockaddr *) &self, sizeof(struct sockaddr_in));
  pte.pending=true;

  struct sockaddr_in server;
  memset((char *)&server, 0, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_port = pte.pte_peer.peer_port;
  memcpy(&server.sin_addr, &(pte.pte_peer.peer_addr), sizeof(struct in_addr));

  if(connect(pte.pte_sd, (struct sockaddr *)&server, sizeof(struct sockaddr_in))<0)
  {
    perror("error in connecting");
    exit(1);
  }
  
  return 0;
}  
  
int peer_recv(char* packet, vector<pte_t>& pte, vector<pte_t>& dte, int i, int PR_MAXPEERS, sockaddr_in& self) // receive and reconnect
{
  int status = recv(pte[i].pte_sd, packet, sizeof(pmsg_t)+5*sizeof(peer_t), 0);
  if(status<0)
  {
    close(pte[i].pte_sd);
    net_assert((status < 0), "peer: peer_recv");
    return status;
  }
  if(status == 0) 
    pte[i].pte_sd = PR_UNINIT_SD; // if connection closed by peer, reset peer table entry
  else 
  {
    pmsg_t msg;
    memcpy((char*)&msg, packet, sizeof(msg)-sizeof(peer_t)); // receive the msg header
    msg.pm_npeers = ntohs(msg.pm_npeers);
    fprintf(stderr, "Received ack from %s:%d\n", pte[i].pte_pname, ntohs(pte[i].pte_peer.peer_port));      
    if(msg.pm_type==PM_RDIRECT)
    {  
      fprintf(stderr, "Join redirected.\n");
      dte.push_back(pte[i]);
      pte.erase(pte.begin()+i);
    }
    else
      pte[i].pending=false;

    /* send the peers in its peer table */
    if(msg.pm_vers != PM_VERS)
    {
      fprintf(stderr, "unknown message version.\n");
      exit(1);
    }
    else 
    {
      if(msg.pm_npeers) 
      {
	cout<<"  which is peered with:\n";
        peer_t peer;
        for(int i=0; i<msg.pm_npeers; i++)
        {
          memset((char*) &peer, 0, sizeof(peer));
          memcpy(&peer, packet+sizeof(msg)+(i-1)*sizeof(peer), sizeof(peer));
          struct hostent *phost = gethostbyaddr((char *)&peer.peer_addr, sizeof(struct in_addr), AF_INET);
          fprintf(stderr, "  %s:%d\n", ((phost && phost->h_name) ? phost->h_name : inet_ntoa(peer.peer_addr)), ntohs(peer.peer_port));
        }
        for(int i=0; i<msg.pm_npeers; i++)
        { 
          int n=pte.size();
          memset((char*) &peer, 0, sizeof(peer));
          memcpy(&peer, packet+sizeof(msg)+(i-1)*sizeof(peer), sizeof(peer));
          if(n<PR_MAXPEERS && check_join(pte, dte, peer)) // if the peer can join the table
          {
            pte_t pteTmp;
            pte.push_back(pteTmp);
            pte.back().pte_pname=new char[PR_MAXFQDN+1];
            pte.back().pte_peer.peer_port=peer.peer_port;
            memset(pte.back().pte_pname, '\0', PR_MAXFQDN+1);
            struct hostent* phost = gethostbyaddr((char *) &peer.peer_addr, sizeof(struct in_addr), AF_INET);
            strcpy(pte.back().pte_pname, ((phost && phost->h_name) ? phost->h_name: inet_ntoa(pte.back().pte_peer.peer_addr)));
            memcpy(&pte.back().pte_peer.peer_addr, phost->h_addr, phost->h_length);
            //memcpy(&pte.back().pte_peer, &peer, sizeof(peer_t));
            
            peer_connect(pte.back(), self);

          }    
        }
      }      
    }   
  }
  return (status);
}

