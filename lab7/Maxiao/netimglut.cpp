/* 
 * Copyright (c) 2014 University of Michigan, Ann Arbor.
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
#include <unistd.h>        // getopt(), STDIN_FILENO, gethostname()
#include <signal.h>        // signal()
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#include "netimg.h"

extern int sd;
int wd;                   /* GLUT window handle */
GLdouble width, height;   /* window width and height */

void
netimg_imginit()
{
  int tod;

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glPolygonMode(GL_FRONT, GL_FILL);

  glGenTextures(1, (GLuint*) &tod);
  glBindTexture(GL_TEXTURE_2D, tod);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); 
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); 
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); 
  glEnable(GL_TEXTURE_2D);
}

/* Callback functions for GLUT */

void 
netimg_display(void)
{
  /* If your image is displayed upside down, you'd need to play with the
     texture coordinate to flip the image around. */
  glBegin(GL_QUADS); 
    glTexCoord2f(0.0,1.0); glVertex3f(0.0, height, 0.0);
    glTexCoord2f(0.0,0.0); glVertex3f(0.0, 0.0, 0.0);
    glTexCoord2f(1.0,0.0); glVertex3f(width, 0.0, 0.0);
    glTexCoord2f(1.0,1.0); glVertex3f(width, height, 0.0);
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
    close(sd);
#ifdef _WIN32
    WSACleanup();
#endif
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
  height = NETIMG_HEIGHT;         /* within which we draw. */

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
