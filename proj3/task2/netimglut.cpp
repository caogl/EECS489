/* 
 * Copyright (c) 2014, 2015 University of Michigan, Ann Arbor.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of Michigan, Ann Arbor. The name of the University 
 * may not be used to endorse or promote products derived from this 
 * software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Author: Sugih Jamin (jamin@eecs.umich.edu)
 *
*/
#include <stdio.h>         // fprintf(), perror(), fflush()
#include <stdlib.h>        // atoi()
#include <assert.h>        // assert()
#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#include "netimg.h"

int wd;                   /* GLUT window handle */
GLdouble width, height;   /* window width and height */

extern char *image;
extern long img_size;    

void
netimg_imginit(unsigned short format)
{
  int i, tod;

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glPolygonMode(GL_FRONT, GL_FILL);

  glGenTextures(1, (GLuint*) &tod);
  glBindTexture(GL_TEXTURE_2D, tod);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); 
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); 
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); 
  glEnable(GL_TEXTURE_2D);

/* 
#if PBO
  int pbod;

  glGenBuffers(1, (GLuint*) &pbod); 
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbod);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, img_size, NULL, GL_DYNAMIC_DRAW);  
#else
*/
  image = (char *)calloc(img_size, sizeof(unsigned char));

  /* determine pixel size */
  switch(format) {
  case GL_RGBA:
    format = 4;
    break;
  case GL_RGB:
    format = 3;
    break;
  case GL_LUMINANCE_ALPHA:
    format = 2;
    break;
  default:
    format = 1;
    break;
  }

  /* paint the image texture background red if color, white
     otherwise to better visualize lost segments */
  for (i = 0; i < img_size; i += format) {
    image[i] = (unsigned char) 0xff;
  }

  return;
}

/* Callback functions for GLUT */

void 
netimg_display(void)
{
  /* If your image is displayed upside down, you'd need to play with the
     texture coordinate to flip the image around. */
  glBegin(GL_QUADS); 
    glTexCoord2f(0.0,1.0); glVertex3f(0.0, 0.0, 0.0);
    glTexCoord2f(0.0,0.0); glVertex3f(0.0, height, 0.0);
    glTexCoord2f(1.0,0.0); glVertex3f(width, height, 0.0);
    glTexCoord2f(1.0,1.0); glVertex3f(width, 0.0, 0.0);
    /* alternate coordinates:
    glTexCoord2f(0.0,0.0); glVertex3f(0.0, 0.0, 0.0);
    glTexCoord2f(0.0,1.0); glVertex3f(0.0, height, 0.0);
    glTexCoord2f(1.0,1.0); glVertex3f(width, height, 0.0);
    glTexCoord2f(1.0,0.0); glVertex3f(width, 0.0, 0.0);
    */
  glEnd();

  glFlush();
}

void
netimg_reshape(int w, int h)
{
  /* save new screen dimensions */
  width = (GLdouble) w;
  height = (GLdouble) h;

  /* tell OpenGL to use the whole window for drawing */
  glViewport(0, 0, (GLsizei) w, (GLsizei) h);

  /* do an orthographic parallel projection with the coordinate
     system set to first quadrant, limited by screen/window size */
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(0.0, width, 0.0, height);
}

void
netimg_kbd(unsigned char key, int x, int y)
{
  switch((char)key) {
  case 'q':
  case 27:
    glutDestroyWindow(wd);
/* stdin is not a descriptor on Windows, so 'q' not supported
    close(sd);
#ifdef _WIN32
    WSACleanup();
#endif
*/
    exit(0);
    break;
  default:
    break;
  }

  return;
}

void
netimg_glutinit(int *argc, char *argv[], void (*idlefunc)())
{

  width  = NETIMG_WIDTH;    /* initial window width and height, */
  height = NETIMG_HEIGHT;   /* within which we draw. */

  glutInit(argc, argv);
  glutInitDisplayMode(GLUT_SINGLE | GLUT_RGBA);
  glutInitWindowSize((int) NETIMG_WIDTH, (int) NETIMG_HEIGHT);
  wd = glutCreateWindow("Netimg Display" /* title */ );   // wd global
  glutDisplayFunc(netimg_display);
  glutReshapeFunc(netimg_reshape);
  glutKeyboardFunc(netimg_kbd);
  glutIdleFunc(idlefunc);

  return;
} 
