#include "peer.h"

/* constructor of the peer node */
peer::peer(int argc, char* argv[])
{
  if(peer_args(argc, argv))
    fprintf(stderr, "Usage: [ -p <nodename>:<port> -n <tablesize> ]\n");
  
  server_init();  

  if(kenport) // if connect to other peer when the peer is initialized
  {
    pte_t pte_tmp;
    struct hostent *sp;
    sp = gethostbyname(kenname);
    memcpy(&(pte_tmp.pte_peer.peer_addr), sp->h_addr, sp->h_length);
    pte_tmp.pte_peer.peer_port=kenport;
    peer_table.push_back(pte_tmp);
    client_init(peer_table[0]);
  }
}

/* set up the server socket and listen to connections */
void peer::server_init()
{
  sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  self.sin_family = AF_INET;
  self.sin_addr.s_addr = INADDR_ANY;
  self.sin_port = 0; // in network byte order

  // allow to reuse same host+port combination---> bind to same sockaddr_in
  int on = 1;
  setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
  bind(sd, (struct sockaddr *)&self, sizeof(struct sockaddr_in));

  listen(sd, PR_QLEN);
  int len = sizeof(struct sockaddr_in);
  getsockname(sd, (struct sockaddr *) &self, (socklen_t *)&len);
  if(sd<0)
  {
    perror("error in server socket set up");
    exit(1);
  }  

  char hostname[PR_MAXFQDN+1];
  memset(hostname, '\0', PR_MAXFQDN);
  gethostname(hostname, PR_MAXFQDN);
  fprintf(stderr, "This peer address is %s:%d\n", hostname, ntohs(self.sin_port));
}

