#ifndef ZMQ_ROUTER_H_
#define ZMQ_ROUTER_H_

#include "zmtp.h"


void zmq_router_init(zmq_socket_t *self);
PT_THREAD(zmq_router_recv(zmq_socket_t *self, zmq_msg_t **msg_ptr));
PT_THREAD(zmq_router_recv_multipart(zmq_socket_t *self, list_t msg_list));
PT_THREAD(zmq_router_send(zmq_socket_t *self, zmq_msg_t *msg_ptr));

#endif
