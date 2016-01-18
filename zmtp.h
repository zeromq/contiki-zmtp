#ifndef ZMTP_H_
#define ZMTP_H_

#include "zmq.h"

int zmtp_connection_add_in_msg(zmtp_connection_t *self, zmq_msg_t *msg);
zmq_msg_t *zmtp_connection_pop_in_msg(zmtp_connection_t *self);
int zmtp_connection_add_out_msg(zmtp_connection_t *self, zmq_msg_t *msg);
zmq_msg_t *zmtp_connection_pop_out_msg(zmtp_connection_t *self);

zmtp_connection_t *zmtp_connection_new();
void zmtp_connection_destroy (zmtp_connection_t **self_p);
void zmtp_connection_init(zmtp_connection_t *self);
int zmtp_connection_tcp_connect (zmtp_connection_t *self);
int zmtp_connection_tcp_listen (zmtp_connection_t *self);

void zmtp_init();
int zmtp_listen(zmtp_channel_t *chan, unsigned short port);

zmtp_channel_t *zmtp_channel_new (zmq_socket_type_t socket_type, struct process *p);
void zmtp_channel_destroy (zmtp_channel_t **self_p);
void zmtp_channel_init(zmtp_channel_t *self, zmq_socket_type_t socket_type, struct process *p);

int zmtp_process_post(process_event_t ev, process_data_t data);

#endif
