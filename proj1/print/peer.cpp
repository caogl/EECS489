#include "peer.h"

/****************************************************************/
/*********************** private function ***********************/
/****************************************************************/
void PEER::peer_usage(char* progname){
  fprintf(stderr, "Usage: %s [ -p peerFQDN.port ]\n", progname); 
  exit(1);
}

/* print_info: print the FQDN:port, according to the internet 
 * addresss stored in "pte", along with a message "mes" */
void PEER::print_info(std::string mes, pte_t* pte){
  struct hostent *phost;   
  phost = gethostbyaddr((char *) &pte->pte_peer.peer_addr,
                        sizeof(struct in_addr), AF_INET);
  fprintf(stderr, "%s%s:%d\n", mes.c_str(),
          ((phost && phost->h_name) ? phost->h_name :
           inet_ntoa(pte->pte_peer.peer_addr)),
           ntohs(pte->pte_peer.peer_port));
}

/* recv_help: receive "data_size" data from the socket described by 
 * ptable[i].pte_sd, store it at buf */
int PEER::recv_help(int i, int data_size, char* buf){
  int bytes;
  int data_offset = 0;
  while (data_offset < data_size){
    bytes = recv(ptable[i].pte_sd, buf+data_offset, data_size-data_offset, 0);
    if (bytes <= 0){
      close(ptable[i].pte_sd);
      net_assert((bytes < 0), "peer: peer_recv");
      fprintf(stderr, "connection closed.\n");
      ptable.erase(ptable.begin()+i);
      return bytes;
    }
    data_offset += bytes;
  }
  return data_offset;
}

/* check_join: the newly attempted-to-join peer is described by pte 
 * return false for any of the case below; return true otherwise;
 * (1) join with peers who are already in your peer table
 * (2) join with peers from which you have received a PR_RDIRECT message
 * (3) join with peers you already have a pending join */
bool PEER::check_join(pte_t* pte) {
  int psize = ptable.size();
  int dsize = dtable.size();

  // check the peer table
  for (int i=0; i<psize; i++){
    if (ptable[i].pte_peer.peer_addr.s_addr == pte->pte_peer.peer_addr.s_addr
        && ptable[i].pte_peer.peer_port == pte->pte_peer.peer_port){
      print_info("Already connected with this peer ", pte);
      return false;
    }
  }

  // check the decline table
  for (int i=0; i<dsize; i++){
    if (dtable[i].pte_peer.peer_addr.s_addr == pte->pte_peer.peer_addr.s_addr
        && dtable[i].pte_peer.peer_port == pte->pte_peer.peer_port){
      print_info("Already declined by this peer ", pte);
      return false;
    }
  }
 
  return true;
}

/* check_buffer: compute ID for the query by adding every 32-bit of it up
 * and check if the sum is already in the circular buffer of this peer; 
 * return true if so, return false otherwise*/
bool PEER::check_buffer(query_t* query){
  int single, temp, ID = 0;
  
  // the settings gurantee an exact division by PR_HEADER, or 32-bit
  temp = sizeof(query_t)/PR_HEADER; 
  for (int i=0; i<temp; i++){
    memcpy(&single, (char*)query+i*PR_HEADER, PR_HEADER);
    ID += single;
  }
  
  if (buffer.find(ID) == buffer.end()){
    buffer.insert(ID);
    return false;
  }
  return true;
}
/****************************************************************/
/*********************** public function ***********************/
/****************************************************************/

PEER::PEER() {
  maxpeers = 0;
  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  memset((char *) &img_self, 0, sizeof(struct sockaddr_in));
  fimage.clear();
  qimage.clear();
}

PEER::~PEER() {
  maxpeers = 0;
  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  memset((char *) &img_self, 0, sizeof(struct sockaddr_in));
  fimage.clear();
  qimage.clear();
  ptable.clear();
  dtable.clear();
  buffer.clear();
}

