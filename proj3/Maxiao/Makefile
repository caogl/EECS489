CC = g++
MKDEP=/usr/X11R6/bin/makedepend -Y
OS := $(shell uname)
ifeq ($(OS), Darwin)
  LIBS = -framework OpenGL -framework GLUT
  CFLAGS = -g -Wall -Wno-deprecated
else
  LIBS = -lGL -lGLU -lglut
  CFLAGS = -g -Wall -Wno-deprecated
endif

BINS = netimg imgdb
HDRS = netimg.h ltga.h
SRCS = ltga.cpp netimglut.cpp
HDRS_SLN = fec.h 
SRCS_SLN = netimg.cpp imgdb.cpp fec.cpp
OBJS = $(SRCS:.cpp=.o) $(SRCS_SLN:.cpp=.o)

all: netimg imgdb

netimg: netimg.o netimglut.o fec.o $(HDRS)
	$(CC) $(CFLAGS) -o $@ $< netimglut.o fec.o $(LIBS)

imgdb: imgdb.o ltga.o fec.o $(HDRS)
	$(CC) $(CFLAGS) -o $@ $< ltga.o fec.o

%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c $<

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

.PHONY: clean
clean: 
	-rm -f -r $(OBJS) *.o *~ *core* netimg $(BINS)

depend: $(SRCS) $(SRCS_SLN) $(HDRS) $(HDRS_SLN) Makefile
	$(MKDEP) $(CFLAGS) $(SRCS) $(SRCS_SLN) $(HDRS) $(HDRS_SLN) >& /dev/null

altdepend: $(ALTSRCS_SLN) $(ALTHDRS) $(HDRS_SLN) Makefile
	$(MKDEP) $(CFLAGS) $(ALTSRCS_SLN) $(ALTHDRS) $(HDRS_SLN) >& /dev/null

# DO NOT DELETE

ltga.o: ltga.h
netimglut.o: netimg.h
netimg.o: netimg.h fec.h
imgdb.o: ltga.h netimg.h fec.h
fec.o: ltga.h
