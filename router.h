#ifndef ZMQ_ROUTER_H_
#define ZMQ_ROUTER_H_

#include "zmtp.h"


void zmq_router_init(zmq_socket_t *self);
//void zmq_router_signal(zmq_socket_t *self, zmtp_channel_signal_t signal);
int zmq_router_connect (zmq_socket_t *self, uip_ipaddr_t *addr, unsigned short port);
int zmq_router_bind (zmq_socket_t *self, unsigned short port);
PT_THREAD(zmq_router_recv(zmq_socket_t *self, zmq_msg_t **msg_ptr));
PT_THREAD(zmq_router_recv_multipart(zmq_socket_t *self, list_t msg_list));
PT_THREAD(zmq_router_send(zmq_socket_t *self, zmq_msg_t *msg_ptr));

#endif
