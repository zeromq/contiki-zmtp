#ifndef ZMQ_SUB_H_
#define ZMQ_SUB_H_

#include "zmtp.h"

void zmq_sub_init(zmq_socket_t *self);
PT_THREAD(zmq_sub_subscribe(zmq_socket_t *self, const char *topic));

#endif