int PEER::peer_args(int argc, char *argv[], pte_t* pte) {
  char c, *p;
  extern char *optarg;
  struct hostent *sp;
  LTGA image;
  bool image_search = false;

  while ((c = getopt(argc, argv, "p:n:q:f:")) != EOF) {
    switch (c) {
    case 'p':
      for (p = optarg+strlen(optarg)-1;     
           p != optarg && *p != PR_PORTSEP; 
           p--); // search for ':' separating addr from port
      net_assert((p == optarg), "peer_args: peer addressed malformed");
      *p++ = '\0';
      // always stored in network byte order
      pte->pte_peer.peer_port = htons((u_short) atoi(p)); 
      
      net_assert((p-optarg > PR_MAXFQDN), "peer_args: FQDN too long");
      sp = gethostbyname(optarg);
      memcpy(&pte->pte_peer.peer_addr, sp->h_addr, sp->h_length); 
      pte->pending = true;	
      break;
    case 'n':
      net_assert(!optarg, "peer_args: missing argument for -n");
      maxpeers = atoi(optarg);
      net_assert(maxpeers <= 0, "peer_args: invalid argument for -n");
      break;
    case 'f':
      net_assert((strlen(optarg)+1 > PR_MAXIMGNAME), 
		 "peer_args: image name too long");
      image.LoadFromFile(optarg);
      net_assert((!image.IsLoaded()), "imginit: image not loaded");
      fimage = std::string(optarg);
      break;
    case 'q':
      net_assert((strlen(optarg)+1 > PR_MAXIMGNAME), 
                 "peer_args: image name too long");
      qimage = std::string(optarg);
      image_search = true;
      break;
    default:
      peer_usage(argv[0]);
      break;
    }
  }
  
  /* set default peer table size if not given in command line */
  if (!maxpeers) { 
    maxpeers = PR_MAXPEER;
  }
  return image_search;
}

int PEER::peer_setup(bool flag) {
  int sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  int len = sizeof(struct sockaddr_in);
  int on = 1; 
  char sname[PR_MAXFQDN+1] = {0};
  struct hostent* sp;
  gethostname(sname, sizeof(sname)); // store the local host name
  
  /* if it's the socket listening on peer connection, 
   * reuse local address; don't reuse address when 
   * it's the socket listening on image transfer */
  if (flag) {
    self.sin_family = AF_INET;
    self.sin_addr.s_addr = INADDR_ANY;
    self.sin_port = 0; // in network byte order
    setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
    bind(sd, (struct sockaddr *) &self, sizeof(struct sockaddr_in));
    listen(sd, PR_QLEN);
    getsockname(sd, (struct sockaddr *) &self, (socklen_t *)&len);
    fprintf(stderr,"This peer address is %s:%d\n",sname,ntohs(self.sin_port));
  } else {
    img_self.sin_family = AF_INET;
    img_self.sin_addr.s_addr = INADDR_ANY;
    img_self.sin_port = 0; // in network byte order
    bind(sd, (struct sockaddr *) &img_self, sizeof(struct sockaddr_in));
    listen(sd, PR_QLEN);
    getsockname(sd, (struct sockaddr *) &img_self, (socklen_t *)&len);
    sp = gethostbyname(sname);
    memcpy(&img_self.sin_addr, sp->h_addr, sp->h_length);
  }

  return sd;
}

int PEER::peer_accept(int sd, pte_t *pte) {
  struct sockaddr_in peer;
  
  /* Accept the new connection, storing the address of the connecting
     peer in the "peer" variable. Also store the socket descriptor
     returned by accept() in the pte */
  int len = sizeof(struct sockaddr_in);
  int td = accept(sd, (struct sockaddr *) &peer, (socklen_t *) &len);

  /* store peer's address+port# in pte if it's a peer connection;
   * otherwise, it's accepted an image transfer, close sd right away
   * to serve only once */
  if (pte){ 
    struct linger so_linger;
    so_linger.l_onoff = 1;
    so_linger.l_linger = PR_LINGER;
    setsockopt(td, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));

    pte->pte_sd = td;
    memcpy((char *) &pte->pte_peer.peer_addr, (char *) &peer.sin_addr, 
           sizeof(struct in_addr));
    pte->pte_peer.peer_port = peer.sin_port; /* stored in network byte order */
  } 

  return td;
}

