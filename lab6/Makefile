CC = g++
MKDEP=makedepend -Y
OS := $(shell uname)
ifeq ($(OS), Darwin)
  LIBS = -framework OpenGL -framework GLUT
  CFLAGS = -g -Wall -Wno-deprecated $(DASGN)
else
  LIBS = -lGL -lGLU -lglut
  CFLAGS = -g -Wall -Wno-deprecated $(DASGN)
endif

BINS = netimg imgdb
HDRS = netimg.h ltga.h fec.h
SRCS = ltga.cpp netimglut.cpp netimg.cpp imgdb.cpp
HDRS_SLN = 
SRCS_SLN = fec.cpp
OBJS = $(SRCS:.cpp=.o) $(SRCS_SLN:.cpp=.o)

all: netimg imgdb

netimg: netimg.o netimglut.o fec.o socks.o $(HDRS)
	$(CC) $(CFLAGS) -o $@ $< netimglut.o fec.o socks.o $(LIBS)

imgdb: imgdb.o ltga.o fec.o socks.o $(HDRS)
	$(CC) $(CFLAGS) -o $@ $< ltga.o fec.o socks.o

%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c $<

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

.PHONY: clean
clean: 
	-rm -f -r $(OBJS) *.o *~ *core* netimg $(BINS)

depend: $(SRCS_SLN) $(HDRS_SLN) Makefile
	$(MKDEP) $(CFLAGS) $(SRCS_SLN) $(HDRS_SLN) >& /dev/null

# DO NOT DELETE
