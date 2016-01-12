#CONTIKI_PROJECT=my-zmq-router
#all: $(CONTIKI_PROJECT) 
all: my-zmq-router
#all: example-psock-server

#PROJECT_SOURCEFILES += zmq.c
#PROJECT_SOURCEFILES += my-zmq-router.c

CONTIKI=contiki
CONTIKI_WITH_IPV6 = 1
include $(CONTIKI)/Makefile.include
