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
#include <string>

#include "netimg.h"

#define net_assert(err, errmsg) { if ((err)) { perror(errmsg); assert(!(err)); } }

#define PR_PORTSEP   ':'
#define PR_UNINIT_SD  -1
#define PR_MAXPEERS  6
#define PR_MAXFQDN   256    // including terminating '\0'
#define PR_QLEN      10
#define PR_LINGER    2

#define PM_VERS      0x1
#define PM_WELCOME   0x1    // Welcome peer
#define PM_RDIRECT   0x2    // Redirect peer
#define PM_SEARCH    0x4    // Search image

#define BUFFER_SIZE  6
using namespace std;


/*****************************************************************************************************/

class peer{
public:
	peer();
	virtual ~peer();
	void peer_usage(char *progname);
	int peer_args(int argc, char *argv[], pte_t* pte);
	int peer_setup();
	int peer_accept(int sd, pte_t *pte);
	int peer_ack(pte_t* pte ,char type, query_t* query);
	int peer_connect(pte_t *pte, query_t* qry);
	int peer_recv(int index); 
	void close_table();
//para:
	void print_log(string tmp, pte_t* pte);
	int fdset(fd_set* rset);
	void check_connect(int sd, int img_sd, fd_set* rset);
	void check_msg(fd_set* rset);


	int maxpeer_num;
	int sID;
	struct sockaddr_in self;	//addr of this peer;
	peer_t img_peer;				//addr of the img sock;
	vector<pte_t> peer_table;	//peer table of this peer;
	vector<pte_t> decline_table;//decline table of this peer;
	vector<int> cir_buffer;     //circular buffer for the search packets;
	int img_client_sd;
	int img_sd;
};


peer::peer(){
	maxpeer_num = -1;
	sID=0;
	img_client_sd = 0;
	img_sd = 0;
	memset((char *)&self, 0, sizeof(struct sockaddr_in));
	memset(&img_peer, 0, sizeof(peer_t));
	for(int i =0; i<BUFFER_SIZE; i++){
		cir_buffer.push_back(-1);
	}
}

peer::~peer(){
	maxpeer_num = -1;
	memset((char *)&self, 0, sizeof(struct sockaddr_in));
	peer_table.clear();
	decline_table.clear();
	cir_buffer.clear();
}
/**************************helper func*********************************/
void peer::print_log(string tmp, pte_t* pte){
	struct hostent *phost;  
	phost = gethostbyaddr((char *) &pte->pte_peer.peer_addr,sizeof(struct in_addr), AF_INET);
	fprintf(stderr, "%s%s:%d\n", tmp.c_str(), ((phost && phost->h_name) ? phost->h_name:
			inet_ntoa(pte->pte_peer.peer_addr)),ntohs(pte->pte_peer.peer_port));
}


int peer::fdset(fd_set* rset){
	int maxsd = 0;
	for(unsigned int i=0; i<peer_table.size(); i++){
		maxsd = max(maxsd, peer_table[i].pte_sd);
		FD_SET(peer_table[i].pte_sd, rset);
	}
	return maxsd;
}