int PEER::peer_ack(pte_t* pte, char type, query_t* query) {
  int err, temp;
  int num = 0;
  int psize = ptable.size();
  pmsg_t msg;
  peer_t peer;
  char packet[sizeof(pmsg_t)+(PR_MAXPEER-1)*sizeof(peer_t)] = {0};

  if (type == PM_SEARCH){
    temp = PR_HEADER + PR_HEADER + sizeof(char) + query->pm_length;
    err = send(pte->pte_sd, query, temp, 0);
    if (err != temp){
      close(pte->pte_sd);
    }
    return err;
  } 

  /* send my peer table along with the welcome or redirect message 
   * but only choose those valid candidates up to a maximum of PR_MAXPEER*/ 
  for (int i=0; i<psize; i++){
    // avoid pending peers and self
    if (!ptable[i].pending && 
	(ptable[i].pte_peer.peer_addr.s_addr != pte->pte_peer.peer_addr.s_addr
        || ptable[i].pte_peer.peer_port != pte->pte_peer.peer_port)) {
      memcpy(&peer.peer_addr, &ptable[i].pte_peer.peer_addr, 
             sizeof(struct in_addr));
      peer.peer_port = ptable[i].pte_peer.peer_port;
      memcpy(packet+sizeof(pmsg_t)+(i-1)*sizeof(peer_t), &peer, sizeof(peer_t));
      memset(&peer, 0, sizeof(peer_t));
      /* count the number of peer to be sent, no more than PR_MAXPEER */
      if (++num == PR_MAXPEER)
	break;
    }  
  }
  
  /* send msg to peer connected at socket td,
   * close the socket td upon error in sending */
  msg.pm_vers = PM_VERS;
  msg.pm_type = type;
  msg.pm_npeers = htons(num);
  memcpy(packet, &msg, sizeof(pmsg_t)-sizeof(peer_t));

  temp = sizeof(pmsg_t)+(num-1)*sizeof(peer_t);
  err = send(pte->pte_sd, &packet, temp, 0);
  if (err != temp){ 
    close(pte->pte_sd);
    return err;
  }

  if (type == PM_WELCOME){
    pte->pending = false;
    ptable.push_back(*pte); 
    print_info("Connected from peer ", pte);
  } else {
    print_info("Peer table full: ", pte);
    close(pte->pte_sd);
  }

  return err;
}

int PEER::peer_connect(pte_t *pte, query_t* query) {
  int sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

  /* reuse local address, bind it to the socket */ 
  int on = 1;
  setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
  bind(sd, (struct sockaddr *) &self, sizeof(struct sockaddr_in));
  
  /* initialize socket with destination peer's IPv4 address and port number */
  struct sockaddr_in server;
  memset((char *) &server, 0, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  if (pte) { // a peer connection 
    server.sin_port = pte->pte_peer.peer_port;
    memcpy(&server.sin_addr, &pte->pte_peer.peer_addr, sizeof(struct in_addr));

    /* push the new peer to the peer table with a pending status */
    pte->pte_sd = sd;
    pte->pending = true;
    ptable.push_back(*pte);
    print_info("Pending connection to peer ", pte);
    if (connect(sd,(struct sockaddr *)&server,sizeof(struct sockaddr_in)) < 0){
      perror("connect");
      abort();
    }
  } else { // image transfer
    server.sin_port = query->peer_port;
    memcpy(&server.sin_addr, &query->peer_addr, sizeof(struct in_addr));
    if (connect(sd,(struct sockaddr *)&server,sizeof(struct sockaddr_in)) < 0){
      close(sd);
    }
    /* make the socket wait for PR_LINGER time unit to make sure
     * that all data sent has been delivered when closing the socket */    
    struct linger so_linger;
    so_linger.l_onoff = 1;
    so_linger.l_linger = PR_LINGER;
    setsockopt(sd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));  
  }

  return sd;
}  

