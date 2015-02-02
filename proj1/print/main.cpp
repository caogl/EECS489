#include "peer.h"

/* Global variables */
int sd, td, imgsd, maxsd;
PEER peer;
fd_set rset;
struct timeval tv;
int wd;                   /* GLUT window handle */
GLdouble width, height;   /* window width and height */

imsg_t imsg;
char *image;
long img_size;
long img_offset;

/* GLUT Display */
void dispd_recvimsg(int sd){
   int bytes = recv(sd, &imsg, sizeof(imsg_t), 0);
   if (bytes < 0 || imsg.pm_vers != PM_VERS){
     perror("recv");
     printf("Received wrong packet\n");
     abort();
   }

   imsg.pm_format = ntohs(imsg.pm_format);
   imsg.pm_width = ntohs(imsg.pm_width);
   imsg.pm_height = ntohs(imsg.pm_height);
}

void dispd_imginit(){
  int tod;
  double img_dsize;

  img_dsize = (double) (imsg.pm_height*imsg.pm_width*(u_short)imsg.pm_depth);
  net_assert((img_dsize > (double) LONG_MAX), "dispd: image too big");
  img_size = (long) img_dsize;                 // global
  image = (char *)malloc(img_size*sizeof(char));

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glGenTextures(1, (GLuint*) &tod);
  glBindTexture(GL_TEXTURE_2D, tod);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  glEnable(GL_TEXTURE_2D);
}

int dispd_recvimage(int sd) {
  int bytes = 0;
  while (img_offset < img_size) {
    bytes = recv(sd, image+img_offset, img_size-img_offset, 0);
    if (bytes < 0){
      perror("recv");
      printf("Receive failed\n");
      abort();
    }
    img_offset += bytes;

    /* give the updated image to OpenGL for texturing */
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint) imsg.pm_format,
                 (GLsizei) imsg.pm_width, (GLsizei) imsg.pm_height, 0,
                 (GLenum) imsg.pm_format, GL_UNSIGNED_BYTE, image);
    /* redisplay */
    glutPostRedisplay();
  }
  return bytes;
}


void dispd_select(){
  if (!tv.tv_sec && !tv.tv_usec){ // reset time
    tv = {PR_TIMER, 0}; 
  }

  // setup rset
  FD_ZERO(&rset);
  FD_SET(sd, &rset);
  if (imgsd >= 0) 
    FD_SET(imgsd, &rset);
  maxsd = std::max(std::max(sd, imgsd), peer.set_fdset(&rset));
  select(maxsd+1, &rset, NULL, NULL, &tv);
  
  // check if image has been found
  if (imgsd >= 0){
    if (FD_ISSET(imgsd, &rset)){
      td = peer.peer_accept(imgsd, 0);
      close(imgsd);
      imgsd = PR_UNINIT_SD; 
      dispd_recvimsg(td);
      dispd_imginit();
      dispd_recvimage(td);
      close(td);
      td = PR_UNINIT_SD;
    } else if (!tv.tv_sec && !tv.tv_usec) {
      // time's up, close connection 
      fprintf(stderr, "Image not found\n");
      close(imgsd);
      imgsd = PR_UNINIT_SD;
    }
  }

  // listen to income connecting peer
  peer.check_connect(sd, &rset); 
  // check messages from any peer in the peer tables
  peer.check_peer(imgsd, &rset); 
}

void dispd_reshape(int w, int h) {
  /* save new screen dimensions */
  width = (GLdouble) w;
  height = (GLdouble) h;

  /* tell OpenGL to use the whole window for drawing */
  glViewport(0, 0, (GLsizei) w, (GLsizei) h);

  /* do an orthographic parallel projection with the coordinate
   * system set to first quadrant, limited by screen/window size */
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(0.0, width, 0.0, height);
}

void dispd_kbd(unsigned char key, int x, int y) {
  switch((char)key) {
    case 'q':
    case 27:
      glutDestroyWindow(wd);
      exit(0);
      break;
    default:
      break;
  }
}

void dispd_display(void) {
  glPolygonMode(GL_FRONT, GL_POINT);

  /* If your image is displayed upside down, you'd need to play with the
   * texture coordinate to flip the image around. */
  glBegin(GL_QUADS);
  glTexCoord2f(0.0,1.0); glVertex3f(0.0, 0.0, 0.0);
  glTexCoord2f(0.0,0.0); glVertex3f(0.0, height, 0.0);
  glTexCoord2f(1.0,0.0); glVertex3f(width, height, 0.0);
  glTexCoord2f(1.0,1.0); glVertex3f(width, 0.0, 0.0);
  glEnd();
  glFlush();
}

void dispd_glutinit(int *argc, char *argv[]){

  width  = PR_WIDTH;    /* initial window width and height, */
  height = PR_HEIGHT;         /* within which we draw. */

  glutInit(argc, argv);
  glutInitDisplayMode(GLUT_SINGLE | GLUT_RGBA);
  glutInitWindowSize((int) PR_WIDTH, (int) PR_HEIGHT);
  wd = glutCreateWindow("Netimg Display" /* title */ );   // wd global
  glutDisplayFunc(dispd_display);
  glutReshapeFunc(dispd_reshape);
  glutKeyboardFunc(dispd_kbd);
  glutIdleFunc(dispd_select); 
}

int main(int argc, char *argv[]) {
  tv = {PR_TIMER, 0}; 
 
  pte_t pte;
  pte.pending = false;
  
  // parse the command
  bool flag = peer.peer_args(argc, argv, &pte); 
  // listen to peer connection
  sd = peer.peer_setup(true); 
  // if flag is true, listen to image transfer; 
  imgsd = flag ? peer.peer_setup(false) : PR_UNINIT_SD;
  // if -p option is given, connect to the peer
  if (pte.pending) { 
    peer.peer_connect(&pte, 0);
  }

  if (imgsd >= 0) { // open a window to display 
    char fake[] = "fake";
    char *fakeargv[] = { fake, NULL    };
    int fakeargc = 1;
    dispd_glutinit(&fakeargc, fakeargv);
    glutMainLoop();
  } else {
    while(true) dispd_select();
  }

  exit(0);
}

