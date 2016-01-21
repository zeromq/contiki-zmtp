/*
 * Copyright (c) 2016 Axel Voitier
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef ZMQ_ROUTER_H_
#define ZMQ_ROUTER_H_

#include "zmtp.h"


void zmq_router_init(zmq_socket_t *self);
PT_THREAD(zmq_router_recv(zmq_socket_t *self, zmq_msg_t **msg_ptr));
PT_THREAD(zmq_router_recv_multipart(zmq_socket_t *self, list_t msg_list));
PT_THREAD(zmq_router_send(zmq_socket_t *self, zmq_msg_t *msg_ptr));

#endif
