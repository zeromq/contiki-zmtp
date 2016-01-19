#ifndef ZMQ_PUB_H_
#define ZMQ_PUB_H_

#include "zmtp.h"

void zmq_pub_init(zmq_socket_t *self);
PT_THREAD(zmq_pub_send(zmq_socket_t *self, zmq_msg_t *msg));

#endif
