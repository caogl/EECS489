CC = gcc
CPP = g++
CFLAGS = -g -Wall -Wno-deprecated -Wno-c++11-extensions
MKDEP=makedepend -Y
OS := $(shell uname)
ifeq ($(OS), Darwin)
  LIBS = -framework OpenGL -framework GLUT -lc
	CFLAGS = -g -Wall -Wno-deprecated -Wno-c++11-extensions
else
	CC = /usr/um/gcc-4.8.2/bin/gcc
	CPP = /usr/um/gcc-4.8.2/bin/g++
  LIBS = -lGL -lGLU -lglut -lcrypto
  CFLAGS = -g -Wall -Wno-deprecated -Wno-c++11-extensions -Wmaybe-uninitialized -std=c++11
endif

BINS = dhtn netimg
HDRS = netimg.h hash.h ltga.h imgdb.h socks.h
SRCS = ltga.cpp socks.cpp hash.cpp imgdb.cpp
HDRS_SLN = dhtn.h
SRCS_SLN = dhtn.cpp
OBJS = $(SRCS_SLN:.cpp=.o) $(SRCS:.cpp=.o)

all: $(BINS)

dhtn: $(OBJS) $(HDRS)
	$(CPP) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

netimg: netimg.o netimglut.o socks.o netimg.h socks.h
	$(CPP) $(CFLAGS) -o $@ $< netimglut.o socks.o $(LIBS)

%.o: %.cpp
	$(CPP) $(CFLAGS) $(INCLUDES) -c $<

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

.PHONY: clean
clean: 
	-rm -f -r $(OBJS) *.o *~ *core* $(BINS)

depend: $(SRCS_SLN) $(HDRS_SLN) Makefile
	$(MKDEP) $(CFLAGS) $(SRCS_SLN) $(HDRS_SLN) >& /dev/null

# DO NOT DELETE

dhtn.o: dhtn.h
