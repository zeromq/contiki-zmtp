/*
 * Copyright (c) 2016 Contributors as noted in the AUTHORS file
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "pub.h"

#include "sys/pt.h"
#include "net/ip/uip-debug.h"
#include <stdio.h>
#include <stdlib.h>


PROCESS(zmq_pub_subscription_receiver, "ZMQ PUB subscription receiver");

void zmq_pub_init(zmq_socket_t *self) {
    self->in_conn = NULL;
    self->out_conn = NULL;

    self->recv = NULL;
    self->recv_multipart = NULL;
    self->send = zmq_pub_send;

    process_start(&zmq_pub_subscription_receiver, self);
}

void do_subscribe(zmtp_connection_t *conn, zmq_msg_t *msg) {
    zmtp_sub_topic_t *topic = zmtp_sub_topic_new(zmq_msg_data(msg) + 1, zmq_msg_size(msg) - 1);
    if(topic == NULL)
        return;

    zmtp_sub_topic_item_t *topic_item = zmtp_sub_topic_item_new(topic);
    if(topic_item == NULL) {
        zmtp_sub_topic_destroy(&topic);
        return;
    }

    list_add(conn->subscribed_topics, topic_item);

    #if (DEBUG) & DEBUG_PRINT
    printf("Added subscription: ");
    uint8_t *pos = topic->data;
    while(pos < (topic->data + topic->size))
        printf("%c", *pos++);
    printf("\r\n");
    #endif
}

void do_unsubscribe(zmtp_connection_t *conn, zmq_msg_t *msg) {
    const uint8_t *topic_data = zmq_msg_data(msg) + 1;
    uint8_t topic_size = zmq_msg_size(msg) - 1;

    zmtp_sub_topic_item_t *topic_item = list_head(conn->subscribed_topics);
    while((topic_item != NULL) &&
          (topic_item->topic->size == topic_size) &&
          (!strncmp((const char *) topic_item->topic->data, (const char *) topic_data, topic_size)))
        topic_item = list_item_next(topic_item);

    if(topic_item == NULL)
        return;

    list_remove(conn->subscribed_topics, topic_item);
    zmtp_sub_topic_destroy(&topic_item->topic);
    zmtp_sub_topic_item_destroy(&topic_item);
}

uint8_t match_subscriptions(zmtp_connection_t *conn, zmq_msg_t *msg) {
    zmtp_sub_topic_item_t *topic_item = list_head(conn->subscribed_topics);
    while(topic_item != NULL) {
        if(topic_item->topic->size <= zmq_msg_size(msg)) {
            if(!strncmp((const char *) topic_item->topic->data, (const char *) zmq_msg_data(msg), topic_item->topic->size))
                return 1;
        }
        topic_item = list_item_next(topic_item);
    }
    return 0;
}

PROCESS_THREAD(zmq_pub_subscription_receiver, ev, data) {
    PRINTF("> zmq_pub_subscription_receiver %d, %d, %p\r\n", process_pt->lc, ev, data);
    PROCESS_BEGIN();

    // These static variables are what limits PUB socket to only one
    static zmq_socket_t *self = NULL;

    self = data;
    self->channel.notify_process_input = PROCESS_CURRENT();

    self->in_conn = list_head(self->channel.connections);

    zmq_msg_t *msg = NULL;
    while(1) {
        while(self->in_conn != NULL) {
            if((self->in_conn->validated & CONNECTION_VALIDATED) != CONNECTION_VALIDATED) {
                self->in_conn = list_item_next(self->in_conn);
                continue;
            }

            msg = zmtp_connection_pop_in_msg(self->in_conn);
            if(msg != NULL) {
                if(zmq_msg_data(msg)[0] == 1)
                    do_subscribe(self->in_conn, msg);
                else if(zmq_msg_data(msg)[0] == 0)
                    do_unsubscribe(self->in_conn, msg);

                zmq_msg_destroy(&msg);
            }
            self->in_conn = list_item_next(self->in_conn);
        }

        PROCESS_WAIT_EVENT_UNTIL(ev == zmq_socket_input_activity);

        self->in_conn = list_head(self->channel.connections);
        msg = NULL;
    }

    PROCESS_END();
}

PT_THREAD(zmq_pub_send(zmq_socket_t *self, zmq_msg_t *msg)) {
    // TODO: implement HWM
    LOCAL_PT(pt);
    PRINTF("> zmq_pub_send %d %p\r\n", pt.lc, msg);
    PT_BEGIN(&pt);

    self->out_conn = list_head(self->channel.connections);

    while(self->out_conn != NULL) {
        // TODO: check if queue is full (HWM)
        if((self->out_conn->validated & CONNECTION_VALIDATED) != CONNECTION_VALIDATED) {
            self->out_conn = list_item_next(self->out_conn);
            continue;
        }

        if(match_subscriptions(self->out_conn, msg)) {
            // NB. there is a bug right here.
            // When serving several peers, the msg gets added to the out queues of each of those connections.
            // The problem is: these queues are implemented with Contiki linked list,
            // Which requires to have a 'next' pointer as the first member of the struct.
            // When adding the same msg to several connections' queue, the next pointer gets overwritten every time...
            zmtp_connection_add_out_msg(self->out_conn, msg);

            zmtp_process_post(zmq_socket_output_activity, self->out_conn);
            PT_WAIT_UNTIL(&pt, self->out_conn->out_size <= 0);
        }

        self->out_conn = list_item_next(self->out_conn);
    }

    PT_END(&pt);
}
