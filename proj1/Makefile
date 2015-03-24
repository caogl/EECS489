CC = g++
MKDEP=makedepend -Y
OS := $(shell uname)
ifeq ($(OS), Darwin)
  LIBS = -framework OpenGL -framework GLUT -lc
  CFLAGS = -g -Wall -Wno-deprecated
else
  LIBS = -lGL -lGLU -lglut
  CFLAGS = -g -Wall -Wno-deprecated
endif
  
BINS = netimg p2pdb
HDRS = ltga.h netimg.h
SRCS = ltga.cpp
HDRS_SLN = 
SRCS_SLN = peer.cpp imgdb.cpp netimg.cpp netimglut.cpp
OBJS = $(SRCS:.cpp=.o) $(SRCS_SLN:.cpp=.o)

all: $(BINS)

netimg: netimg.o netimglut.o netimg.h
	$(CC) $(CFLAGS) -o $@ $< netimglut.o $(LIBS)

p2pdb: peer.o imgdb.o ltga.o
	$(CC) $(CFLAGS) -o $@ $< imgdb.o ltga.o

%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c $<

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

.PHONY: clean
clean: 
	-rm -f -r $(OBJS) *.o *~ *core* p2pdb

depend: $(SRCS) $(SRCS_SLN) $(HDRS) $(HDRS_SLN) Makefile
	$(MKDEP) $(CFLAGS) $(SRCS) $(SRCS_SLN) $(HDRS) $(HDRS_SLN) >& /dev/null

# DO NOT DELETE

ltga.o: ltga.h
peer.o: ltga.h netimg.h
imgdb.o: ltga.h netimg.h
netimg.o: netimg.h
netimglut.o: netimg.h
