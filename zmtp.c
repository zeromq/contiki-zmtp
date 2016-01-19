/*
 * Copyright (c) 2012, Thingsquare, http://www.thingsquare.com/.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "zmtp.h"

#include "net/ip/uip-debug.h"
#include "sys/cc.h"
#include "lib/ringbuf.h"

#include <stdio.h>
#include <stdlib.h>

#define UIP_IP_BUF   ((struct uip_tcpip_hdr *)&uip_buf[UIP_LLH_LEN])

#ifndef ZMTP_MAX_EVENTS
  #define ZMTP_MAX_EVENTS 50
#endif

PROCESS(zmtp_process, "ZMTP process");

static int zmtp_tcp_input(struct tcp_socket *s, void *conn_ptr, const uint8_t *inputptr, int inputdatalen);
static void zmtp_tcp_event(struct tcp_socket *s, void *conn_ptr, tcp_socket_event_t ev);
PT_THREAD(zmtp_tcp_send(zmtp_connection_t *conn, const uint8_t *data, size_t len));
PT_THREAD(zmtp_tcp_send_inner (zmtp_connection_t *conn, const uint8_t *data, size_t len, size_t *sent));

int zmtp_channel_tcp_connect (zmtp_channel_t *self);
int zmtp_channel_tcp_listen (zmtp_channel_t *self);
PT_THREAD(zmtp_channel_remote_connected(zmtp_channel_t *self));

static void print_data(const uint8_t *data, int data_size);

#define PROCESS_WAIT_THREAD(thread, valid_ev) \
    do { \
      LC_SET((process_pt)->lc); \
      if((ev != valid_ev) && (ev != zmtp_call_me_again)) { \
        process_post(&zmtp_process, ev, data); \
        return PT_WAITING; \
      } \
      if(PT_SCHEDULE(thread)) { \
        return PT_WAITING; \
      } \
    } while(0);

// struct pt hdl_conn_pt;
/********/
//
// #ifndef ZMTP_MAX_ENDPOINTS
//   #define ZMTP_MAX_ENDPOINTS 2
// #endif
//
// struct zmtp_tcp_endpoint {
//     uip_ipaddr_t addr;
//     uint16_t port;
// };
// typedef struct zmtp_tcp_endpoint zmtp_tcp_endpoint_t;
// MEMB(zmtp_tcp_endpoints, zmtp_tcp_endpoint_t, ZMTP_MAX_ENDPOINTS);
//
// zmtp_tcp_endpoint_t *
// zmtp_tcp_endpoint_new (uip_ipaddr_t *ip_addr, uint16_t port)
// {
//     zmtp_tcp_endpoint_t *self = memb_alloc(&zmtp_tcp_endpoints);
//     if (self == NULL) {
//         printf("Reached exhaustion of zmtp_tcp_endpoints");
//         return NULL;
//     }
//
//     if(ip_addr != NULL)
//         uip_ipaddr_copy(&self->addr, ip_addr);
//
//     self->port = uip_htons(port);
//
//     return self;
// }
//
// void
// zmtp_tcp_endpoint_destroy (zmtp_tcp_endpoint_t **self_p)
// {
//     if (*self_p) {
//         zmtp_tcp_endpoint_t *self = *self_p;
//         memb_free(&zmtp_tcp_endpoints, self);
//         *self_p = NULL;
//     }
// }
//
// zmtp_connection_t *
// zmtp_tcp_endpoint_connect (zmtp_tcp_endpoint_t *self)
// {
//     zmtp_connection_t *conn = memb_alloc(&zmtp_connections);
//     if (conn == NULL) {
//         printf("Reached exhaustion of zmtp_connections\r\n");
//         return NULL;
//     }
//
//     PSOCK_INIT(&conn->ps, conn->buffer, sizeof(conn->buffer));
//     conn->ip_conn = tcp_connect(&self->addr, self->port, conn);
//
//     return conn;
// }
//
// zmtp_connection_t *
// zmtp_tcp_endpoint_listen (zmtp_tcp_endpoint_t *self)
// {
//     zmtp_connection_t *conn = memb_alloc(&zmtp_connections);
//     if (conn == NULL) {
//         printf("Reached exhaustion of zmtp_connections\r\n");
//         return NULL;
//     }
//
//     PSOCK_INIT(&conn->ps, conn->buffer, sizeof(conn->buffer));
//
//     tcp_listen(self->port);
//
//     return conn;
// }

/*-----------------------------------------------------------*/

/*-----------------------------------------------------------*/

#ifndef ZMTP_MAX_CHANNELS
  #define ZMTP_MAX_CHANNELS 10
#endif

MEMB(zmtp_channels, zmtp_channel_t, ZMTP_MAX_CHANNELS);
MEMB(zmtp_connections, zmtp_connection_t, ZMTP_MAX_CONNECTIONS);

zmtp_connection_t *zmtp_connection_new() {
    zmtp_connection_t *self = memb_alloc(&zmtp_connections);
    if(self == NULL) {
      printf("ERROR: Reached exhaustion of connections\r\n");
      return NULL;
    }

    zmtp_connection_init(self);

    return self;
}

void zmtp_connection_destroy (zmtp_connection_t **self_p) {
    if (*self_p) {
        zmtp_connection_t *self = *self_p;

        // Shall it cleanes the in/out queues?
        // Shall it destroys the messages in those queues? (nb. we are not owner of these objects)

        memb_free(&zmtp_connections, self);
        *self_p = NULL;
    }
}

void zmtp_connection_init(zmtp_connection_t *self) {
    bzero(self, sizeof(zmtp_connection_t));
    LIST_STRUCT_INIT(self, in_queue);
    LIST_STRUCT_INIT(self, out_queue);
    LIST_STRUCT_INIT(self, sub_topics);

    tcp_socket_register(
        &self->socket,
        self,
        self->inputbuf, sizeof(self->inputbuf),
        self->outputbuf, sizeof(self->outputbuf),
        zmtp_tcp_input, zmtp_tcp_event
    );
}

int zmtp_connection_tcp_connect (zmtp_connection_t *self) {
    return tcp_socket_connect(&self->socket, self->addr, self->port);
}

int zmtp_connection_tcp_listen (zmtp_connection_t *self) {
    PRINTF("Listening on port %d\r\n", self->port);
    return tcp_socket_listen(&self->socket, self->port);
}

int zmtp_connection_tcp_unlisten (zmtp_connection_t *self) {
    return tcp_socket_unlisten(&self->socket);
}