void PEER::peer_recv(int imgsd, int i) {
  int npeers, psize, td, bytes = 0;
  query_t query;
  pte_t pte;
  char packet[PR_MAXPEER*sizeof(peer_t)] = {0};
    
  /* return if receiving header unsuccessfully*/
  if (!recv_help(i, PR_HEADER, (char*)&query)) return;
  bytes += PR_HEADER;  
 
  /* parse the header */
  if (query.pm_vers != PM_VERS || (query.pm_type != PM_RDIRECT &&
      query.pm_type != PM_WELCOME && query.pm_type != PM_SEARCH)){
    close(ptable[i].pte_sd);
    fprintf(stderr, "unknown message version.\n");
    ptable.erase(ptable.begin()+i);
    return;
  } 
  
  if (query.pm_type == PM_SEARCH){
    // receive IPv4 address+image name length
    if (!recv_help(i, PR_HEADER+sizeof(char), (char*)&query+bytes)) return;
    bytes += (PR_HEADER+sizeof(char));  
    
    // receive the rest of the message
    if (!recv_help(i, query.pm_length, (char*)&query+bytes)) return;
    // check duplication at the circular buffer 
    if (check_buffer(&query)) return; 
    if (!fimage.compare(query.pm_name)){ // image found
      td = peer_connect(0, &query);
      img_send(td);
    } else { // image not found, flood the peer table
      psize = ptable.size();
      for (int j=0; j<psize; j++){
        if (i == j) continue; // don't send to where it's from
        peer_ack(&ptable[j], PM_SEARCH, &query); 
      }   
    } 
  } else { // PM_WELCOME or PM_DIRECT
    /* Get the peer number, which is temporarily stored
     * at "peer_port" of "query", receive the rest 
     * of message, and store it into "packet"*/
    npeers = ntohs(query.peer_port);
    if (npeers && !recv_help(i, npeers*sizeof(peer_t), packet)) return;
    print_info("Received ack from ", &ptable[i]);
 
    if (query.pm_type == PM_RDIRECT){ // redirect message
      print_info("Join redirected by ", &ptable[i]);
      dtable.push_back(ptable[i]);
      ptable.erase(ptable.begin()+i);
    } else { // welcome message
      print_info("Connected to peer ", &ptable[i]);
      ptable[i].pending = false;
      // Now it's time to send my query if it is active
      if (imgsd >= 0){
        memset(&query, 0, sizeof(query));
        query.pm_vers = PM_VERS;
        query.pm_type = PM_SEARCH;
        query.peer_port = img_self.sin_port; // network byte order
        memcpy(&query.peer_addr,&img_self.sin_addr, sizeof(struct in_addr));
        query.pm_length = qimage.size()+1; // NULL terminator included
        qimage.copy(query.pm_name, qimage.size(), 0);
	query.pm_name[qimage.size()] = '\0';
        peer_ack(&ptable[i], PM_SEARCH, &query);
      }   
    }

    for (int j=0; j<npeers; j++){
      memcpy(&pte.pte_peer, packet+j*sizeof(peer_t), sizeof(peer_t));
      psize = ptable.size();
      if (psize < maxpeers && check_join(&pte)){
        peer_connect(&pte, 0);
      }
      memset(&pte, 0, sizeof(pte_t));
    }
  }
}

int PEER::set_fdset(fd_set* r){
  int maxsd = 0;
  for (unsigned int i = 0; i < ptable.size(); i++) {
    maxsd = std::max(maxsd, ptable[i].pte_sd);
    FD_SET(ptable[i].pte_sd, r);
  }
  return maxsd;
}

