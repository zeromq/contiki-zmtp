/*
 * Copyright (c) 2016 Contributors as noted in the AUTHORS file
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef ZMQ_H_
#define ZMQ_H_

#define DEBUG DEBUG_NONE
// #define DEBUG DEBUG_PRINT
#include "net/ip/uip-debug.h"

#include "sys/pt.h"
#include "sys/process.h"
#include "lib/list.h"
#include "net/ip/uip.h"
#include "contiki-net.h"

/*
Limitations:
- Router/Dealer does not support yet custom identity
- Due to Contiki's protothread, it blocks processing of incoming data and other outgoing data when sending to a peer
- Due to Contiki's protothread, router blocks on sending (at least for a part of the sending process)
- Whenever a remote peer we connected to disconnect, its in/out queues are destroyed (how to trace who is who with Contiki's net stack?)
- No runtime configurable HWM (how should it count? Per connection or per socket?), but implementation is partially ready for it ATM
- Fair queuing is implemented just as a loop cycling through all connections
- If received data is bigger than ZMTP_INPUT_BUFFER_SIZE (default: 200), it will not be able to read it over several buffer reads,
  consider it as an error and thus will close the connection
- Same for output data bigger than ZMTP_OUTPUT_BUFFER_SIZE (default: 200), it will have an undefined behaviour
- There is nothing to close/stop a socket
- Can only bind, but connecting should not be hard to implement (just need to parse address strings)
- Only one PUB socket can be created
- SUB sockets can only subscribe (but PUB do support unsubcriptions), but subscriptions will only be sent once to currenlty connected peer
*/

#ifndef ZMQ_MAX_MSGS
  #define ZMQ_MAX_MSGS 20
#endif

#ifndef ZMTP_MAX_SUB_TOPICS
  #define ZMTP_MAX_SUB_TOPICS 10
#endif

#ifndef ZMTP_MAX_CONNECTIONS
  #define ZMTP_MAX_CONNECTIONS 10
#endif

#ifndef ZMTP_INPUT_BUFFER_SIZE
  #define ZMTP_INPUT_BUFFER_SIZE 200
#endif

#ifndef ZMTP_OUTPUT_BUFFER_SIZE
  #define ZMTP_OUTPUT_BUFFER_SIZE 200
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

struct _zmq_sub_topic_t {
    struct _zmq_sub_topic_t *next;
    uint8_t *topic;
    uint8_t size;
};
typedef struct _zmq_sub_topic_t zmq_sub_topic_t;

struct zmtp_connection {
    struct zmtp_connection *next; // For list usage
    uint8_t inputbuf[ZMTP_INPUT_BUFFER_SIZE];
    uint8_t outputbuf[ZMTP_OUTPUT_BUFFER_SIZE];
    struct tcp_socket socket;
    uip_ipaddr_t addr;
    uip_ipaddr_t *addr_ptr;
    uint16_t port;

    uint8_t sent_done; // Used to block zmtp_tcp_send_inner
    uint8_t validated; // Tell if the full initial handshake passed
    struct _zmtp_channel_t *channel; // The channel to which we are attached to

    // Connections's double queues
    LIST_STRUCT(in_queue);
    // int in_hwm; // Not used at the moment
    int in_size;
    LIST_STRUCT(out_queue);
    int out_hwm; // Not used at the moment
    int out_size;

    // For a PUB socket
    LIST_STRUCT(sub_topics);

    // For a ROUTER socker
    // uint8_t *identity; // Not used at the moment
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
    struct process *notify_process_input;
    struct process *notify_process_output;
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
    zmtp_connection_t *in_conn; // Current connection being used for input
    zmtp_connection_t *out_conn; // Current connection being used for output
    PT_THREAD((*recv) (struct zmq_socket *self, zmq_msg_t **msg_ptr));
    PT_THREAD((*recv_multipart) (struct zmq_socket *self, list_t msg_list));
    PT_THREAD((*send) (struct zmq_socket *self, zmq_msg_t *msg_ptr));
};
typedef struct zmq_socket zmq_socket_t;

process_event_t zmq_socket_input_activity;
process_event_t zmq_socket_output_activity;

void zmq_init();
int zmq_connect(zmq_socket_t *self, const char *host, unsigned short port);
int zmq_bind (zmq_socket_t *self, unsigned short port);

void zmq_socket_init(zmq_socket_t *self, zmq_socket_type_t socket_type);

PT_THREAD(zmq_socket_recv_fair_queue(zmq_socket_t *self, zmq_msg_t **msg_ptr));
PT_THREAD(zmq_socket_recv_multipart_fair_queue(zmq_socket_t *self, list_t msg_list));
PT_THREAD(zmq_socket_send_round_robin_block(zmq_socket_t *self, zmq_msg_t *msg));

zmq_msg_t *zmq_msg_new(uint8_t flags, size_t size);
zmq_msg_t *zmq_msg_from_data(uint8_t flags, uint8_t **data_p, size_t size);
zmq_msg_t *zmq_msg_from_const_data(uint8_t flags, void *data, size_t size);
void zmq_msg_destroy(zmq_msg_t **self_p);
uint8_t zmq_msg_flags(zmq_msg_t *self);
uint8_t *zmq_msg_data(zmq_msg_t *self);
size_t zmq_msg_size(zmq_msg_t *self);
zmq_msg_t *_zmq_msg_from_wire(const uint8_t *inputptr, int inputdatalen, int *read);

#define LOCAL_PT(pt_) static struct pt pt_; static uint8_t pt_inited=0; if(pt_inited == 0) { PT_INIT(&pt_); pt_inited = 1; }

#endif
