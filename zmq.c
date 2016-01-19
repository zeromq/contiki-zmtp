#include "zmq.h"
#include "router.h"
#include "dealer.h"
#include "pub.h"
#include "sub.h"
#include "pull.h"
#include "push.h"

#include <stdio.h>
#include <stdlib.h>

#include "net/ip/uip-debug.h"

#define LOCAL_PT(pt_) static struct pt pt_; static uint8_t pt_inited=0; if(pt_inited == 0) { PT_INIT(&pt_); pt_inited = 1; }

void zmq_init() {
    static uint8_t init_done = 0;

    if(init_done == 0) {
        zmq_socket_input_activity = process_alloc_event();
        zmq_socket_output_activity = process_alloc_event();

        zmtp_init();
    }
}

int zmq_connect (zmq_socket_t *self, uip_ipaddr_t *addr, unsigned short port) {
    // TODO: implement
    return -1;
}

int zmq_bind (zmq_socket_t *self, unsigned short port) {
    return zmtp_listen(&self->channel, port);
}

/*----------------------------------------------------------------------------*/

void zmq_socket_init(zmq_socket_t *self, zmq_socket_type_t socket_type) {
    zmtp_channel_init(&self->channel, socket_type, PROCESS_CURRENT(), PROCESS_CURRENT());

    switch(socket_type) {
      case ZMQ_ROUTER:
        zmq_router_init(self);
        break;
      case ZMQ_DEALER:
        zmq_dealer_init(self);
        break;
      case ZMQ_PUB:
        zmq_pub_init(self);
        break;
      case ZMQ_SUB:
        zmq_sub_init(self);
        break;
      case ZMQ_PUSH:
        zmq_push_init(self);
        break;
      case ZMQ_PULL:
        zmq_pull_init(self);
        break;
      default:
        printf("ERROR: Socket type not supported yet\r\n");
    }
}

