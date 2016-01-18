#ifndef ZMQ_H_
#define ZMQ_H_

#include "sys/pt.h"
#include "sys/process.h"
#include "lib/list.h"
#include "net/ip/uip.h"
#include "contiki-net.h"

/*
Limitations:
- Router/Dealer does not support custom identity
- Blocks processing of incoming data and other outgoing data when sending to a peer
- Whenever a remote peer we connected to disconnect, its in/out queues are destroyed
- No runtime configurable HWM (how should it count? Per connection or per socket?)
- Fair queuing is implemented just as a loop cycling through all connections
*/

#ifndef ZMQ_MAX_MSGS
  #define ZMQ_MAX_MSGS 4
#endif

#ifndef ZMTP_MAX_CONNECTIONS
  #define ZMTP_MAX_CONNECTIONS 10
#endif

#ifndef ZMTP_INPUT_BUFFER_SIZE
  #define ZMTP_INPUT_BUFFER_SIZE 400
#endif

#ifndef ZMTP_OUTPUT_BUFFER_SIZE
  #define ZMTP_OUTPUT_BUFFER_SIZE 400
#endif

enum {
    ZMQ_MSG_MORE = 1,
    ZMQ_MSG_LARGE = 2,
    ZMQ_MSG_COMMAND = 4,
};

struct _zmq_msg_t {
    struct _zmq_msg_t *next; // For whenever we list messages
    uint8_t flags;                 //  Flags byte for message
    uint8_t *data;                 //  Data part of message
    size_t size;                //  Size of data in bytes
    uint8_t greedy;                //  Did we take ownership of data?
};
typedef struct _zmq_msg_t zmq_msg_t;

struct zmtp_connection {
  struct zmtp_connection *next;
  uint8_t inputbuf[ZMTP_INPUT_BUFFER_SIZE];
  uint8_t outputbuf[ZMTP_OUTPUT_BUFFER_SIZE];
  struct tcp_socket socket;
  uip_ipaddr_t *addr;
  uint16_t port;
  uint8_t sent_done;
  uint8_t validated;
  struct _zmtp_channel_t *channel;

  // Connections's
  LIST_STRUCT(in_queue);
  // int in_hwm; // Not used at the moment
  int in_size;
  LIST_STRUCT(out_queue);
  int out_hwm; // Not used at the moment
  int out_size;
};
typedef struct zmtp_connection zmtp_connection_t;

typedef enum {
    ZMQ_ROUTER,
    ZMQ_DEALER,
    ZMQ_PUB,
    ZMQ_SUB,
    ZMQ_XPUB,
    ZMQ_XSUB,
    ZMQ_REQ,
    ZMQ_REP,
    ZMQ_PUSH,
    ZMQ_PULL
} zmq_socket_type_t;

struct _zmtp_channel_t {
    LIST_STRUCT(connections);
    zmq_socket_type_t socket_type;

    // How to signal back to our client protothread
    struct process *notify_process;
};
typedef struct _zmtp_channel_t zmtp_channel_t;

typedef enum {
    ZMTP_PEER_CONNECT,
    ZMTP_PEER_DISCONNECT,
    ZMTP_RECEIVED_DATA,
    ZMTP_SETN_DATA
} zmtp_channel_signal_t;

struct zmq_socket {
    zmtp_channel_t channel;
    zmtp_connection_t *current_conn;
    // void (*signal) (struct zmq_socket *self, zmtp_channel_signal_t signal);
    int (*connect) (struct zmq_socket *self, uip_ipaddr_t *addr, unsigned short port);
    int (*bind) (struct zmq_socket *self, unsigned short port);
    PT_THREAD((*recv) (struct zmq_socket *self, zmq_msg_t **msg_ptr));
    PT_THREAD((*recv_multipart) (struct zmq_socket *self, list_t msg_list));
    PT_THREAD((*send) (struct zmq_socket *self, zmq_msg_t *msg_ptr));
};
typedef struct zmq_socket zmq_socket_t;

process_event_t zmq_socket_input_activity;
process_event_t zmq_socket_output_activity;

void zmq_init();
void zmq_socket_init(zmq_socket_t *self, zmq_socket_type_t socket_type);

zmq_msg_t *zmq_msg_new(uint8_t flags, size_t size);
zmq_msg_t *zmq_msg_from_data(uint8_t flags, uint8_t **data_p, size_t size);
zmq_msg_t *zmq_msg_from_const_data(uint8_t flags, void *data, size_t size);
void zmq_msg_destroy(zmq_msg_t **self_p);
uint8_t zmq_msg_flags(zmq_msg_t *self);
uint8_t *zmq_msg_data(zmq_msg_t *self);
size_t zmq_msg_size(zmq_msg_t *self);
zmq_msg_t *_zmq_msg_from_wire(const uint8_t *inputptr, int inputdatalen, int *read);

#endif