int zmtp_connection_add_in_msg(zmtp_connection_t *self, zmq_msg_t *msg) {
    // TODO implement HWM
    list_add(self->in_queue, msg);
    self->in_size++;

    return 0;
}

zmq_msg_t *zmtp_connection_pop_in_msg(zmtp_connection_t *self) {
    zmq_msg_t *msg = list_pop(self->in_queue);
    if(msg != NULL)
        self->in_size--;

    return msg;
}

int zmtp_connection_add_out_msg(zmtp_connection_t *self, zmq_msg_t *msg) {
    // TODO implement HWM
    list_add(self->out_queue, msg);
    self->out_size++;

    return 0;
}

zmq_msg_t *zmtp_connection_pop_out_msg(zmtp_connection_t *self) {
    zmq_msg_t *msg =  list_pop(self->out_queue);
    if(msg != NULL)
        self->out_size--;

    return msg;
}

/*-------------------------------------------------------------*/

zmtp_channel_t *zmtp_channel_new (zmq_socket_type_t socket_type, struct process *in_p, struct process *out_p)
{
    zmtp_channel_t *self = memb_alloc(&zmtp_channels);
    if(self == NULL) {
      printf("ERROR: Reached exhaustion of channels\r\n");
      return NULL;
    }

    zmtp_channel_init(self, socket_type, in_p, out_p);

    return self;
}

void zmtp_channel_destroy (zmtp_channel_t **self_p)
{
    if (*self_p) {
        zmtp_channel_t *self = *self_p;
        memb_free(&zmtp_channels, self);
        *self_p = NULL;
    }
}

void zmtp_channel_init(zmtp_channel_t *self, zmq_socket_type_t socket_type, struct process *in_p, struct process *out_p) {
    LIST_STRUCT_INIT(self, connections);
    self->socket_type = socket_type;

    self->notify_process_input = in_p;
    self->notify_process_output = out_p;
}

zmtp_connection_t *zmtp_channel_add_conn(zmtp_channel_t *self) {
    zmtp_connection_t *conn = zmtp_connection_new();
    if(conn == NULL)
        return NULL;

    conn->channel = self;
    list_add(self->connections, conn);

    return conn;
}

void zmtp_channel_close_conn(zmtp_channel_t *self, zmtp_connection_t *conn) {
    list_remove(self->connections, conn);
    zmtp_connection_tcp_unlisten(conn);
    zmtp_connection_destroy(&conn);
}

PT_THREAD(zmtp_remote_connected(zmtp_connection_t *conn)) {
    LOCAL_PT(pt);
    PRINTF("> zmtp_remote_connected %d\n", pt.lc);
    PT_BEGIN(&pt);

    static const uint8_t signature[10] = { 0xff, 0, 0, 0, 0, 0, 0, 0, 1, 0x7f };

    PT_WAIT_THREAD(&pt, zmtp_tcp_send(conn, signature, sizeof(signature)));

    PT_END(&pt);
}

PT_THREAD(zmtp_send_version_major(zmtp_connection_t *conn)) {
    LOCAL_PT(pt);
    PRINTF("> zmtp_send_version_major %d\n", pt.lc);
    PT_BEGIN(&pt);

    static const uint8_t version = 3;

    PT_WAIT_THREAD(&pt, zmtp_tcp_send(conn, &version, 1));

    PT_END(&pt);
}

