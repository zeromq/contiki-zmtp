/*
 * Copyright (c) 2016 Contributors as noted in the AUTHORS file
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "sub.h"

#include <stdlib.h>


void zmq_sub_init(zmq_socket_t *self) {
    self->in_conn = NULL;
    self->out_conn = NULL;

    self->recv = zmq_socket_recv_fair_queue;
    self->recv_multipart = zmq_socket_recv_multipart_fair_queue;
    self->send = NULL;
}

int zmq_sub_subscribe(zmq_socket_t *self, const char *data) {
    zmtp_sub_topic_t *topic = zmtp_sub_topic_new((const uint8_t *) data, strlen(data));
    if(topic == NULL)
        return 0;

    zmtp_sub_topic_item_t *topic_item = zmtp_sub_topic_item_new(topic);
    if(topic_item == NULL) {
        zmtp_sub_topic_destroy(&topic);
        return 0;
    }

    list_add(self->channel.subscribe_topics, topic_item);

    zmtp_process_post(zmq_socket_add_subscription, &self->channel);

    return 1;
}
