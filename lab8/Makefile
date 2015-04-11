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
HDRS = ltga.h socks.h 
SRCS = ltga.cpp netimglut.cpp socks.cpp netimg.cpp
HDRS_SLN = imgdb.h netimg.h
SRCS_SLN = imgdb.cpp
OBJS = $(SRCS:.cpp=.o) $(SRCS_SLN:.cpp=.o)

all: netimg imgdb

netimg: netimg.o netimglut.o socks.o $(HDRS)
	$(CC) $(CFLAGS) -o $@ $< netimglut.o socks.o $(LIBS)

imgdb: imgdb.o ltga.o socks.o $(HDRS)
	$(CC) $(CFLAGS) -o $@ $< ltga.o socks.o
	
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

imgdb.o: netimg.h imgdb.h
imgdb.o: netimg.h