PT_THREAD(zmtp_send_greeting(zmtp_connection_t *conn)) {
    LOCAL_PT(pt);
    PRINTF("> zmtp_send_greeting %d\n", pt.lc);
    PT_BEGIN(&pt);

    static const uint8_t greeting[53] = { 1, 'N', 'U', 'L', 'L', '\0', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    PT_WAIT_THREAD(&pt, zmtp_tcp_send(conn, greeting, sizeof(greeting)));

    PT_END(&pt);
}

PT_THREAD(zmtp_send_ready(zmtp_connection_t *conn)) {
    // TODO: support custom DEALER identity

    static int total_size;
    static uint8_t ready[43] = {
        0x04, // Command flag,
        0, // Command size, to be updated
        5, 'R', 'E', 'A', 'D', 'Y',
        11, 'S', 'o', 'c', 'k', 'e', 't', '-', 'T', 'y', 'p', 'e',
        0, 0, 0, 0, // Socket type length, to be updated
        0, 0, 0, 0, 0, 0, // Longest socket name
        8, 'I', 'd', 'e', 'n', 't', 'i', 't', 'y', 0, 0, 0, 0 // If we need identity...
    };

    switch(conn->channel->socket_type) {
      case ZMQ_DEALER:
        total_size = 43;
        ready[1] = 41;
        ready[23] = 6;
        ready[24] = 'D'; ready[25] = 'E'; ready[26] = 'A';
        ready[27] = 'L'; ready[28] = 'E'; ready[29] = 'R';
        // TODO: Fixed empty identity ATM. Support for custom identity to be done.
        break;
      case ZMQ_ROUTER:
        total_size = 30;
        ready[1] = 28;
        ready[23] = 6;
        ready[24] = 'R'; ready[25] = 'O'; ready[26] = 'U';
        ready[27] = 'T'; ready[28] = 'E'; ready[29] = 'R';
        break;
      case ZMQ_PUB:
        total_size = 27;
        ready[1] = 25;
        ready[23] = 3;
        ready[24] = 'P'; ready[25] = 'U'; ready[26] = 'B';
        break;
      case ZMQ_SUB:
        total_size = 27;
        ready[1] = 25;
        ready[23] = 3;
        ready[24] = 'S'; ready[25] = 'U'; ready[26] = 'B';
        break;
      case ZMQ_XPUB:
        total_size = 28;
        ready[1] = 26;
        ready[23] = 4;
        ready[24] = 'X'; ready[25] = 'P'; ready[26] = 'U'; ready[27] = 'B';
        break;
      case ZMQ_XSUB:
        total_size = 28;
        ready[1] = 26;
        ready[23] = 4;
        ready[24] = 'X'; ready[25] = 'S'; ready[26] = 'U'; ready[27] = 'B';
        break;
      case ZMQ_REQ:
        total_size = 27;
        ready[1] = 25;
        ready[23] = 3;
        ready[24] = 'R'; ready[25] = 'E'; ready[26] = 'Q';
        break;
      case ZMQ_REP:
        total_size = 27;
        ready[1] = 25;
        ready[23] = 3;
        ready[24] = 'R'; ready[25] = 'E'; ready[26] = 'P';
        break;
      case ZMQ_PUSH:
        total_size = 28;
        ready[1] = 26;
        ready[23] = 4;
        ready[24] = 'P'; ready[25] = 'U'; ready[26] = 'S'; ready[27] = 'H';
        break;
      case ZMQ_PULL:
        total_size = 28;
        ready[1] = 26;
        ready[23] = 4;
        ready[24] = 'P'; ready[25] = 'U'; ready[26] = 'L'; ready[27] = 'L';
        break;
    }

    LOCAL_PT(pt);
    PRINTF("> zmtp_send_ready %d\n", pt.lc);
    PT_BEGIN(&pt);

    PT_WAIT_THREAD(&pt, zmtp_tcp_send(conn, ready, total_size));

    PT_END(&pt);
}

PT_THREAD(zmtp_send_msg(zmtp_connection_t *conn, zmq_msg_t *msg)) {
    static uint8_t *buffer;
    static int total_size;
    LOCAL_PT(pt);
    PRINTF("> zmtp_send_msg %d\n", pt.lc);
    PT_BEGIN(&pt);

    int size_size = ((zmq_msg_size(msg) > 255) ? 8 : 1);
    total_size = zmq_msg_size(msg) + 1 + size_size;
    buffer = malloc(total_size);

    if(buffer != NULL) {
        buffer[0] = zmq_msg_flags(msg);
        if(size_size == 1) {
            buffer[1] = zmq_msg_size(msg);
        }else{
            buffer[0] |= ZMQ_MSG_LARGE;
            buffer[1] = (zmq_msg_size(msg) >> 56) & 0xFF;
            buffer[2] = (zmq_msg_size(msg) >> 48) & 0xFF;
            buffer[3] = (zmq_msg_size(msg) >> 40) & 0xFF;
            buffer[4] = (zmq_msg_size(msg) >> 32) & 0xFF;
            buffer[5] = (zmq_msg_size(msg) >> 24) & 0xFF;
            buffer[6] = (zmq_msg_size(msg) >> 16) & 0xFF;
            buffer[7] = (zmq_msg_size(msg) >> 8) & 0xFF;
            buffer[8] = zmq_msg_size(msg) & 0xFF;
        }

        memcpy(buffer+size_size+1, zmq_msg_data(msg), zmq_msg_size(msg));

        PT_WAIT_THREAD(&pt, zmtp_tcp_send(conn, buffer, total_size));

        free(buffer);
    }else
        printf("ERROR: Not enought memory\r\n");

    PT_END(&pt);
}

PT_THREAD(zmtp_tcp_send(zmtp_connection_t *conn, const uint8_t *data, size_t len)) {
    static size_t sent, sent_all;
    LOCAL_PT(pt);
    // PRINTF("> zmtp_tcp_send %d %p %d\n", pt.lc, data, len);
    PT_BEGIN(&pt);

    PRINTF("Sending (%d): ", (int) len);
    #if (DEBUG) & DEBUG_PRINT
    print_data(data, len);
    #endif
    PRINTF("\r\n");

    sent_all = 0;
    do {
      sent = 0;
      PT_WAIT_THREAD(&pt, zmtp_tcp_send_inner(conn, data + sent_all, len - sent_all, &sent));
      sent_all += sent;
    } while(sent_all < len);

    PT_END(&pt);
}

PT_THREAD(zmtp_tcp_send_inner (zmtp_connection_t *conn, const uint8_t *data, size_t len, size_t *sent)) {
    LOCAL_PT(pt);
    // PRINTF("> zmtp_tcp_send_inner %d\n", pt.lc);
    PT_BEGIN(&pt);

    *sent = MIN(sizeof(conn->outputbuf), len);

    conn->sent_done = 0;
    tcp_socket_send(&conn->socket, data, *sent);
    PT_WAIT_UNTIL(&pt, conn->sent_done == 1);

    PRINTF("Sent (%d): ", (int) *sent);
    #if (DEBUG) & DEBUG_PRINT
    print_data(data, *sent);
    #endif
    PRINTF("\r\n");

    PT_END(&pt);
}

// static
// PT_THREAD(s_tcp_recv (zmtp_connection_t *conn, void *buffer, size_t len))
// {
//     PSOCK_BEGIN(&conn->ps);
//     // uint8_t *original_buffer = conn->ps.bufptr;
//     // conn->ps.bufptr = buffer;
//     PSOCK_READBUF_LEN(&conn->ps, len);
//     memcpy(buffer, conn->buffer, len);
//     // conn->ps.bufptr = original_buffer;
//
//     printf("Read (%d): ", len);
//     print_data(buffer, len);
//     printf("\r\n");
//
//     PSOCK_END(&conn->ps);
// }
//
// uint8_t outgoing_sign[10] = { 0xff, 0, 0, 0, 0, 0, 0, 0, 1, 0x7f };
//
// PT_THREAD(handle_connection(zmtp_channel_t *self)) {
//     PT_BEGIN(&hdl_conn_pt);
//     // PSOCK_BEGIN(&self->conn->ps);
//
//     /*if (s_negotiate (self) == -1) {
//         PSOCK_CLOSE(&self->conn->ps);
//         memb_free(&zmtp_connections, self->conn);
//         self->conn = NULL;
//     }*/
//
//     printf("Trying to send\r\n");
//
//     PT_WAIT_THREAD(&hdl_conn_pt, s_tcp_recv (self->conn, outgoing_sign, 1));
//     printf("Got %02hhX\r\n", *outgoing_sign);
//
//     // PT_WAIT_THREAD(&hdl_conn_pt, s_tcp_recv (self->conn, outgoing_sign, 1));
//     // printf("Got %02hhX\r\n", *outgoing_sign);
//     //
//     // PT_WAIT_THREAD(&hdl_conn_pt, s_tcp_recv (self->conn, outgoing_sign, 1));
//     // printf("Got %02hhX\r\n", *outgoing_sign);
//
//     PT_WAIT_THREAD(&hdl_conn_pt, s_tcp_send (self->conn, outgoing_sign, sizeof outgoing_sign));
//     // s_tcp_send (self->conn, outgoing_sign, sizeof outgoing_sign);
//
//     // size_t len = 10;
//     // void *data = outgoing.signature;
//     //
//     // printf("Sending (%d): ", len);
//     // print_data(data, len);
//     // printf("\r\n");
//     //
//     // PSOCK_SEND(&self->conn->ps, (uint8_t *) data, len);
//     //
//     // printf("Sent (%d): ", len);
//     // print_data(data, len);
//     // printf("\r\n");
//
//     PT_WAIT_THREAD(&hdl_conn_pt, s_tcp_recv (self->conn, outgoing_sign, 1));
//     printf("Got %02hhX\r\n", *outgoing_sign);
//
//     printf("Done\r\n");
//
//     PT_END(&hdl_conn_pt);
//     // PSOCK_END(&self->conn->ps);
// }
//
// static int
// s_negotiate (zmtp_channel_t *self)
// {
//     if(self->conn == NULL)
//         goto io_error;
//
//     printf("Negociating\r\n");
//
//     zmtp_connection_t *s = self->conn;
//
//     //  This is our greeting (64 octets)
//     const struct zmtp_greeting outgoing = {
//         .signature = { 0xff, 0, 0, 0, 0, 0, 0, 0, 1, 0x7f },
//         .version   = { 3, 0 },
//         .mechanism = { 'N', 'U', 'L', 'L', '\0' }
//     };
//     //  Send protocol signature
//     if (s_tcp_send (s, outgoing.signature, sizeof outgoing.signature) == -1)
//         goto io_error;
//
//     printf("Sent signature\r\n");
//
//     //  Read the first byte.
//     struct zmtp_greeting incoming;
//     if (s_tcp_recv (s, incoming.signature, 1) == -1)
//         goto io_error;
//     if(incoming.signature [0] != 0xff)
//         goto io_error;
//
//     //  Read the rest of signature
//     if (s_tcp_recv (s, incoming.signature + 1, 9) == -1)
//         goto io_error;
//     if((incoming.signature [9] & 1) == 1)
//         goto io_error;
//
//     printf("Verified signature\r\n");
//
//     //  Exchange major version numbers
//     if (s_tcp_send (s, outgoing.version, 1) == -1)
//         goto io_error;
//     if (s_tcp_recv (s, incoming.version, 1) == -1)
//         goto io_error;
//
//     if(incoming.version [0] == 3)
//         goto io_error;
//
//     printf("Remote is version 3\r\n");
//
//     //  Send the rest of greeting to the peer.
//     if (s_tcp_send (s, outgoing.version + 1, 1) == -1)
//         goto io_error;
//     if (s_tcp_send (s, outgoing.mechanism, sizeof outgoing.mechanism) == -1)
//         goto io_error;
//     if (s_tcp_send (s, outgoing.as_server, sizeof outgoing.as_server) == -1)
//         goto io_error;
//     if (s_tcp_send (s, outgoing.filler, sizeof outgoing.filler) == -1)
//         goto io_error;
//
//     printf("Sent rest of the greeting\r\n");
//
//     //  Receive the rest of greeting from the peer.
//     if (s_tcp_recv (s, incoming.version + 1, 1) == -1)
//         goto io_error;
//     if (s_tcp_recv (s, incoming.mechanism, sizeof incoming.mechanism) == -1)
//         goto io_error;
//     if (s_tcp_recv (s, incoming.as_server, sizeof incoming.as_server) == -1)
//         goto io_error;
//     if (s_tcp_recv (s, incoming.filler, sizeof incoming.filler) == -1)
//         goto io_error;
//
//     printf("Received rest of the greeting\r\n");
//
//     //  Send READY command
//     zmtp_msg_t *ready = zmtp_msg_from_const_data (0x04, "\5READY", 6);
//     if(ready == NULL)
//         goto io_error;
//     zmtp_channel_send (self, ready);
//     zmtp_msg_destroy (&ready);
//
//     printf("Sent READY\r\n");
//
//     //  Receive READY command
//     ready = zmtp_channel_recv (self);
//     if (!ready)
//         goto io_error;
//     if((zmtp_msg_flags (ready) & ZMTP_MSG_COMMAND) == ZMTP_MSG_COMMAND)
//         goto io_error;
//     zmtp_msg_destroy (&ready);
//
//     printf("Received READY\r\n");
//
//     return 0;
//
// io_error:
//     return -1;
// }
//
// int
// zmtp_channel_send (zmtp_channel_t *self, zmtp_msg_t *msg)
// {
//     uint8_t frame_flags = 0;
//     if ((zmtp_msg_flags (msg) & ZMTP_MSG_MORE) == ZMTP_MSG_MORE)
//         frame_flags |= ZMTP_MORE_FLAG;
//     if ((zmtp_msg_flags (msg) & ZMTP_MSG_COMMAND) == ZMTP_MSG_COMMAND)
//         frame_flags |= ZMTP_COMMAND_FLAG;
//     if (zmtp_msg_size (msg) > 255)
//         frame_flags |= ZMTP_LARGE_FLAG;
//     if (s_tcp_send (self->conn, &frame_flags, sizeof frame_flags) == -1)
//         return -1;
//
//     if (zmtp_msg_size (msg) <= 255) {
//         const uint8_t msg_size = zmtp_msg_size (msg);
//         if (s_tcp_send (self->conn, &msg_size, sizeof msg_size) == -1)
//             return -1;
//     }
//     else {
//         uint8_t buffer [8];
//         const uint64_t msg_size = (uint64_t) zmtp_msg_size (msg);
//         buffer [0] = msg_size >> 56;
//         buffer [1] = msg_size >> 48;
//         buffer [2] = msg_size >> 40;
//         buffer [3] = msg_size >> 32;
//         buffer [4] = msg_size >> 24;
//         buffer [5] = msg_size >> 16;
//         buffer [6] = msg_size >> 8;
//         buffer [7] = msg_size;
//         if (s_tcp_send (self->conn, buffer, sizeof buffer) == -1)
//             return -1;
//     }
//     if (s_tcp_send (self->conn, zmtp_msg_data (msg), zmtp_msg_size (msg)) == -1)
//         return -1;
//     return 0;
// }
//

/*---------------------------------------------------*/



static process_event_t zmtp_do_listen;
static process_event_t zmtp_do_remote_connected;
static process_event_t zmtp_do_send_version_major;
static process_event_t zmtp_do_send_greeting;
static process_event_t zmtp_do_send_ready;
static process_event_t zmtp_call_me_again;
static process_event_t zmtp_process_event;

/* Native Contiki event system gets mixed up with the functioning
of protothreads. The reentry character of it ends up inverting events order...
Use our own event queue.
*/
struct zmtp_event {
  struct zmtp_event *next;
  process_event_t ev;
  void *data;
};
typedef struct zmtp_event zmtp_event_t;
LIST(zmtp_events);
MEMB(zmtp_event_bank, zmtp_event_t, ZMTP_MAX_EVENTS);

void zmtp_init() {
    static uint8_t init_done = 0;

    if(init_done == 0) {
        // memb_init(&zmtp_tcp_endpoints);
        // memb_init(&zmtp_messages);
        // memb_init(&zmtp_channels);
        //
        // PT_INIT(&hdl_conn_pt);
        memb_init(&zmtp_connections);
        memb_init(&zmtp_event_bank);

        zmtp_do_listen = process_alloc_event();
        zmtp_do_remote_connected = process_alloc_event();
        zmtp_do_send_version_major = process_alloc_event();
        zmtp_do_send_greeting = process_alloc_event();
        zmtp_do_send_ready = process_alloc_event();
        zmtp_call_me_again = process_alloc_event();
        zmtp_process_event = process_alloc_event();

        list_init(zmtp_events);

        process_start(&zmtp_process, NULL);

        init_done = 1;
    }
}

int zmtp_add_event(process_event_t ev, void *data) {
    zmtp_event_t *event = memb_alloc(&zmtp_event_bank);
    if(event == NULL) {
        printf("ERROR: Reached exhaustion of events\r\n");
        return -1;
    }

    event->ev = ev;
    event->data = data;
    list_add(zmtp_events, event);

    return 0;
}

int zmtp_pop_event(process_event_t *ev, void **data) {
    zmtp_event_t *event = list_pop(zmtp_events);
    if(event == NULL)
        return -1;

    *ev = event->ev;
    *data = event->data;

    memb_free(&zmtp_event_bank, event);

    return 0;
}

void print_event_name(process_event_t ev) {
    if(ev == zmtp_do_listen) {
        PRINTF("zmtp_do_listen");

    }else if(ev == zmtp_do_remote_connected) {
        PRINTF("zmtp_do_remote_connected");

    }else if(ev == zmtp_do_send_version_major) {
        PRINTF("zmtp_do_send_version_major");

    }else if(ev == zmtp_do_send_greeting) {
        PRINTF("zmtp_do_send_greeting");

    }else if(ev == zmtp_do_send_ready) {
        PRINTF("zmtp_do_send_ready");

    }else if(ev == zmtp_call_me_again) {
        PRINTF("zmtp_call_me_again");

    }else if(ev == zmtp_process_event) {
        PRINTF("zmtp_process_event");

    }else if(ev == zmq_socket_input_activity) {
        PRINTF("zmq_socket_input_activity");

    }else if(ev == zmq_socket_output_activity) {
        PRINTF("zmq_socket_output_activity");

    }else if(ev == PROCESS_EVENT_NONE) {
        PRINTF("PROCESS_EVENT_NONE");

    }else if(ev == PROCESS_EVENT_INIT) {
        PRINTF("PROCESS_EVENT_INIT");

    }else if(ev == PROCESS_EVENT_POLL) {
        PRINTF("PROCESS_EVENT_POLL");

    }else if(ev == PROCESS_EVENT_EXIT) {
        PRINTF("PROCESS_EVENT_EXIT");

    }else if(ev == PROCESS_EVENT_SERVICE_REMOVED) {
        PRINTF("PROCESS_EVENT_SERVICE_REMOVED");

    }else if(ev == PROCESS_EVENT_CONTINUE) {
        PRINTF("PROCESS_EVENT_CONTINUE");

    }else if(ev == PROCESS_EVENT_MSG) {
        PRINTF("PROCESS_EVENT_MSG");

    }else if(ev == PROCESS_EVENT_EXITED) {
        PRINTF("PROCESS_EVENT_EXITED");

    }else if(ev == PROCESS_EVENT_TIMER) {
        PRINTF("PROCESS_EVENT_TIMER");

    }else if(ev == PROCESS_EVENT_COM) {
        PRINTF("PROCESS_EVENT_COM");

    }else if(ev == PROCESS_EVENT_MAX) {
        PRINTF("PROCESS_EVENT_MAX");

    }else{
        PRINTF("%d", ev);
    }
}

int zmtp_listen(zmtp_channel_t *chan, unsigned short port) {
    zmtp_connection_t *conn = zmtp_channel_add_conn(chan);
    if(conn == NULL)
        return -1;

    conn->port = port;
    zmtp_add_event(zmtp_do_listen, conn);
    process_post(&zmtp_process, zmtp_process_event, conn);
    // process_post(&zmtp_process, zmtp_do_listen, conn);

    return 0;
}

/********/

static int zmtp_tcp_input(struct tcp_socket *s, void *conn_ptr, const uint8_t *inputptr, int inputdatalen) {
  zmtp_connection_t *conn = (zmtp_connection_t *) conn_ptr;
  #if (DEBUG) & DEBUG_PRINT
  PRINTF("From ");
  uip_debug_ipaddr_print(&(UIP_IP_BUF->srcipaddr));
  PRINTF(", received input %d bytes: ", inputdatalen);
  print_data(inputptr, inputdatalen);
  PRINTF(" on connection %p\r\n", conn);
  #endif

  if((conn->validated & CONNECTION_VALIDATED) != CONNECTION_VALIDATED) {
      if(!(conn->validated & CONNECTION_VALIDATED_SIGNATURE)) {
          if((inputdatalen >= 10) &&
             (inputptr[0] == 0xFF) &&
             (inputptr[9] == 0x7F)
            ) {
              conn->validated |= CONNECTION_VALIDATED_SIGNATURE;
              zmtp_add_event(zmtp_do_send_version_major, conn);
              process_post(&zmtp_process, zmtp_process_event, conn);
              PRINTF("Validated signature\n");
              return inputdatalen - 10;
          }else{
              printf("WARNING: Remote sent bad signature\r\n");
              uip_close();
              return 0;
          }

      }else if(!(conn->validated & CONNECTION_VALIDATED_VERSION)) {
          if((inputdatalen >= 1) && (inputptr[0] >= 3)) {
              conn->validated |= CONNECTION_VALIDATED_VERSION;
              zmtp_add_event(zmtp_do_send_greeting, conn);
              process_post(&zmtp_process, zmtp_process_event, conn);
              PRINTF("Validated version\n");
              return inputdatalen - 1;
          }else{
              printf("WARNING: Remote sent unsupported/bad version\r\n");
              uip_close();
              return 0;
          }

      }else if(!(conn->validated & CONNECTION_VALIDATED_GREETING)) {
          if(inputdatalen >= 53) {
              conn->validated |= CONNECTION_VALIDATED_GREETING;
              PRINTF("Validated greeting\n");
              zmtp_add_event(zmtp_do_send_ready, conn);
              process_post(&zmtp_process, zmtp_process_event, conn);
              return inputdatalen - 53;
          }else{
              printf("WARNING: Remote sent bad greeting version\r\n");
              uip_close();
              return 0;
          }

      }else if(!(conn->validated & CONNECTION_VALIDATED_READY)) {
          uint8_t size_offset;
          if(!(inputptr[0] & ZMQ_MSG_LARGE))
              size_offset = 1;
          else
              size_offset = 8;

          if((inputdatalen >= (7 + size_offset)) &&
             (inputptr[0] & ZMQ_MSG_COMMAND) && // Command flag
             (inputptr[1+size_offset] == 5) && // len('READY')
             (!strncmp((const char *) &inputptr[2+size_offset], "READY", 5))
            ) {

              const uint8_t *pos = &inputptr[7 + size_offset];
              while(pos < (inputptr + inputdatalen)) {
                  uint8_t field_name_size = *pos++;
                  if((pos + field_name_size) > (inputptr + inputdatalen)) {
                      printf("WARNING: Remote sent bad field name size\r\n");
                      uip_close();
                      return 0;
                  }
                  const char *field_name = (const char *) pos;
                  pos += field_name_size;
                  if((pos + 4) > (inputptr + inputdatalen)) {
                      printf("WARNING: Remote did not sent enought data for field name\r\n");
                      uip_close();
                      return 0;
                  }
                  // TODO: deal with endiannes
                  uint32_t value_size = (pos[0] << 24) | (pos[1] << 16) | (pos[2] << 8) | pos[3];
                  pos += 4;
                  if((pos + value_size) > (inputptr + inputdatalen)) {
                      printf("WARNING: Remote sent bad field value size\r\n");
                      uip_close();
                      return 0;
                  }
                  const char *field_value = (const char *) pos;
                  pos += value_size;

                  if((field_name_size == 11) && !strncasecmp(field_name, "Socket-Type", 11)) {
                      zmq_socket_type_t socket_type;
                      if(value_size == 3) {
                          if(!strncmp(field_value, "PUB", 3)) {
                              socket_type = ZMQ_PUB;
                              if((conn->channel->socket_type != ZMQ_SUB) && (conn->channel->socket_type != ZMQ_XSUB)) {
                                  printf("WARNING: Can't connect something else than a SUB or XSUB to a PUB\r\n");
                                  uip_close();
                                  return 0;
                              }
                          }else if(!strncmp(field_value, "SUB", 3)) {
                              socket_type = ZMQ_SUB;
                              if((conn->channel->socket_type != ZMQ_PUB) && (conn->channel->socket_type != ZMQ_XPUB)) {
                                  printf("WARNING: Can't connect something else than a PUB or XPUB to a SUB\r\n");
                                  uip_close();
                                  return 0;
                              }
                          }else if(!strncmp(field_value, "REQ", 3)) {
                              socket_type = ZMQ_REQ;
                              if((conn->channel->socket_type != ZMQ_REP) && (conn->channel->socket_type != ZMQ_ROUTER)) {
                                  printf("WARNING: Can't connect something else than a REP or ROUTER to a REQ\r\n");
                                  uip_close();
                                  return 0;
                              }
                          }else if(!strncmp(field_value, "REP", 3)) {
                              socket_type = ZMQ_REP;
                              if((conn->channel->socket_type != ZMQ_REQ) && (conn->channel->socket_type != ZMQ_DEALER)) {
                                  printf("WARNING: Can't connect something else than a REQ or DEALER to a REQ\r\n");
                                  uip_close();
                                  return 0;
                              }
                          }else{
                              printf("WARNING: Remote sent bad socket type\r\n");
                              uip_close();
                              return 0;
                          }
                      }else if(value_size == 4) {
                          if(!strncmp(field_value, "PUSH", 4)) {
                              socket_type = ZMQ_PUSH;
                              if(conn->channel->socket_type != ZMQ_PULL) {
                                  printf("WARNING: Can't connect something else than a PULL to a PUSH\r\n");
                                  uip_close();
                                  return 0;
                              }
                          }else if(!strncmp(field_value, "PULL", 4)) {
                              socket_type = ZMQ_PULL;
                              if(conn->channel->socket_type != ZMQ_PUSH) {
                                  printf("WARNING: Can't connect something else than a PUSH to a PULL\r\n");
                                  uip_close();
                                  return 0;
                              }
                          }else if(!strncmp(field_value, "XPUB", 4)) {
                              socket_type = ZMQ_XPUB;
                              if((conn->channel->socket_type != ZMQ_SUB) && (conn->channel->socket_type != ZMQ_XSUB)) {
                                  printf("WARNING: Can't connect something else than a SUB or XSUB to a XPUB\r\n");
                                  uip_close();
                                  return 0;
                              }
                          }else if(!strncmp(field_value, "XSUB", 4)) {
                              socket_type = ZMQ_XSUB;
                              if((conn->channel->socket_type != ZMQ_PUB) && (conn->channel->socket_type != ZMQ_XPUB)) {
                                  printf("WARNING: Can't connect something else than a PUB or XPUB to a XSUB\r\n");
                                  uip_close();
                                  return 0;
                              }
                          }else{
                              printf("WARNING: Remote sent bad socket type\r\n");
                              uip_close();
                              return 0;
                          }
                      }else if(value_size == 6) {
                          if(!strncmp(field_value, "DEALER", 6)) {
                              socket_type = ZMQ_DEALER;
                              if((conn->channel->socket_type != ZMQ_REP) && (conn->channel->socket_type != ZMQ_DEALER) && (conn->channel->socket_type != ZMQ_ROUTER)) {
                                  printf("WARNING: Can't connect something else than a REP, DEALER or ROUTER to a DEALER\r\n");
                                  uip_close();
                                  return 0;
                              }
                          }else if(!strncmp(field_value, "ROUTER", 6)) {
                              socket_type = ZMQ_ROUTER;
                              if((conn->channel->socket_type != ZMQ_REQ) && (conn->channel->socket_type != ZMQ_DEALER) && (conn->channel->socket_type != ZMQ_ROUTER)) {
                                  printf("WARNING: Can't connect something else than a REQ, DEALER or ROUTER to a ROUTER\r\n");
                                  uip_close();
                                  return 0;
                              }
                          }else{
                              printf("WARNING: Remote sent bad socket type\r\n");
                              uip_close();
                              return 0;
                          }
                      }else{
                          printf("WARNING: Remote sent bad socket type\r\n");
                          uip_close();
                          return 0;
                      }
                      PRINTF("Got socket type %d\n", socket_type);
                  }else if((field_name_size == 8) && !strncasecmp(field_name, "Identity", 8)) {
                      char buf[128];
                      strncpy(buf, field_value, value_size);
                      buf[value_size] = 0;
                      PRINTF("Got Identity %s\n", buf);
                  }
              }

              conn->validated |= CONNECTION_VALIDATED_READY;
              PRINTF("Validated ready\n");

              process_poll(conn->channel->notify_process_input);
              if(conn->channel->notify_process_input != conn->channel->notify_process_output)
                  process_poll(conn->channel->notify_process_output);
          }else{
              printf("WARNING: Remote sent bad ready\r\n");
              uip_close();
              return 0;
          }
      }
  }else{
      PRINTF("Connection is validated\n");
      int read = 0;
      const uint8_t *pos = inputptr;
      while(pos < (inputptr + inputdatalen)) {
          zmq_msg_t *msg = _zmq_msg_from_wire(pos, inputdatalen, &read);
          if(msg == NULL) {
              uip_close();
              return 0;
          }
          pos += read;
          PRINTF("Read a message from wire\n");
          zmtp_connection_add_in_msg(conn, msg);
          process_post(conn->channel->notify_process_input, zmq_socket_input_activity, NULL);
      }
  }

  return 0;
}

static void zmtp_tcp_event(struct tcp_socket *s, void *conn_ptr, tcp_socket_event_t ev) {
  zmtp_connection_t *conn = (zmtp_connection_t *) conn_ptr;
  // PRINTF("EV > %p\n", conn);
  switch(ev) {
    case TCP_SOCKET_CONNECTED:
        PRINTF("event TCP_SOCKET_CONNECTED for ");

        if(conn->addr == NULL) // Was a listen, need to create a new connection for other clients
            zmtp_listen(conn->channel, conn->port);

        zmtp_add_event(zmtp_do_remote_connected, conn);
        process_post(&zmtp_process, zmtp_process_event, conn);
        // process_post(&zmtp_process, zmtp_do_remote_connected, conn);
        break;
    case TCP_SOCKET_CLOSED:
        PRINTF("event TCP_SOCKET_CLOSED for ");
        if(conn->addr == NULL)
            zmtp_channel_close_conn(conn->channel, conn);
        break;
    case TCP_SOCKET_TIMEDOUT:
        PRINTF("event TCP_SOCKET_TIMEDOUT for ");
        if(conn->addr == NULL)
            zmtp_channel_close_conn(conn->channel, conn);
        break;
    case TCP_SOCKET_ABORTED:
        PRINTF("event TCP_SOCKET_ABORTED for ");
        if(conn->addr == NULL)
            zmtp_channel_close_conn(conn->channel, conn);
        break;
    case TCP_SOCKET_DATA_SENT:
        PRINTF("event TCP_SOCKET_DATA_SENT for ");
        conn->sent_done = 1;
        process_post(&zmtp_process, zmtp_call_me_again, conn);
        break;
    default:
        PRINTF("event %d for ", ev);
  }
  #if (DEBUG) & DEBUG_PRINT
  uip_debug_ipaddr_print(&(UIP_IP_BUF->srcipaddr));
  PRINTF("\r\n");
  #endif
}

/*---------------------------------------------------------------------------*/
static void print_data(const uint8_t *data, int data_size) {
  int i;
  for(i=0 ; i < data_size ; i++)
    printf("%02hhX ", data[i]);
}
/*---------------------------------------------------------------------------*/

// static struct psock ps;
// static uint8_t buffer[100];
//
// PT_THREAD(test(struct psock *p)) {//}, const void *data, size_t len)) {
//
// uint8_t outgoing_signn[10] = { 0xff, 0, 0, 0, 0, 0, 0, 0, 1, 0x7f };
// size_t len = sizeof outgoing_signn;
// void *data = outgoing_signn;
//
//     PSOCK_BEGIN(p);
//
//     printf("Trying to send\r\n");
//
//     //PT_WAIT_THREAD(&hdl_conn_pt, s_tcp_recv (self->conn, outgoing.signature, sizeof outgoing.signature));
//     // s_tcp_recv (self->conn, outgoing.signature, sizeof outgoing.signature);
//
//     printf("Sending (%d): ", len);
//     print_data(data, len);
//     printf("\r\n");
//
//     PSOCK_SEND(p, (uint8_t *) data, len);
//
//     printf("Sent (%d): ", len);
//     print_data(data, len);
//     printf("\r\n");
//
//     printf("Done\r\n");
//     PSOCK_END(p);
// }
//
// PT_THREAD(test2(struct psock *p)) {
//     PT_BEGIN(&hdl_conn_pt);
//     printf("Calling\r\n");
//     PT_WAIT_THREAD(&hdl_conn_pt, test(p));//, outgoing_sign, sizeof outgoing_sign));
//     // test(p);
//     printf("Called\r\n");
//     PT_END(&hdl_conn_pt);
// }
//
// zmtp_channel_t *chan = NULL;

/*---------------------------------------------------------*/

// static struct tcp_socket socket;
//
// #define INPUTBUFSIZE 256 // Must be power of two and no greater than 256
// static uint8_t inputbuf[INPUTBUFSIZE];
//
// #define OUTPUTBUFSIZE 400
// static uint8_t outputbuf[OUTPUTBUFSIZE];
//
// static uint8_t input_ringbuf_data[INPUTBUFSIZE];
// static struct ringbuf input_ringbuf;
//
// static int
// input(struct tcp_socket *s, void *ptr,
//       const uint8_t *inputptr, int inputdatalen)
// {
//   printf("From ");
//   uip_debug_ipaddr_print(&(UIP_IP_BUF->srcipaddr));
//   printf(", received input %d bytes: ", inputdatalen);
//   print_data(inputptr, inputdatalen);
//   printf("\r\n");
//
//   int i, rc;
//   for(i=0 ; i < inputdatalen ; i++) {
//     rc = ringbuf_put(&input_ringbuf, inputptr[i]);
//     if(rc == 0) { // Buffer full
//       printf("Input ring buffer is full! Discarding data as tcp-socket don't implement yet not reading everything: ");
//       print_data(inputptr + i, inputdatalen - i);
//       printf("\r\n");
//       return inputdatalen - i;
//     }
//   }
//
//   return 0;
// }
//
// static
// PT_THREAD(my_recv_(struct pt *process_pt, process_event_t ev, process_data_t data, uint8_t *buffer, size_t len, uint8_t *done)) {
//   PROCESS_BEGIN();
//
//   *done = 0;
//   while(len > 0) {
//     int read = ringbuf_get(&input_ringbuf);
//     // printf("> Read from RB: %d\r\n", read);
//     if(read == -1) {
//       // printf("> Yielding\r\n");
//       // PT_YIELD(pt);
//       PROCESS_PAUSE();
//     }else{
//       *buffer = (uint8_t) read;
//       buffer++;
//       len--;
//     }
//   }
//   printf("> Done\r\n");
//   *done = 1;
//
//   PROCESS_END();
// }
//
// static void my_recv(uint8_t *buffer, size_t len) {
//   int done;
//   do{
//     my_recv_(io_thread.process_pt, io_thread.ev, io_thread.data, buffer, len, &done);
//   }while(done == 0);
//   // while(PT_SCHEDULE(my_recv_(pt, buffer, len)));
// }
//
// static void
// event(struct tcp_socket *s, void *ptr,
//       tcp_socket_event_t ev)
// {
//   switch(ev) {
//     case TCP_SOCKET_CONNECTED:
//         printf("event TCP_SOCKET_CONNECTED for ");
//         break;
//     case TCP_SOCKET_CLOSED:
//         printf("event TCP_SOCKET_CLOSED for ");
//         break;
//     case TCP_SOCKET_TIMEDOUT:
//         printf("event TCP_SOCKET_TIMEDOUT for ");
//         break;
//     case TCP_SOCKET_ABORTED:
//         printf("event TCP_SOCKET_ABORTED for ");
//         break;
//     case TCP_SOCKET_DATA_SENT:
//         printf("event TCP_SOCKET_DATA_SENT for ");
//         break;
//     default:
//         printf("event %d for ", ev);
//   }
//   uip_debug_ipaddr_print(&(UIP_IP_BUF->srcipaddr));
//   printf("\r\n");
// }
//
// uint8_t test_buf[100];

/*---------------------------------------------------------------------------*/

int zmtp_process_post(process_event_t ev, process_data_t data) {
    return process_post(&zmtp_process, ev, data);
}

PROCESS_THREAD(zmtp_process, ev, data)
{
    // printf("> zmtp_process %d ", process_pt->lc);
    // print_event_name(ev);
    // printf(" %p\n", data);

    PROCESS_BEGIN();

  // chan = zmtp_channel_new();
  // printf("Channel: %p\r\n", chan);
  // int rc = zmtp_channel_tcp_listen(chan, SERVER_PORT);
  // printf("listen on %d: %d\r\n", SERVER_PORT, rc);
//
  // PSOCK_INIT(&ps, buffer, sizeof(buffer));
  // printf("Initialised psock\r\n");
  //
  // tcp_listen(UIP_HTONS(SERVER_PORT));
  // printf("listening on 9999");
//
  // int m1i = -1;
  // uint8_t m1c = -1;
  //
  // int m1ci = (int) m1c;
  // printf("sizeof int: %d\r\n", sizeof(int));
  // printf("m1i: %08hhX / %08X =eq -1=> %d\r\n", m1i, m1i, (m1i == -1));
  // printf("m1ci: %08hhX /  %08X =eq -1=> %d\r\n", m1ci, m1ci, (m1ci == -1));
//
  // ringbuf_init(&input_ringbuf, input_ringbuf_data, INPUTBUFSIZE);
  //
  // tcp_socket_register(&my_chan.conn.socket, NULL,
  //             inputbuf, sizeof(inputbuf),
  //             outputbuf, sizeof(outputbuf),
  //             zmtp_tcp_input, zmtp_tcp_event);
  // tcp_socket_listen(&my_chan.conn.socket, SERVER_PORT);


  while(1) {
      PROCESS_WAIT_EVENT();
      // PROCESS_WAIT_EVENT_UNTIL(ev == tcpip_event);
      // printf("Got event %d, %d\r\n", ev, uip_flags);
      // if(uip_connected()) {
      //     printf("Connected\r\n");
      //     while(!(uip_aborted() || uip_closed() || uip_timedout())) {
      //         PROCESS_WAIT_EVENT_UNTIL(ev == tcpip_event);
      //         printf("Got event ! %d, %d\r\n", ev, uip_flags);
      //         // test2(&ps);
      //         handle_connection(chan);
      //     }
      // }
//
      // my_recv(test_buf, 3);
      // printf("Got: ");
      // print_data(test_buf, 3);
      // printf("\r\n");

      // printf("Got event %d %p\n", ev, data);
      if(ev == zmtp_process_event) {
          if(zmtp_pop_event(&ev, &data) != 0)
              continue;
          PRINTF(">> Event is now ");
          print_event_name(ev);
          PRINTF(" %p\n", data);
      }

      if(ev == zmtp_do_listen) {
          zmtp_connection_tcp_listen(data);

      }else if(ev == zmtp_do_remote_connected) {
          PRINTF("Do remote connect\n");
          process_post(&zmtp_process, zmtp_call_me_again, data);
          PROCESS_WAIT_THREAD(zmtp_remote_connected(data), zmtp_do_remote_connected);
          PRINTF("Connect done\n");

      }else if(ev == zmtp_do_send_version_major) {
        process_post(&zmtp_process, zmtp_call_me_again, data);
        PROCESS_WAIT_THREAD(zmtp_send_version_major(data), zmtp_do_send_version_major);

      }else if(ev == zmtp_do_send_greeting) {
        process_post(&zmtp_process, zmtp_call_me_again, data);
        PROCESS_WAIT_THREAD(zmtp_send_greeting(data), zmtp_do_send_greeting);

      }else if(ev == zmtp_do_send_ready) {
        process_post(&zmtp_process, zmtp_call_me_again, data);
        PROCESS_WAIT_THREAD(zmtp_send_ready(data), zmtp_do_send_ready);

      }else if(ev == zmq_socket_output_activity) {
        static zmq_msg_t *msg = NULL;
        msg = zmtp_connection_pop_out_msg(data);
        if(msg == NULL) // ?!
            continue;

        process_post(&zmtp_process, zmtp_call_me_again, data);
        PROCESS_WAIT_THREAD(zmtp_send_msg(data, msg), zmq_socket_output_activity);
        process_post(((zmtp_connection_t *) data)->channel->notify_process_output, zmq_socket_output_activity, NULL);

      }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
