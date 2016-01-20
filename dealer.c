/*
 * Copyright (c) 2016 Axel Voitier
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "dealer.h"


void zmq_dealer_init(zmq_socket_t *self) {
    self->in_conn = NULL;
    self->out_conn = NULL;

    self->recv = zmq_socket_recv_fair_queue;
    self->recv_multipart = zmq_socket_recv_multipart_fair_queue;
    self->send = zmq_socket_send_round_robin_block;
}
