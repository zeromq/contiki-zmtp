#include "sub.h"

#include <stdlib.h>


static PT_THREAD(zmq_sub_send_all(zmq_socket_t *self, zmq_msg_t *msg));

void zmq_sub_init(zmq_socket_t *self) {
    self->in_conn = NULL;
    self->out_conn = NULL;

    self->recv = zmq_socket_recv_fair_queue;
    self->recv_multipart = zmq_socket_recv_multipart_fair_queue;
    self->send = NULL;
}

PT_THREAD(zmq_sub_subscribe(zmq_socket_t *self, const char *topic)) {
    LOCAL_PT(pt);
    PT_BEGIN(&pt);

    size_t size = strlen(topic);
    uint8_t *data = malloc(size+1);
    if(data != NULL) {
      data[0] = 1;
      memcpy(data+1, topic, size);
      static zmq_msg_t *msg = NULL;
      msg = zmq_msg_from_data(0, &data, size+1);

      PT_WAIT_THREAD(&pt, zmq_sub_send_all(self, msg));
      zmq_msg_destroy(&msg);
    }else{
        printf("ERROR: could not allocate memory for subscription message\r\n");
    }

    PT_END(&pt);
}

static PT_THREAD(zmq_sub_send_all(zmq_socket_t *self, zmq_msg_t *msg)) {
    // TODO: implement HWM
    LOCAL_PT(pt);
    PRINTF("> zmq_sub_send_all %d %p\n", pt.lc, msg);
    PT_BEGIN(&pt);

    self->out_conn = list_head(self->channel.connections);

    while(self->out_conn != NULL) {
        // TODO: check if queue is full (HWM)
        if((self->out_conn->validated & CONNECTION_VALIDATED) != CONNECTION_VALIDATED) {
            self->out_conn = list_item_next(self->out_conn);
            continue;
        }

        // NB. there is a bug right here.
        // When serving several peers, the msg gets added to the out queues of each of those connections.
        // The problem is: these queues are implemented with Contiki linked list,
        // Which requires to have a 'next' pointer as the first member of the struct.
        // When adding the same msg to several connections' queue, the next pointer gets overwritten every time...
        zmtp_connection_add_out_msg(self->out_conn, msg);

        zmtp_process_post(zmq_socket_output_activity, self->out_conn);
        PT_WAIT_UNTIL(&pt, self->out_conn->out_size <= 0);

        self->out_conn = list_item_next(self->out_conn);
    }

    PT_END(&pt);
}
