#include "zmq.h"
#include "router.h"

#include <stdio.h>
#include <stdlib.h>

void zmq_init() {
    static uint8_t init_done = 0;

    if(init_done == 0) {
        zmq_socket_input_activity = process_alloc_event();
        zmq_socket_output_activity = process_alloc_event();

        zmtp_init();
    }
}

void zmq_socket_init(zmq_socket_t *self, zmq_socket_type_t socket_type) {
    zmtp_channel_init(&self->channel, socket_type, PROCESS_CURRENT());

    switch(socket_type) {
      case ZMQ_ROUTER:
        zmq_router_init(self);
        break;
      default:
        printf("ERROR: Socket type not supported yet");
    }
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
