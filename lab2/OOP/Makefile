CC = g++
MKDEP=makedepend -Y
LIBS =
CFLAGS = -g -Wall

HDRS =
SRCS =
HDRS_SLN = 
SRCS_SLN = peer.cpp
OBJS = $(SRCS:.cpp=.o) $(SRCS_SLN:.cpp=.o)

peer: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

wipe: $(OBJS:.o=.cpp)
	-rm -f $(OBJS)

caen: cleanslate $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c $<

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

.PHONY: clean
clean: 
	-rm -f -r $(OBJS) *.o *~ *core* peer

depend: $(SRCS) $(SRCS_SLN) $(HDRS) $(HDRS_SLN) Makefile
	$(MKDEP) $(CFLAGS) $(SRCS) $(SRCS_SLN) $(HDRS) $(HDRS_SLN) >& /dev/null

# DO NOT DELETE
