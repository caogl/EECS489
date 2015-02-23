CC = gcc
CPP = g++
MKDEP=makedepend -Y
OS := $(shell uname)
ifeq ($(OS), Darwin)
  LIBS = -framework OpenGL -framework GLUT -lc
	CFLAGS = -g -Wall -Wno-deprecated -Wno-c++11-extensions -DLAB3
else
	CC = /usr/um/gcc-4.8.2/bin/gcc
	CPP = /usr/um/gcc-4.8.2/bin/g++
  LIBS = -lGL -lGLU -lglut -lcrypto
  CFLAGS = -g -Wall -Wno-deprecated -Wno-c++11-extensions -std=c++11 -DLAB3
endif

BINS = imgdb netimg
HDRS = netimg.h ltga.h hash.h imgdb.h socks.h
SRCS = ltga.cpp socks.cpp netimglut.cpp netimg.cpp
HDRS_SLN = 
SRCS_SLN = imgdb.cpp hash.cpp
OBJS = $(SRCS_SLN:.cpp=.o) ltga.o socks.o 

all: $(BINS)

imgdb: $(OBJS) $(HDRS)
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

depend: $(SRCS) $(SRCS_SLN) $(HDRS) $(HDRS_SLN) Makefile
	$(MKDEP) $(CFLAGS) $(SRCS) $(SRCS_SLN) $(HDRS) $(HDRS_SLN) >& /dev/null

# DO NOT DELETE

ltga.o: ltga.h
socks.o: netimg.h
netimglut.o: netimg.h
netimg.o: netimg.h socks.h
imgdb.o: ltga.h netimg.h socks.h hash.h imgdb.h
hash.o: netimg.h hash.h
imgdb.o: ltga.h hash.h
