#include "router.h"

#include <stdio.h>

#define LOCAL_PT(pt_) static struct pt pt_; static uint8_t pt_inited=0; if(pt_inited == 0) { PT_INIT(&pt_); pt_inited = 1; }


void zmq_router_init(zmq_socket_t *self) {
  self->connect = zmq_router_connect;
  // self->signal = zmq_router_signal;
  self->bind = zmq_router_bind;
  self->recv = zmq_router_recv;
  self->recv_multipart = zmq_router_recv_multipart;
  self->send = zmq_router_send;
}

/*void zmq_router_signal(zmq_socket_t *self, zmtp_channel_signal_t signal) {
    switch(signal) {
      case ZMTP_PEER_DISCONNECT:

        break;
      default:
        break;
    }
}*/

int zmq_router_connect (zmq_socket_t *self, uip_ipaddr_t *addr, unsigned short port) {
    // TODO: implement
    return -1;
}

int zmq_router_bind (zmq_socket_t *self, unsigned short port) {
    return zmtp_listen(&self->channel, port);
}

PT_THREAD(zmq_router_recv(zmq_socket_t *self, zmq_msg_t **msg_ptr)) {
    // TODO: redo this function
    LOCAL_PT(pt);
    // printf("> zmq_router_recv %d\n", pt.lc);
    PT_BEGIN(&pt);

    if(self->current_conn == NULL)
        self->current_conn = list_head(self->channel.connections);
    else{
        // Detect if the current_conn disconnected meanwhile
        zmtp_connection_t *scan = list_head(self->channel.connections);
        while(scan != self->current_conn)
            scan = list_item_next(scan);
        if(scan != self->current_conn)
            self->current_conn = list_head(self->channel.connections);
    }

    zmq_msg_t *msg = NULL;
    while(1) {
        while(self->current_conn != NULL) {
            msg = zmtp_connection_pop_in_msg(self->current_conn);
            if(msg != NULL) {
                *msg_ptr = msg;
                if((zmq_msg_flags(msg) & ZMQ_MSG_MORE) != ZMQ_MSG_MORE)
                    self->current_conn = list_item_next(self->current_conn);
                break;
            }
            self->current_conn = list_item_next(self->current_conn);
        }

        if(msg != NULL)
            break;

        if(self->current_conn == NULL) {
            PT_YIELD(&pt);
            self->current_conn = list_head(self->channel.connections);
            msg = NULL;
            continue;
        }
    }

    PT_END(&pt);
}

PT_THREAD(zmq_router_recv_multipart(zmq_socket_t *self, list_t msg_list)) {
    LOCAL_PT(pt);
    // printf("> zmq_router_recv_multipart %d\n", pt.lc);
    PT_BEGIN(&pt);

    zmq_msg_t *msg;
    do{
        PT_WAIT_THREAD(&pt, zmq_router_recv(self, &msg));
        list_add(msg_list, msg);
    } while((zmq_msg_flags(msg) & ZMQ_MSG_MORE) == ZMQ_MSG_MORE);


    PT_END(&pt);
}

PT_THREAD(zmq_router_send(zmq_socket_t *self, zmq_msg_t *msg_ptr)) {
    LOCAL_PT(pt);
    PT_BEGIN(&pt);

    PT_END(&pt);
}