void peer::check_connect(int sd, int img_sd, fd_set* rset){
	LTGA image;
	imsg_t imsg;
	long img_size;
	char fname[NETIMG_MAXFNAME] = { 0 };	

	if(FD_ISSET(img_sd, rset)){
		int td = imgdb_accept(img_sd);
		int len = peer_table.size();
		if(imgdb_recvqry(td, fname, &imsg) == NETIMG_QRY){ //Query 
			if (imgdb_loadimg(fname, &image, &imsg, &img_size) == NETIMG_FOUND) {
				imgdb_sendimg(td, &imsg, &image, img_size);
			} else {
				//sending out a search packet to the peer to peer network;
				img_client_sd = td; //save the sd;
				query_t qry;
				qry.vers = PM_VERS;
				qry.type = PM_SEARCH;
				qry.search_ID = sID++;
				memcpy((char*)&qry.img_peer, (char*)&img_peer, sizeof(peer_t));
				memcpy(qry.img_name, fname, NETIMG_MAXFNAME);

//				cout<< "forming the query_t, port: "<<qry.img_peer.peer_port<<endl;
				//cout<<"can not find image, and forming a packet"<<endl;
				//sending searching packet to all the other peers in the peer table;
				for(int i=0; i<len; i++){
					//the peer still in pending is not allowed to search;
					if(peer_table[i].pending == false){
						peer_ack(&peer_table[i], PM_SEARCH, &qry);
					}
				}
			}
		}
		else{ // finding result;
			//receive the image according to the imsg_t;
			cout<<"find the image;"<<endl;
			//int size = recv(td, (char*)&image, sizeof(LTGA), 0);
			int buffer_size = (unsigned short)ntohs(imsg.im_width)*(unsigned short)ntohs(imsg.im_height)*(u_short)imsg.im_depth;
			cout<< "depth: "<< (unsigned short)imsg.im_depth;
			cout<<"image size is: "<<buffer_size<<endl;
			char* buffer;
			buffer = (char*)malloc(buffer_size);
			int size = recv(td, buffer, buffer_size, MSG_WAITALL);
			cout << "recv size is: "<< size <<endl; 
			cout << "img_clinet_sd is "<< img_client_sd<<endl;

//			imgdb_sendimg(img_client_sd, &imsg, &image, img_size); // Task 2

			int bytes = send(img_client_sd, (char*)&imsg, sizeof(imsg_t), 0);
//			cout<<"imsg bytes send "<<bytes<<endl;
			if(bytes<0){
				perror("error in sending1");
				exit(1);
			}
			if(buffer_size >0){
				bytes = send(img_client_sd, buffer, buffer_size, 0);
				cout<<"image total bytes sends: "<< bytes <<endl;
			}
//			close(img_client_sd);
		}
	}

	if(FD_ISSET(sd, rset)){
		pte_t pte_tmp;
		peer_accept(sd, &pte_tmp);

		int len = peer_table.size(); 
		// check if pte_tmp is already in the peer table;
		for (int i =0; i<len; i++){
			if(pte_tmp.pte_peer.peer_addr.s_addr == peer_table[i].pte_peer.peer_addr.s_addr &&
				pte_tmp.pte_peer.peer_port == peer_table[i].pte_peer.peer_port){
				if (peer_table[i].pending &&
					(self.sin_addr.s_addr < pte_tmp.pte_peer.peer_addr.s_addr ||
					(self.sin_addr.s_addr == pte_tmp.pte_peer.peer_addr.s_addr &&
					self.sin_port < pte_tmp.pte_peer.peer_port))) {
					close(peer_table[i].pte_sd);
					decline_table.push_back(peer_table[i]);
					peer_table.erase(peer_table.begin()+i);
					peer_ack(&pte_tmp, PM_WELCOME, 0);
				} 
				else {
					peer_ack(&pte_tmp, PM_RDIRECT, 0);
				}
				return;
			}
		}
		//after checking the peer table,
		//if not exist;
		if(len< maxpeer_num){
			peer_ack(&pte_tmp, PM_WELCOME, 0);
		}
		else{
			peer_ack(&pte_tmp, PM_RDIRECT, 0);
		}
	}
	return;
}

void peer::check_msg(fd_set* rset){
	int len = peer_table.size();
	for(int i=0; i<len; i++){
		if(peer_table[i].pte_sd>0 && FD_ISSET(peer_table[i].pte_sd, rset)){
//			print_log("FD is set :", &peer_table[i]);
			peer_recv(i);
		}
	}
	return;
}



void peer::peer_usage(char *progname){
	fprintf(stderr, "Usage: %s [ -p peerFQDN.port -n maxpeer_num]\n", progname); 
	exit(1);
}


/*
 * peer_args: parses command line args.
 */
