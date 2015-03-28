CC = g++
MKDEP=makedepend -Y
OS := $(shell uname)
ifeq ($(OS), Darwin)
  LIBS = -framework OpenGL -framework GLUT
  CFLAGS = -g -Wall -Wno-deprecated
else
  LIBS = -lGL -lGLU -lglut
  CFLAGS = -g -Wall -Wno-deprecated
endif

BINS = netimg imgdb
HDRS = socks.h netimg.h imgdb.h ltga.h
SRCS = ltga.cpp netimglut.cpp
HDRS_SLN = fec.h
SRCS_SLN = socks.cpp netimg.cpp imgdb.cpp
OBJS = $(SRCS:.cpp=.o) $(SRCS_SLN:.cpp=.o)

all: netimg imgdb

netimg: netimg.o socks.o netimglut.o netimg.h
	$(CC) $(CFLAGS) -o $@ $< netimglut.o socks.o $(LIBS)

imgdb: imgdb.o socks.o ltga.o $(HDRS)
	$(CC) $(CFLAGS) -o $@ $< ltga.o socks.o

%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c $<

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

.PHONY: clean
clean: 
	-rm -f -r $(OBJS) *.o *~ *core* netimg $(BINS)

depend: $(SRCS) $(SRCS_SLN) $(HDRS) $(HDRS_SLN) Makefile
	$(MKDEP) $(CFLAGS) $(SRCS) $(SRCS_SLN) $(HDRS) $(HDRS_SLN) >& /dev/null

# DO NOT DELETE

ltga.o: ltga.h
netimglut.o: netimg.h
socks.o: netimg.h
netimg.o: netimg.h socks.h fec.h
imgdb.o: ltga.h socks.h netimg.h imgdb.h fec.h
imgdb.o: ltga.h socks.h
