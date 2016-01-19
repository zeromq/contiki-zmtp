#include "push.h"


void zmq_push_init(zmq_socket_t *self) {
    self->in_conn = NULL;
    self->out_conn = NULL;

    self->recv = NULL;
    self->recv_multipart = NULL;
    self->send = zmq_socket_send_round_robin_block;
}