void PEER::check_connect(int sd, fd_set* r){
  if (!FD_ISSET(sd, r)) return;

  pte_t pte;
  peer_accept(sd, &pte); 
  
  int psize = ptable.size();
  for (int i=0; i<psize; i++){
    /* determine if peer has already been in the peer table */
    if (ptable[i].pte_peer.peer_addr.s_addr == pte.pte_peer.peer_addr.s_addr
	&& ptable[i].pte_peer.peer_port == pte.pte_peer.peer_port) {
      /* Check if peer is pending; if so, compare IPv4 + port. 
       * if current host has a smaller value, close its connection, 
       * move the peer to decline table, add the same peer to the 
       * peer table (but with a different socket descriptor), then 
       * return true (later along with a welcome message); otherwise, 
       * return false (later along with a redirect message) */
      if (ptable[i].pending && 
	  (self.sin_addr.s_addr < pte.pte_peer.peer_addr.s_addr ||
           (self.sin_addr.s_addr == pte.pte_peer.peer_addr.s_addr &&
	    self.sin_port < pte.pte_peer.peer_port))) {
        close(ptable[i].pte_sd);
        dtable.push_back(ptable[i]);
        ptable.erase(ptable.begin()+i); 
        peer_ack(&pte, PM_WELCOME, 0);
      } else { 
      	peer_ack(&pte, PM_RDIRECT, 0);
      }
      return;
    }
  }

  /* if not peered before, check if peer table is full;
   * if not full, add it to the peer table without pending*/
  if (psize < maxpeers){
    peer_ack(&pte, PM_WELCOME, 0);
  } else {
    peer_ack(&pte, PM_RDIRECT, 0);
  } 
}

void PEER::check_peer(int imgsd, fd_set* r){
  int psize = ptable.size();
  for (int i = psize-1; i >= 0; i--) {
    if (!FD_ISSET(ptable[i].pte_sd, r)) continue;
    peer_recv(imgsd, i);
  }
}

void PEER::img_send(int td){
  LTGA image;
  imsg_t imsg;
  long img_size, left;
  double img_dsize;
  int alpha, greyscale, segsize, bytes;
  char* ip;

  /* image init */
  image.LoadFromFile(fimage.c_str());
  
  img_dsize = (double) (image.GetImageWidth()
                        *image.GetImageHeight()
                        *(image.GetPixelDepth()/8));
  net_assert((img_dsize > (double) LONG_MAX), "display: image too big");
  img_size = (long) img_dsize;

  imsg.pm_vers = PM_VERS;
  imsg.pm_depth = (unsigned char)(image.GetPixelDepth()/8);
  imsg.pm_width = htons(image.GetImageWidth());
  imsg.pm_height = htons(image.GetImageHeight());
  alpha = image.GetAlphaDepth();
  greyscale = image.GetImageType();
  greyscale = (greyscale == 3 || greyscale == 11);
  if (greyscale) {
    imsg.pm_format = htons(alpha ? GL_LUMINANCE_ALPHA : GL_LUMINANCE);
  } else {
    imsg.pm_format = htons(alpha ? GL_RGBA : GL_RGB);
  }

  /* image sending */
  if (send(td, &imsg, sizeof(imsg_t), 0) <= 0){
    close(td);
    printf("Send failed\n");
    return;
  }

  segsize = img_size/PR_NUMSEG; /* compute segment size */
  /* but don't let segment be too small*/
  segsize = segsize < PR_MSS ? PR_MSS : segsize; 
  /* ip points to the start of byte buffer holding image */
  ip = (char *) image.GetPixels();    

  for (left = img_size; left; left -= bytes){
     /* Send one segment of data of size segsize at each iteration.
      * The last segment may be smaller than segsize */
    segsize = std::min(segsize, (int)left);
    bytes = send(td, ip+img_size-left, segsize, 0);
    fprintf(stderr, "img_send: size %d, sent %d\n", (int) left, bytes);
    if (bytes <= 0){
      close(td);
      printf("Send failed\n");
      return;
    }
  }

  /* close connection after image transfer is completed */
  close(td);
}
