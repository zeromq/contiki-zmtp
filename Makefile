all: my-zmq-dealer my-zmq-pub my-zmq-sub

# If you save target to Makefile.target, do add these lines as well to it
ifeq ($(TARGET),minimal-net)
    CONTIKI_WITH_RPL = 0
    CFLAGS += -DHARD_CODED_ADDRESS=\"fdfd::10\"
endif

PROJECT_SOURCEFILES = zmtp.c router.c dealer.c push.c pull.c pub.c sub.c zmq.c

CONTIKI=contiki
CONTIKI_WITH_IPV6 = 1
include $(CONTIKI)/Makefile.include