/* set up the client socket and connect to the server peer */
void peer::client_init(pte_t &pte)
{
  pte.pte_sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  int on = 1;
  setsockopt(pte.pte_sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  setsockopt(pte.pte_sd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
  bind(pte.pte_sd, (struct sockaddr *) &self, sizeof(struct sockaddr_in));
  
  struct sockaddr_in server;
  memset((char *)&server, 0, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_port = pte.pte_peer.peer_port;
  memcpy(&server.sin_addr, &(pte.pte_peer.peer_addr), sizeof(struct in_addr));

  if(connect(pte.pte_sd, (struct sockaddr *)&server, sizeof(struct sockaddr_in))<0)
  {
    perror("error in client socket set up");
    exit(1);
  }
  pte.pending=true;

  fprintf(stderr, "Connected to peer %s:%d\n", inet_ntoa(pte.pte_peer.peer_addr), ntohs(pte.pte_peer.peer_port));
}

/* accept the connection, while retrieve the incomming peer addr+port and update
 * peer_table */
void peer::peer_accept(pte_t &pte)
{
  struct sockaddr_in peer_client;
  int len = sizeof(struct sockaddr_in);
  int td = accept(sd, (struct sockaddr *)&peer_client, (socklen_t *)&len);
  pte.pte_sd = td;

  struct linger linger_time;
  linger_time.l_onoff = 1;
  linger_time.l_linger = PR_LINGER;
  setsockopt(td, SOL_SOCKET, SO_LINGER, &linger_time, sizeof(linger_time));

  memcpy(&(pte.pte_peer.peer_addr), &(peer_client.sin_addr), sizeof(struct in_addr));
  pte.pte_peer.peer_port = peer_client.sin_port;
}

/* send back (1) PM_WELCOME or PM_REDIRECT
 *           (2) peer table entries for potential join */
void peer::peer_ack(int td, char type)
{
  peer_t peer_tmp;
  pmsg_t msg;
  msg.pm_vers = PM_VERS;
  msg.pm_type = type;
  msg.pm_npeers = htons(peer_table.size()<=PR_MAXPEERS ? peer_table.size():PR_MAXPEERS);

  char packet[sizeof(pmsg_t)+PR_MAXPEERS*sizeof(peer_t)]; 
  int packetSize=sizeof(pmsg_t);
  memcpy(packet, &msg, packetSize);

  for(int i=0; i<min((int)peer_table.size(), PR_MAXPEERS); i++)
  {
    memset(&peer_tmp, '\0', sizeof(peer_t));
    memcpy(&peer_tmp.peer_addr, &(peer_table[i].pte_peer.peer_addr), sizeof(struct in_addr));
    peer_tmp.peer_port=peer_table[i].pte_peer.peer_port;
    memcpy(packet+sizeof(pmsg_t)+i*sizeof(peer_t), &peer_tmp, sizeof(peer_t));
    packetSize+=sizeof(peer_t);
  }

  int total_sent=0;
  while(total_sent<packetSize)
  {
    int err=send(td, packet+total_sent, packetSize-total_sent, 0);
    if(err==0)
      break;
    total_sent+=err;
  }
  
  assert(total_sent==packetSize); // check for sent bytes
}

/* check_join: the newly attempted-to-join peer is described by pte
* return false for any of the case below; return true otherwise;
* (1) join with peers who are already in your peer table, including pending join
* (2) join with peers from which you have received a PR_RDIRECT message */
bool peer::check_join(peer_t &pte)
{
  for(int i=0; i<(int)peer_table.size(); i++)
  {
    if(peer_table[i].pte_peer.peer_addr.s_addr==pte.peer_addr.s_addr
       && peer_table[i].pte_peer.peer_port==pte.peer_port)  
      return false;
  }
  for(int i=0; i<(int)peer_decline.size(); i++)
  {
    if(peer_decline[i].pte_peer.peer_port==pte.peer_port 
        && peer_decline[i].pte_peer.peer_addr.s_addr==pte.peer_addr.s_addr)
    return false;
  }
  
  return true;
}

/* receive the join request
 * if peer table not full, join with the suggested peers */
void peer::peer_recv(int index)
{
  pmsg_t msg;
  int td=peer_table[index].pte_sd;  

  int byte_recv=recv(td, &msg, sizeof(pmsg_t), 0);
  if(byte_recv<0)
  {
    close(td);
    net_assert((byte_recv < 0), "peer: peer_recv");
    return;
  }
  if(byte_recv==0) // coonection closed by peer, reset peer table entry
  {
    peer_table[index].pte_sd=PR_UNINIT_SD;
    return;
  }

  fprintf(stderr, "Received ack from %s:%d\n", inet_ntoa(peer_table[index].pte_peer.peer_addr), ntohs(peer_table[index].pte_peer.peer_port));

  if(msg.pm_vers != PM_VERS)
  {
    fprintf(stderr, "unknown message version.\n");
    exit(1);
  }
  if(msg.pm_type==PM_RDIRECT)
  {
    fprintf(stderr, "Join redirected.\n");
    peer_decline.push_back(peer_table[index]);
    peer_table.erase(peer_table.begin()+index);
  }
  else // WELCOME
    peer_table[index].pending=false;

  int npeers=ntohs(msg.pm_npeers);
  if(npeers)
  {
    char peers[npeers*sizeof(peer_t)];
    byte_recv=0;
    while(byte_recv<(int)(npeers*sizeof(peer_t)))
    {
      int err=recv(td, peers, (npeers*sizeof(peer_t)-byte_recv), 0);
      if(err==0)
        break;
      byte_recv+=err;
    }
    assert(byte_recv==int(npeers*sizeof(peer_t)));  

    cout<<" which is peered with:\n";
    peer_t peer_tmp;
    for(int i=0; i<npeers; i++)
    {
      memset(&peer_tmp, '\0', sizeof(peer_t));
      memcpy(&peer_tmp, peers+i*sizeof(peer_t), sizeof(peer_t));
      struct hostent *phost = gethostbyaddr((char *)&peer_tmp.peer_addr, sizeof(struct in_addr), AF_INET);
      fprintf(stderr, " %s:%d\n", ((phost && phost->h_name) ? phost->h_name : inet_ntoa(peer_tmp.peer_addr)), ntohs(peer_tmp.peer_port));
    }
    for(int i=0; i<npeers; i++)
    {
      memset(&peer_tmp, '\0', sizeof(peer_t));
      memcpy(&peer_tmp, peers+i*sizeof(peer_t), sizeof(peer_t));
      if((int)peer_table.size()<MAXPEERS && check_join(peer_tmp))
      {
        pte_t pte_tmp;
        memcpy(&(pte_tmp.pte_peer), &peer_tmp, sizeof(peer_t));
        peer_table.push_back(pte_tmp);
        client_init(peer_table.back());
      }      
    }
  }

}

int peer::peer_args(int argc, char *argv[])
{
  kenname=new char[PR_MAXFQDN+1];
  kenport=0;
  memset(kenname, '\0', PR_MAXFQDN+1);
  MAXPEERS=6; // default value, if not specified in command line argument

  char c, *p;
  extern char *optarg;
  while ((c = getopt(argc, argv, "p:n:")) != EOF)
  {
    switch (c)
    {
      case 'p':
        for (p = optarg+strlen(optarg)-1; // point to last character of addr:port arg
             p != optarg && *p != PR_PORTSEP; // search for ':' separating addr from port
             p--);
        net_assert((p == optarg), "peer_args: peer addressed malformed");
        *p++ = '\0';
        kenport = htons((u_short) atoi(p)); // always stored in network byte order
        net_assert((p-optarg >= PR_MAXFQDN), "peer_args: FQDN too long");
        kenname=new char[PR_MAXFQDN];
        strcpy(kenname, optarg);
        break;
      case 'n':
        MAXPEERS=atoi(optarg);
        assert(MAXPEERS>=1);
        break;
      default:
        return(1);
        break;
    }
  }
  return (0);
}

int main(int argc, char *argv[])
{
  peer peer1(argc, argv);
  fd_set rset;

  while(1)
  {
    int maxsd=peer1.sd;

    FD_SET(peer1.sd, &rset);
    for(int i=0; i<(int)peer1.peer_table.size(); i++)
    {
      if(peer1.peer_table[i].pte_sd>0)
      {
        maxsd=max(maxsd, peer1.peer_table[i].pte_sd);
        FD_SET(peer1.peer_table[i].pte_sd, &rset);
      }
    }
    FD_SET(peer1.peer_imgdb.sd, &rset);        

    struct timeval t_value;
    t_value.tv_sec = 2;
    t_value.tv_usec = 500000;
    select(maxsd+1, &rset, NULL, NULL, &t_value); 
  
    /*************(1): if the server listen socket is ready to recv **************/
    
    if (FD_ISSET(peer1.sd, &rset)) 
    {
      //(i): peer table not full, accept connection
      if((int)peer1.peer_table.size()<peer1.MAXPEERS)
      {
        pte_t pte_tmp;
        peer1.peer_accept(pte_tmp);
        peer1.peer_ack(pte_tmp.pte_sd, PM_WELCOME);
        peer1.peer_table.push_back(pte_tmp);

        fprintf(stderr, "Connected from peer %s:%d\n", inet_ntoa(peer1.peer_table.back().pte_peer.peer_addr), ntohs(peer1.peer_table.back().pte_peer.peer_port));
      }
      //(ii): peer table full, redirect
      else
      {
        peer1.peer_accept(peer1.redirected);
        peer1.peer_ack(peer1.redirected.pte_sd, PM_RDIRECT);

        fprintf(stderr, "Peer table full: %s:%d redirected\n", inet_ntoa(peer1.redirected.pte_peer.peer_addr), ntohs(peer1.redirected.pte_peer.peer_port));
        close(peer1.redirected.pte_sd);
      }
    }  

    /************(2): if any peer connecting socket is ready to recv *************/
    for(int i=0; i<(int)peer1.peer_table.size(); i++)
    {
      if(peer1.peer_table[i].pte_sd>0 && FD_ISSET(peer1.peer_table[i].pte_sd, &rset))
        peer1.peer_recv(i);
    }

    /************(3): if the imgdb listen socket is ready to recv *************/
    if (FD_ISSET(peer1.peer_imgdb.sd, &rset))
    {
      if(peer1.peer_imgdb.handleqry())
        cout<<"sending out search packet"<<endl;
    } 

  }
}
