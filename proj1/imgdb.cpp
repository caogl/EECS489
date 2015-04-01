#include "imgdb.h"
#include "peer.h"

/* defult constructor for imgdb, called when peer is initialized */
imgdb::imgdb()
{
  imgdb_sockinit();
}

/* initialize imgdb server listen socket */
void imgdb::imgdb_sockinit()
{
  char sname[NETIMG_MAXFNAME+1];
  memset(sname, '\0', NETIMG_MAXFNAME+1);

  td=PR_UNINIT_SD;
  sd = socket(AF_INET, SOCK_STREAM,IPPROTO_TCP);
  if(sd<0)
  {
    perror("error in creating socket");
    exit(1);
  }
  struct linger linger_time;
  linger_time.l_onoff = 1;
  linger_time.l_linger = PR_LINGER;
  setsockopt(sd, SOL_SOCKET, SO_LINGER, &linger_time, sizeof(linger_time));

  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  self.sin_family = AF_INET;
  self.sin_addr.s_addr = INADDR_ANY;
  self.sin_port = 0;

  int on = 1;
  setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
  bind(sd, (struct sockaddr *) &self, sizeof(struct sockaddr_in));
  listen(sd, NETIMG_QLEN);

  int len = sizeof(sockaddr_in);
  int status = getsockname(sd, (struct sockaddr *)&self, (socklen_t *)&len);
  if(status<0)
  {
    perror("error getsockname");
    exit(1);
  }

  // Since the image socket is bound to INADDR_ANY, cannot use getsockname() to find out the
  // address of the socket (it will return 0's)
  status = gethostname(sname, NETIMG_MAXFNAME+1);
  if(status<0)
  {
    perror("error gethostname");
    exit(1);
  }
  struct hostent *sp;
  sp = gethostbyname(sname);
  memcpy(&(self.sin_addr), sp->h_addr, sp->h_length);

  fprintf(stderr, "Imgdb socket address is %s:%d\n", sname, ntohs(self.sin_port));
}

/* if   (1): receives a packet from netimg client, check to see if image exist
 *           if yes, send the image, if not ,send the search packet
 * else (2): receives a packet from a peer image socket, 
 *           receives the image, then send to netimg client
 */     
int imgdb::handleqry()
{
  int td_tmp=imgdb_accept();
  if(imgdb_recvqry(td_tmp)==NETIMG_QRY)
  {
    td=td_tmp;
    if(imgdb_loadimg()==NETIMG_FOUND)
    {
      imgdb_sendimg(NULL);
      close(td);
      td=PR_UNINIT_SD;

      return 0;
    }
    else
      return 1;
  }
  else
  {
    //char *ip=(char *) image.GetPixels();

    double img_dsize;

    imsg.im_height=ntohs(imsg.im_height);
    imsg.im_width=ntohs(imsg.im_width);
    img_dsize = (double) (imsg.im_height*imsg.im_width*(u_short)imsg.im_depth);
    net_assert((img_dsize > (double) LONG_MAX), "netimg: image too big");
    img_size = (long) img_dsize;

    char ip[img_size];
    memset(ip, '\0', img_size);

    int byte_recv=0;
    while(byte_recv<img_size)
    {
      int err=recv(td_tmp, ip+byte_recv, img_size-byte_recv, 0);
      if(err==0)
        break;
      if(err<0)
      {
        close(td_tmp);
        perror("peer: peer_recv.\n");
        exit(1);
      }
      byte_recv+=err;
    }

    imsg.im_height=htons(imsg.im_height);
    imsg.im_width=htons(imsg.im_width);

    imgdb_sendimg(ip);

    /* very important to close the td_tmp descriptor here, 
       if not, subsequent image transfer between peers cannot succeed, 
       connect to the other image socket for image transfer takes forever
      ----> because a tcp socket is identified by source+destination
            address+port tuples, cannot estabilish the same one without 
            closing the previous one !!!*/
    close(td_tmp);                   
    close(td);
    td=PR_UNINIT_SD;

    return 0;
  }
}

/*
 * imgdb_loadimg: load TGA image from file *fname to *image.
 * Store size of image, in bytes, in *img_size.
 * Initialize *imsg with image's specifics.
 * All four variables must point to valid memory allocated by caller.
 * Terminate process on encountering any error.
 * Returns NETIMG_FOUND if *fname found, else returns NETIMG_NFOUND.
 */

