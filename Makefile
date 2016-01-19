all: my-zmq-dealer my-zmq-pub my-zmq-sub

PROJECT_SOURCEFILES = zmtp.c router.c dealer.c push.c pull.c pub.c sub.c zmq.c

CONTIKI=contiki
CONTIKI_WITH_IPV6 = 1
include $(CONTIKI)/Makefile.include