int peer::peer_args(int argc, char *argv[], pte_t *pte){
	char c, *p;
	extern char *optarg;
	struct hostent *sp; //peer hostname structure;

	while ((c = getopt(argc, argv, "p:n:")) != EOF) {
		switch (c) {
		case 'p':
			for (p = optarg+strlen(optarg)-1;     // point to last character of addr:port arg
				p != optarg && *p != PR_PORTSEP; // search for ':' separating addr from port
				p--);
			net_assert((p == optarg), "peer_args: peer addressed malformed");
			*p++ = '\0';
			pte->pte_peer.peer_port = htons((u_short) atoi(p)); // always stored in network byte order
			net_assert((p-optarg >= PR_MAXFQDN), "peer_args: FQDN too long");
			sp = gethostbyname(optarg);
			memcpy(&pte->pte_peer.peer_addr, sp->h_addr, sp->h_length);
			pte->pending = true;
			break;

		case 'n':
			net_assert(!optarg, "peer_args: maxpeer table num is missing");
//*** new added arg: maxpeer_num: 
			maxpeer_num = atoi(optarg);
			net_assert(maxpeer_num <= 0, "peer_args: invalid number for peer table");
			break;

		default:
			return(1);
			break;
		}
	}

//if -n is not in the cmd arg, then set it to the default num;
	if(maxpeer_num == -1){
		maxpeer_num = PR_MAXPEERS;
	}
	return (0);
}

int peer::peer_setup(){
	int sd;
	sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	int len = sizeof(struct sockaddr_in);
	char sname[PR_MAXFQDN] = {0};
	gethostname(sname, sizeof(sname));

	/* initialize socket address */
	self.sin_family = AF_INET;
	self.sin_addr.s_addr = INADDR_ANY;
	self.sin_port = 0; // in network byte order

	/* reuse local address so that bind doesn't complain of address already in use. */
	int on = 1;
//	setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));

	/* bind address to socket */
	bind(sd, (struct sockaddr *)&self, sizeof(struct sockaddr_in));
	/* listen on socket */
	listen(sd, PR_QLEN);
	
	//as the port num is '0', the getsockname can give a random num to the port;
	getsockname(sd, (struct sockaddr *) &self, (socklen_t *)&len);
    fprintf(stderr,"This peer address is %s:%d\n",sname,ntohs(self.sin_port));

	/* return socket id. */
	return (sd);
}

/*??? how to distinguish the connection is from a peer or netimg client*/
int peer::peer_accept(int sd, pte_t *pte){
//	cout<<"peer_accept is called."<<endl;
//	print_log("test peer_accept: ", pte);
	struct sockaddr_in peer;
  
	int len = sizeof(struct sockaddr_in);
	int td = accept(sd, (struct sockaddr *)&peer, (socklen_t *)&len);

	pte->pte_sd = td;

	/* make the socket wait for PR_LINGER time unit to make sure
	that all data sent has been delivered when closing the socket */
	struct linger linger_time;
	linger_time.l_onoff = 1;
	linger_time.l_linger = PR_LINGER;
	setsockopt(td, SOL_SOCKET, SO_LINGER, &linger_time, sizeof(linger_time));

	/* store peer's address+port# in pte */
	memcpy((char *) &pte->pte_peer.peer_addr, (char *) &peer.sin_addr, sizeof(struct in_addr));
	pte->pte_peer.peer_port = peer.sin_port; /* stored in network byte order */
//	cout<<"accept peer port: "<< ntohs(peer.sin_port)<<endl;
	return (pte->pte_sd);
}