int imgdb::imgdb_loadimg()
{
  int alpha, greyscale;
  double img_dsize;
  
  imsg.im_vers = NETIMG_VERS;

  image.LoadFromFile(fname);

  if (!image.IsLoaded()) 
    imsg.im_found = NETIMG_NFOUND;
  else 
  {
    imsg.im_found = NETIMG_FOUND;

    cout << "Image: " << endl;
    cout << "     Type   = " << LImageTypeString[image.GetImageType()] 
         << " (" << image.GetImageType() << ")" << endl;
    cout << "     Width  = " << image.GetImageWidth() << endl;
    cout << "     Height = " << image.GetImageHeight() << endl;
    cout << "Pixel depth = " << image.GetPixelDepth() << endl;
    cout << "Alpha depth = " << image.GetAlphaDepth() << endl;
    cout << "RL encoding = " << (((int) image.GetImageType()) > 8) << endl;
    /* use image->GetPixels()  to obtain the pixel array */
    
    img_dsize = (double) (image.GetImageWidth()*image.GetImageHeight()*(image.GetPixelDepth()/8));
    net_assert((img_dsize > (double) LONG_MAX), "imgdb: image too big");
    img_size = (long) img_dsize;
    
    imsg.im_depth = (unsigned char)(image.GetPixelDepth()/8);
    imsg.im_width = htons(image.GetImageWidth());
    imsg.im_height = htons(image.GetImageHeight());
    alpha = image.GetAlphaDepth();
    greyscale = image.GetImageType();
    greyscale = (greyscale == 3 || greyscale == 11);
    if (greyscale)
      imsg.im_format = htons(alpha ? GL_LUMINANCE_ALPHA : GL_LUMINANCE);
    else
      imsg.im_format = htons(alpha ? GL_RGBA : GL_RGB);
  }
    
  return(imsg.im_found);
}
  
/* imgdb_accept: accepts connection on the given socket, sd, return the generated socket */
int imgdb::imgdb_accept()
{
  struct sockaddr_in client;
  struct hostent *cp;

  int len = sizeof(struct sockaddr_in);
  int td_tmp = accept(sd, (struct sockaddr *)&client, (socklen_t *)&len);
  if(td_tmp<0)
  {
    perror("error in accepting");
    exit(1);
  }
  struct linger linger_time;
  linger_time.l_onoff = 1;
  linger_time.l_linger = PR_LINGER;
  setsockopt(td_tmp, SOL_SOCKET, SO_LINGER, &linger_time, sizeof(linger_time));

  struct linger linger_setup;
  linger_setup.l_onoff = 1;
  linger_setup.l_linger = NETIMG_LINGER;
  int status = setsockopt(td_tmp, SOL_SOCKET, SO_LINGER, &linger_setup, sizeof(struct linger));
  if(status<0)
  {
    perror("error in setsockopt");
    exit(1);
  }

  cp = gethostbyaddr((char *) &client.sin_addr, sizeof(struct in_addr), AF_INET);
  fprintf(stderr, "Connected from client or peer with image %s:%d\n",
          ((cp && cp->h_name) ? cp->h_name : inet_ntoa(client.sin_addr)),
          ntohs(client.sin_port));
  return td_tmp;
}

/* receive the query packet and first judge whether it is from netimg querying for an image,
 * or from another peer transferring an image, copy the received packet into correct struct 
 * and return the type of the struct 
 */
int imgdb::imgdb_recvqry(int td_tmp)
{
  char header[2];
  int bytes=0;
  bytes+=recv(td_tmp, header, 2, 0);
  if(header[1]==NETIMG_QRY)
  {
    iqry_t iqry;
    memcpy(&iqry, header, 2);
    bytes = recv(td_tmp, (char *)&iqry+2, sizeof(iqry_t)-2, 0);
    if (bytes<0 || iqry.iq_vers!= NETIMG_VERS)
    {
      perror("error in recv or wrong vers");
      exit(1);
    }
    memset(fname, '\0', NETIMG_MAXFNAME+1);
    strcpy(fname, iqry.iq_name);
    return NETIMG_QRY;
  }
  else
  {
    memcpy(&imsg, header, 2);
    bytes+=recv(td_tmp, (char*)&imsg+2, sizeof(imsg_t)-2, 0);
    if (bytes<0 || bytes!=(int)sizeof(imsg_t))
    {
      perror("error in recv imsg packet");
      exit(1);
    }
    return NETIMG_RPY;
  }
}

/*
 * imgdb_sendimg: send the image to the client
 * First send the specifics of the image (width, height, etc.)
 * contained in *imsg to the client.  *imsg must have been
 * initialized by caller.
 * Then send the image contained in *image, but for future
 * debugging purposes we're going to send the image in
 * chunks of segsize instead of as one single image.
 * We're going to send the image slowly, one chunk for every
 * NETIMG_USLEEP microseconds.
 *
 * Terminate process upon encountering any error.
 * Doesn't otherwise modify anything.
*/
void imgdb::imgdb_sendimg(char* img)
{
  int segsize;
  char *ip;
  int bytes;
  long left;

  bytes = send(td, &imsg, sizeof(imsg_t), 0);
  if(bytes<0)
  {
    perror("error in sending imsg");
    exit(1);
  }

  if (imsg.im_found == NETIMG_FOUND) 
  {
    segsize = img_size/NETIMG_NUMSEG;                     /* compute segment size */
    segsize = segsize < NETIMG_MSS ? NETIMG_MSS : segsize; /* but don't let segment be too small*/
    
    if(!img) 
      ip = (char *) image.GetPixels();    /* ip points to the start of byte buffer holding image */
    else
      ip=img;
    
    for (left = img_size; left; left -= bytes) // "bytes" contains how many bytes was sent at the last iteration.
    {  
      if(segsize>left)
      {
        segsize = left;
      }
      bytes = send(td, ip+img_size-left, segsize, 0);
      if(bytes<0)
      {
        perror("error in sending image");
        exit(1);
      }

      fprintf(stderr, "imgdb_send: size %d, sent %d\n", (int) left, bytes);
      usleep(NETIMG_USLEEP);
    }
  }

  return;
}


