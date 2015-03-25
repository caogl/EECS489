# for using g++ version 4.7.0 on CAEN
PATH := /usr/um/gcc-4.7.0/bin:$(PATH)
LD_LIBRARY_PATH := /usr/um/gcc-4.7.0/lib64
LD_RUN_PATH := /usr/um/gcc-4.7.0/lib64

CC = g++
OS := $(shell uname)
STDFLAG = -std=c++11
ifeq ($(OS), Darwin)
  LIBS = -framework OpenGL -framework GLUT -lc
  CFLAGS = -g -Wall -Wno-deprecated -m32
else
  LIBS = -lGL -lGLU -lglut
  CFLAGS = -g -Wall -Wno-deprecated
endif


OBJS = main.o peer.o ltga.o
peer: $(OBJS)
	${CC} ${CFLAGS} $(OBJS) -o peer ${LIBS} ${STDFLAG}

main.o: main.cpp
	${CC} ${CFLAGS} -c main.cpp ${LIBS} ${STDFLAG}

peer.o:  peer.cpp peer.h
	${CC} ${CFLAGS} -c peer.cpp ${LIBS} ${STDFLAG}

ltga.o: ltga.cpp ltga.h
	${CC} ${CFLAGS} -c ltga.cpp ${LIBS} ${STDFLAG}


clean:	
	rm -f peer *.o