int peer::peer_ack(pte_t* pte, char type, query_t* query){
//	cout<< "peer_ack is called."<<endl;	
	int err; int size;
	peer_t peer;
	int len = peer_table.size();
	char packet[sizeof(pmsg_t)+(PR_MAXPEERS-1)*sizeof(peer_t)] = {0};
//	cout<<"size of packet"<<sizeof(pmsg_t)+(PR_MAXPEERS-1)*sizeof(peer_t)<<endl;
 
	//if it is a search ack, simply do query and return;
	if(type == PM_SEARCH){
		size = sizeof(pmsg_t)+NETIMG_MAXFNAME;
		err = send(pte->pte_sd, query, size, 0);
		if(err != size){
			close(pte->pte_sd); 
		}
		return err;
	}

 
	/* send msg to peer connected at socket td,
	close the socket td upon error in sending */

	pmsg_t msg;
	msg.pm_vers = PM_VERS;
	msg.pm_type = type;

	if(len){
		int n_count = 0;
		// from table entry 0 - 4  + last one;
		for(int i = 0; i<len; i++){
			//if it is not the pending peer, or it is the self
			if(!peer_table[i].pending && 
				(peer_table[i].pte_peer.peer_addr.s_addr != pte->pte_peer.peer_addr.s_addr||
				peer_table[i].pte_peer.peer_port != pte->pte_peer.peer_port)){
				memcpy(&peer.peer_addr, &peer_table[i].pte_peer.peer_addr,sizeof(struct in_addr));
				peer.peer_port = peer_table[i].pte_peer.peer_port;
				memcpy(packet+sizeof(pmsg_t)+(i-1)*sizeof(peer_t), &peer, sizeof(peer_t));
				memset(&peer, 0, sizeof(peer_t));
				n_count++;
				if(n_count == 6) break;
			}
		}
//		cout << sizeof(pmsg_t) <<' '<<n_count<<' '<<sizeof(peer_t)<<endl;
		size = sizeof(pmsg_t) + (n_count-1)*sizeof(peer_t);
		msg.pm_npeers = htons(n_count);
	}
	else{
		msg.pm_npeers = 0;
		size = 4;
	}

	memcpy(packet, &msg, sizeof(pmsg_t)-sizeof(peer_t));
	err = send(pte->pte_sd, &packet, size, 0);

//	cout<<"peer ack sending size: "<<err<<endl;

	if(err!= size){
		close(pte->pte_sd);
		return err;
	}

	if(type == PM_WELCOME){
		pte->pending = false;
		peer_table.push_back(*pte);
		print_log("Connected from peer ", pte);
	}
	else{
		print_log("Peer table full, Reject the peer connection from: ", pte);
		close(pte->pte_sd);
	}
	return(err);
}


int peer::peer_connect(pte_t *pte, query_t* qry){

	int sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	int on =1;
	setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
//???
	bind(sd, (struct sockaddr *) &self, sizeof(struct sockaddr_in));

	struct sockaddr_in server;
	memset((char *)&server, 0, sizeof(struct sockaddr_in));

	server.sin_family = AF_INET;

	if(pte){ // peer connection;
		server.sin_port = pte->pte_peer.peer_port;
		memcpy(&server.sin_addr, &pte->pte_peer.peer_addr, sizeof(struct in_addr));
		pte->pte_sd = sd;
		peer_table.push_back(*pte);
		pte->pending = true;
		print_log("try connecting to peer ", pte);

		if(connect(pte->pte_sd, (struct sockaddr *)&server, sizeof(struct sockaddr_in))<0){
			perror("error in connecting");
			exit(1);
		}
	}
	else{ //image connection;
		server.sin_port = qry->img_peer.peer_port;
		memcpy(&server.sin_addr, &qry->img_peer.peer_addr, sizeof(struct in_addr));
		cout<<"connect to port: "<< server.sin_port<<" "<<ntohs(server.sin_port)<<endl;	
	//	cout<<"server addr is: "<< server.sin_addr<<endl;


		if (connect(sd,(struct sockaddr *)&server,sizeof(struct sockaddr_in)) < 0){
			close(sd);
			cout<< "connecting failed"<<endl; 
		}
 		cout<< "connecting success"<<endl;
		struct linger so_linger;
		so_linger.l_onoff = 1;
		so_linger.l_linger = PR_LINGER;
		setsockopt(sd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger)); 
		
	}
	return(sd);
}  