PT_THREAD(zmq_socket_recv_fair_queue(zmq_socket_t *self, zmq_msg_t **msg_ptr)) {
    // TODO: redo this function
    LOCAL_PT(pt);
    // PRINTF("> zmq_socket_recv_fair_queue %d\r\n", pt.lc);
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
            if((self->in_conn->validated & CONNECTION_VALIDATED) != CONNECTION_VALIDATED) {
                self->in_conn = list_item_next(self->in_conn);
                continue;
            }

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

PT_THREAD(zmq_socket_recv_multipart_fair_queue(zmq_socket_t *self, list_t msg_list)) {
    LOCAL_PT(pt);
    // PRINTF("> zmq_socket_recv_multipart_fair_queue %d\r\n", pt.lc);
    PT_BEGIN(&pt);

    zmq_msg_t *msg;
    do{
        PT_WAIT_THREAD(&pt, zmq_socket_recv_fair_queue(self, &msg));
        list_add(msg_list, msg);
    } while((zmq_msg_flags(msg) & ZMQ_MSG_MORE) == ZMQ_MSG_MORE);

    PT_END(&pt);
}

// Take ownership of the message and will destroy it
PT_THREAD(zmq_socket_send_round_robin_block(zmq_socket_t *self, zmq_msg_t *msg)) {
    // TODO: implement HWM
    LOCAL_PT(pt);
    PRINTF("> zmq_socket_send_round_robin_block %d %p\r\n", pt.lc, msg);
    PT_BEGIN(&pt);

    if(self->out_conn == NULL)
        self->out_conn = list_head(self->channel.connections);

    while(1) {
        // TODO: check if queue is full (HWM)
        while((self->out_conn != NULL) && ((self->out_conn->validated & CONNECTION_VALIDATED) != CONNECTION_VALIDATED))
            self->out_conn = list_item_next(self->out_conn);

        if(self->out_conn == NULL) {
            PT_YIELD(&pt);
            self->out_conn = list_head(self->channel.connections);
            continue;
        }else
            break;
    }

    zmtp_connection_add_out_msg(self->out_conn, msg);
    zmtp_process_post(zmq_socket_output_activity, self->out_conn);
    PT_WAIT_UNTIL(&pt, self->out_conn->out_size <= 0);

    PT_END(&pt);
}


/*----------------------------------------------------------------------------*/

MEMB(zmq_messages, zmq_msg_t, ZMQ_MAX_MSGS);

//  Constructor; it allocates buffer for message data.
//  The initial content of the allocated buffer is undefined.

zmq_msg_t *zmq_msg_new(uint8_t flags, size_t size) {
    zmq_msg_t *self = memb_alloc(&zmq_messages);
    if(self == NULL) {
      printf("ERROR: Reached exhaustion of messages\r\n");
      return NULL;
    }

    self->flags = flags;
    self->data = (uint8_t *) malloc (size);
    self->size = size;
    self->greedy = 1;
    return self;
}

//  Constructor; takes ownership of data and frees it when destroying the
//  message. Nullifies the data reference.

zmq_msg_t *zmq_msg_from_data(uint8_t flags, uint8_t **data_p, size_t size) {
    zmq_msg_t *self = memb_alloc(&zmq_messages);
    if(self == NULL) {
      printf("ERROR: Reached exhaustion of messages\r\n");
      return NULL;
    }

    self->flags = flags;
    self->data = *data_p;
    self->size = size;
    self->greedy = 1;
    *data_p = NULL;
    return self;
}

//  Constructor that takes a constant data and does not copy, modify, or
//  free it.

zmq_msg_t *zmq_msg_from_const_data(uint8_t flags, void *data, size_t size) {
    zmq_msg_t *self = memb_alloc(&zmq_messages);
    if(self == NULL) {
      printf("ERROR: Reached exhaustion of messages\r\n");
      return NULL;
    }

    self->flags = flags;
    self->data = data;
    self->size = size;
    self->greedy = 0;
    return self;
}

void zmq_msg_destroy(zmq_msg_t **self_p) {
    if (*self_p) {
        zmq_msg_t *self = *self_p;
        if (self->greedy)
            free(self->data);
        memb_free(&zmq_messages, self);
        *self_p = NULL;
    }
}

uint8_t zmq_msg_flags(zmq_msg_t *self) {
    return self->flags;
}

uint8_t *zmq_msg_data(zmq_msg_t *self) {
    return self->data;
}

size_t zmq_msg_size(zmq_msg_t *self) {
    return self->size;
}

zmq_msg_t *_zmq_msg_from_wire(const uint8_t *inputptr, int inputdatalen, int *read) {
    uint8_t frame_flags;
    size_t size;
    const uint8_t *pos;

    pos = inputptr;

    if((pos + 1) > (inputptr + inputdatalen)) {
        printf("WARNING: Not enough data to read message flags\r\n");
        return NULL;
    }

    frame_flags = *pos++;

    //  Check large flag
    if (!(frame_flags & ZMQ_MSG_LARGE)) {
        if((pos + 1) > (inputptr + inputdatalen)) {
            printf("WARNING: Not enough data to read message size\r\n");
            return NULL;
        }

        size = (size_t) *pos++;

    }else{
        if((pos + 8) > (inputptr + inputdatalen)) {
            printf("WARNING: Not enough data to read message size\r\n");
            return NULL;
        }

        size = (uint64_t) pos[0] << 56 |
               (uint64_t) pos[1] << 48 |
               (uint64_t) pos[2] << 40 |
               (uint64_t) pos[3] << 32 |
               (uint64_t) pos[4] << 24 |
               (uint64_t) pos[5] << 16 |
               (uint64_t) pos[6] << 8  |
               (uint64_t) pos[7];
        pos += 8;
    }

    if((pos + size) > (inputptr + inputdatalen)) {
        printf("WARNING: Not enough data to read message\r\n");
        return NULL;
    }

    uint8_t *data = malloc(size);
    if(data == NULL) {
        printf("WARNING: Could not allocate data block for message\r\n");
        return NULL;
    }

    memcpy(data, pos, size);

    pos += size;
    *read = pos - inputptr;

    uint8_t msg_flags = 0;
    if ((frame_flags & ZMQ_MSG_MORE) == ZMQ_MSG_MORE)
        msg_flags |= ZMQ_MSG_MORE;
    if ((frame_flags & ZMQ_MSG_COMMAND) == ZMQ_MSG_COMMAND)
        msg_flags |= ZMQ_MSG_COMMAND;

    return zmq_msg_from_data(msg_flags, &data, size);
}
