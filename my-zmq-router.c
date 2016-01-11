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

#include "contiki-net.h"
#include "net/ip/uip-debug.h"
#include "sys/cc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UIP_IP_BUF   ((struct uip_tcpip_hdr *)&uip_buf[UIP_LLH_LEN])

#define SERVER_PORT 9999

static struct tcp_socket socket;

#define INPUTBUFSIZE 400
static uint8_t inputbuf[INPUTBUFSIZE];

#define OUTPUTBUFSIZE 400
static uint8_t outputbuf[OUTPUTBUFSIZE];

typedef enum {
  _ZMQ_DO_NOTHING,
  _ZMQ_CLOSE_CONN,
  _ZMQ_SEND_SIGN,
} _zmq_send_thread_do_t;

_zmq_send_thread_do_t send_thread_do;

uint8_t data_sign[11] = {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x03};

PROCESS(tcp_server_process, "TCP echo process");
AUTOSTART_PROCESSES(&tcp_server_process);
static uint8_t get_received;
static int bytes_to_send;
/*---------------------------------------------------------------------------*/
static void print_data(const uint8_t *data, int data_size) {
  for(int i=0 ; i < data_size ; i++)
    printf("%02hhX ", data[i]);
}
/*---------------------------------------------------------------------------*/
static int
input(struct tcp_socket *s, void *ptr,
      const uint8_t *inputptr, int inputdatalen)
{
  printf("From ");
  uip_debug_ipaddr_print(&(UIP_IP_BUF->srcipaddr));
  printf(", received input %d bytes: ", inputdatalen);
  print_data(inputptr, inputdatalen);
  printf("\r\n");

  if(inputdatalen >= 10) {
    if((inputptr[0] == 0xFF) && (inputptr[9] == 0x7F)) {
      send_thread_do = _ZMQ_SEND_SIGN;
      return 0;
    }
  }

  return 0;
}
/*---------------------------------------------------------------------------*/
static void
event(struct tcp_socket *s, void *ptr,
      tcp_socket_event_t ev)
{
  switch(ev) {
    case TCP_SOCKET_CONNECTED:
        printf("event TCP_SOCKET_CONNECTED for ");
        break;
    case TCP_SOCKET_CLOSED:
        printf("event TCP_SOCKET_CLOSED for ");
        break;
    case TCP_SOCKET_TIMEDOUT:
        printf("event TCP_SOCKET_TIMEDOUT for ");
        break;
    case TCP_SOCKET_ABORTED:
        printf("event TCP_SOCKET_ABORTED for ");
        break;
    case TCP_SOCKET_DATA_SENT:
        printf("event TCP_SOCKET_DATA_SENT for ");
        break;
    default:
        printf("event %d for ", ev);
  }
  uip_debug_ipaddr_print(&(UIP_IP_BUF->srcipaddr));
  printf("\r\n");
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(tcp_server_process, ev, data)
{
  PROCESS_BEGIN();

  int data_size, original_data_size, sent, tosend;
  uint8_t *data = NULL;

  tcp_socket_register(&socket, NULL,
               inputbuf, sizeof(inputbuf),
               outputbuf, sizeof(outputbuf),
               input, event);
  tcp_socket_listen(&socket, SERVER_PORT);

  printf("Listening on %d\r\n", SERVER_PORT);
  send_thread_do = _ZMQ_DO_NOTHING;
  data_size = 0;
  while(1) {
    PROCESS_PAUSE();

    if(send_thread_do != _ZMQ_DO_NOTHING)
      printf("Do %d\r\n", send_thread_do);

    switch(send_thread_do) {
      case _ZMQ_CLOSE_CONN:
        tcp_socket_close(&socket);
        break;
      case _ZMQ_SEND_SIGN:
        data = data_sign;
        data_size = 11;
        break;
      case _ZMQ_DO_NOTHING:
        break;
    }

    if((send_thread_do != _ZMQ_DO_NOTHING) && (data_size > 0)) {
      printf("data size: %d\r\n", data_size);

      original_data_size = data_size;
      sent = tcp_socket_send(&socket, data, data_size);
      data_size -= sent;

      while(data_size > 0) {
        PROCESS_PAUSE();
        tosend = MIN(data_size, sizeof(outputbuf));
        sent = tcp_socket_send(&socket, (uint8_t *)"", tosend);
        data_size -= sent;
      }
      data_size = 0;

      printf("Sent data: ");
      print_data(data, original_data_size);
      printf("\r\n");
    }

    send_thread_do = _ZMQ_DO_NOTHING;
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