int peer::peer_recv(int index){
//	cout<<"peer_recv is called."<<endl;

	int npeers = 0; 
	int bytes =0;
	int searchID = 0; 
	int sd = peer_table[index].pte_sd;
	//buffer for peer_t;
	char buffer[PR_MAXPEERS*sizeof(peer_t)] = {0};
	char fname[NETIMG_MAXFNAME] = {0};
	peer_t orig_img;

	//image info:
	LTGA image;
	imsg_t imsg;
	long img_size;
	
	pmsg_t msg;
	//recv the header first: the header size is 4 bytes;
	bytes += recv(sd, &msg, 4, 0);
//	bytes += recv_loop(index, 4, (char*)&msg);
	
	//parse the header
	if (msg.pm_vers != PM_VERS){
		close(sd);
		fprintf(stderr, "unknown message version.\n");
		peer_table.erase(peer_table.begin()+index);
		return 1;
	}

	if(msg.pm_type == PM_SEARCH){
		searchID = ntohs(msg.pm_npeers);
		cout<< "peer_recv get search packet ID is: "<<searchID<<endl;
		//recv the orginal image address;
		bytes += recv(sd, &orig_img, sizeof(peer_t), 0);
		//recv the image name;
		bytes += recv(sd, fname, NETIMG_MAXFNAME, 0);


		query_t qry;
		memcpy((char*)&qry, (char*)&msg, 4);
		memcpy((char*)&qry+4, (char*)&orig_img, sizeof(peer_t));
		memcpy((char*)&qry+12, fname, NETIMG_MAXFNAME);


		cout<< "peer_recv, port: "<<qry.img_peer.peer_port<<endl;

		//check the duplication at the circular buffer;
		for(int i = 0; i<BUFFER_SIZE; i++){
			if(cir_buffer[i] == searchID){
				cout<<"search packet already be searched"<<endl;
				return 1;
			}
		}
		cout<<"search packet and update the buffer;"<<endl;
		cir_buffer.erase(cir_buffer.begin());
		cir_buffer.push_back(searchID);

		//find if the image is there;
		if(imgdb_loadimg(fname, &image, &imsg, &img_size) == NETIMG_FOUND){
			//connect to the original peer;
			cout<<"image was found in this peer!!!"<<endl;
			//create a new socket;
			int tmp_sd = peer_connect(0, &qry);
			cout<< "new sock is: "<<tmp_sd<<endl;
			imgdb_sendimg(tmp_sd, &imsg, &image, img_size);
			close(tmp_sd);
		}
		else{// not found, sending to other peers;
			int len = peer_table.size();
			for(int i =0; i<len; i++){
				if(index == i || peer_table[i].pending == true){
					continue;
				}
				peer_ack(&peer_table[i], PM_SEARCH, &qry);
				cout<< "sending out to other peers"<<endl;
			}
		}
	}
	else{
		//get number of return peer
		npeers = ntohs(msg.pm_npeers);
		if(npeers){
			bytes += recv(sd, buffer, npeers*sizeof(peer_t), 0);
		}
//		close(sd);
//		bytes += recv_loop(index, npeers*sizeof(peer_t), buffer);
		print_log("Received ack from ", &peer_table[index]);
		if(msg.pm_type == PM_WELCOME){
			peer_table[index].pending = false;
//			cout << "npeers is: "<< npeers <<endl;
		}
		if(msg.pm_type == PM_RDIRECT){
			print_log("Join redirected by ", &peer_table[index]);
			decline_table.push_back(peer_table[index]);
			peer_table.erase(peer_table.begin()+index);
		}
	
		if (npeers) {
			// if message contains a peer address, inform user of
			// the peer two hops away
			for(int i =0; i< npeers; i++){
				struct hostent *phost;   
				peer_t p_tmp;
				memcpy(&p_tmp, buffer+i*sizeof(peer_t), sizeof(peer_t));	
				phost = gethostbyaddr((char *) &p_tmp.peer_addr,
									sizeof(struct in_addr), AF_INET);
				fprintf(stderr, "  which is peered with: %s:%d\n", 
						((phost && phost->h_name) ? phost->h_name :
						inet_ntoa(p_tmp.peer_addr)),
						ntohs(p_tmp.peer_port));
			}
		}

		for(int i=0; i<npeers; i++){
			pte_t pte;
			memcpy(&pte.pte_peer, buffer+i*sizeof(peer_t), sizeof(peer_t));
			int newlen = peer_table.size();
			bool check1 = true;
			bool check2 = true;
			int psize = peer_table.size();
			int dsize = decline_table.size();

			//check for peer table;
			for(int j=0; j<psize; j++){
				if (peer_table[j].pte_peer.peer_addr.s_addr == pte.pte_peer.peer_addr.s_addr
				&& peer_table[j].pte_peer.peer_port == pte.pte_peer.peer_port){
//					print_log("Already connected with this peer ", &pte);
					check1 = false;
					break;
				}
			}

			//check for decline table;
			for(int j=0; j<dsize; j++){
				if (decline_table[j].pte_peer.peer_addr.s_addr == pte.pte_peer.peer_addr.s_addr
				&& decline_table[j].pte_peer.peer_port == pte.pte_peer.peer_port){
//					print_log("Already declined by this peer ", &pte);
					check2 = false;
					break;
				}
			}
 			if (newlen < maxpeer_num && check1 && check2){

				peer_connect(&pte, 0);
			}
			memset(&pte, 0, sizeof(pte_t));
		}	
	}
//	cout<< "recv total: "<<bytes<<endl;
	return 0;	
}


