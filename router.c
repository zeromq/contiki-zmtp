#include "router.h"

#include <stdio.h>

#include "net/ip/uip-debug.h"


void zmq_router_init(zmq_socket_t *self) {
    self->in_conn = NULL;
    self->out_conn = NULL;

    // self->signal = zmq_router_signal;
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

PT_THREAD(zmq_router_recv(zmq_socket_t *self, zmq_msg_t **msg_ptr)) {
    // TODO: redo this function
    LOCAL_PT(pt);
    // PRINTF("> zmq_router_recv %d\n", pt.lc);
    PT_BEGIN(&pt);

    if(self->in_conn == NULL)
        self->in_conn = list_head(self->channel.connections);
    else{
        // Detect if the current_conn disconnected meanwhile
        zmtp_connection_t *scan = list_head(self->channel.connections);
        while(scan != self->in_conn)
            scan = list_item_next(scan);
        if(scan != self->in_conn)
            self->in_conn = list_head(self->channel.connections);
    }

    zmq_msg_t *msg = NULL;
    while(1) {
        while(self->in_conn != NULL) {
            msg = zmtp_connection_pop_in_msg(self->in_conn);
            if(msg != NULL) {
                *msg_ptr = msg;
                if((zmq_msg_flags(msg) & ZMQ_MSG_MORE) != ZMQ_MSG_MORE)
                    self->in_conn = list_item_next(self->in_conn);
                break;
            }
            self->in_conn = list_item_next(self->in_conn);
        }

        if(msg != NULL)
            break;

        if(self->in_conn == NULL) {
            PT_YIELD(&pt);
            self->in_conn = list_head(self->channel.connections);
            msg = NULL;
            continue;
        }
    }

    PT_END(&pt);
}

PT_THREAD(zmq_router_recv_multipart(zmq_socket_t *self, list_t msg_list)) {
    LOCAL_PT(pt);
    // PRINTF("> zmq_router_recv_multipart %d\n", pt.lc);
    PT_BEGIN(&pt);

    zmq_msg_t *msg;
    do{
        PT_WAIT_THREAD(&pt, zmq_router_recv(self, &msg));
        list_add(msg_list, msg);
    } while((zmq_msg_flags(msg) & ZMQ_MSG_MORE) == ZMQ_MSG_MORE);

    PT_END(&pt);
}

PT_THREAD(zmq_router_send(zmq_socket_t *self, zmq_msg_t *msg_ptr)) {
    static zmtp_connection_t *conn;
    // TODO: implement HWM
    LOCAL_PT(pt);
    PT_BEGIN(&pt);

    // TODO: deal with identity instead
    conn = list_head(self->channel.connections);
    zmtp_connection_add_out_msg(conn, msg_ptr);
    zmtp_process_post(zmq_socket_output_activity, conn);
    PT_WAIT_UNTIL(&pt, conn->out_size <= 0);

    PT_END(&pt);
}
