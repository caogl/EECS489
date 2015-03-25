#include "imgdb.h"

/* defult constructor for imgdb, called when peer is initialized */
imgdb::imgdb()
{
  imgdb_sockinit();
}

/* initialize imgdb server listen socket */
void imgdb::imgdb_sockinit()
{
  struct sockaddr_in self;
  char sname[NETIMG_MAXFNAME+1];
  memset(sname, '\0', NETIMG_MAXFNAME+1);

  sd = socket(AF_INET, SOCK_STREAM,IPPROTO_TCP);
  if(sd<0)
  {
    perror("error in creating socket");
    exit(1);
  }
  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  self.sin_family = AF_INET;
  self.sin_addr.s_addr = INADDR_ANY;
  self.sin_port = 0;
  
  bind(sd, (struct sockaddr *) &self, sizeof(struct sockaddr_in));
  listen(sd, NETIMG_QLEN);

  struct sockaddr_in tmp;
  int len = sizeof(sockaddr_in);
  int status = getsockname(sd, (struct sockaddr *)&tmp, (socklen_t *)&len);
  if(status<0)
  {
    perror("error getsockname");
    exit(1);
  }
  self.sin_port = tmp.sin_port;
  status = gethostname(sname, NETIMG_MAXFNAME+1);
  if(status<0)
  {
    perror("error gethostname");
    exit(1);
  }

  fprintf(stderr, "imgdb socket address is %s:%d\n", sname, ntohs(self.sin_port));
}

/* if (1) receives a query from netimg client, check to see if image exist
 *        if yes, send the image, if not ,send the search packet
 *    (2) receives a image transfer ......
 */     
int imgdb::handleqry()
{
  int td_tmp=imgdb_accept();
  if(imgdb_recvqry(td_tmp)==NETIMG_QRY)
  {
    td=td_tmp;
    if(imgdb_loadimg()==NETIMG_FOUND)
    {
      imgdb_sendimg();
      return 0;
    }
    else
      return 1;
  }
  else
  {
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
  if(td<0)
  {
    perror("error in accepting");
    exit(1);
  }

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
    cout<<iqry.iq_vers<<endl;
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
    if (bytes<0)
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
void imgdb::imgdb_sendimg()
{
  int segsize;
  char *ip;
  int bytes;
  long left;

  /* Task 2: YOUR CODE HERE
   * Send the imsg packet to client connected to socket td.
   */
  /* YOUR CODE HERE */
  bytes = send(td, &imsg, sizeof(imsg_t), 0);
  if(bytes<0)
  {
    perror("error in sending");
    exit(1);
  }

  if (&image) 
  {
    segsize = img_size/NETIMG_NUMSEG;                     /* compute segment size */
    segsize = segsize < NETIMG_MSS ? NETIMG_MSS : segsize; /* but don't let segment be too small*/

    ip = (char *) image.GetPixels();    /* ip points to the start of byte buffer holding image */
    
    for (left = img_size; left; left -= bytes) {  // "bytes" contains how many bytes was sent
      // at the last iteration.

      /* Task 2: YOUR CODE HERE
       * Send one segment of data of size segsize at each iteration.
       * The last segment may be smaller than segsize
       */
      /* YOUR CODE HERE */
      if(segsize>left)
      {
        segsize = left;
      }
      bytes = send(td, ip+img_size-left, segsize, 0);
      if(bytes<0)
      {
        perror("error in sending");
        exit(1);
      }

      fprintf(stderr, "imgdb_send: size %d, sent %d\n", (int) left, bytes);
      usleep(NETIMG_USLEEP);
    }
  }

  return;
}