void peer::close_table(){
	for (unsigned int i=0; i < peer_table.size(); i++) {
		if (peer_table[i].pte_sd != PR_UNINIT_SD) {
			close(peer_table[i].pte_sd);
		}
	}
}


/*****************************************************************************************************/
int main(int argc, char *argv[]){
	fd_set rset;
	int sd, maxsd = 0;
	int img_sd = 0;
//	peer_t img_peer;
//	memset(img_peer, 0, sizeof(peer_t));


	pte_t pte_tmp;
	pte_tmp.pte_sd = PR_UNINIT_SD;
	pte_tmp.pte_peer.peer_port = 0;
	pte_tmp.pending = false;

	peer p_obj;

	if (p_obj.peer_args(argc, argv, &pte_tmp)) {
		p_obj.peer_usage(argv[0]);
	}

	/* setup peer and listen on connection*/
	sd = p_obj.peer_setup();
	img_sd = imgdb_sockinit(&p_obj.img_peer);
	p_obj.img_sd = img_sd;

	/*connect*/
	if (pte_tmp.pte_peer.peer_port) {
//		cout<< "connecting..."<<endl;
		p_obj.peer_connect(&pte_tmp, 0);
	}
	while(1){
		maxsd = 0;
		for(unsigned int i=0; i<p_obj.peer_table.size(); i++){
			maxsd = max(maxsd, p_obj.peer_table[i].pte_sd);
		}
		maxsd = max(max(sd, img_sd), maxsd);

		FD_ZERO(&rset);
		FD_SET(sd, &rset);
		//register the image socket sd;
		FD_SET(img_sd, &rset);

		for(unsigned int i=0; i<p_obj.peer_table.size(); i++){
			if (p_obj.peer_table[i].pte_sd > 0) {
				FD_SET(p_obj.peer_table[i].pte_sd, &rset);
			}
		}

		struct timeval t_value;
		t_value.tv_sec = 2;
		t_value.tv_usec = 500000;

		select(maxsd+1, &rset, NULL, NULL, &t_value); 

		//checking if there is incoming connecting peer;
		p_obj.check_connect(sd, img_sd, &rset);
		//checking if there is incomming message;
		p_obj.check_msg(&rset);
	}
	
	
	p_obj.close_table();
	close(sd);
	return 0;
}
