#ifndef ZMTP_H_
#define ZMTP_H_

/*
Limitations:
- Only one endpoint/connection per channel (eg. one bind or one connect).
*/

/*------------ PUBLIC API -------------*/

#ifndef ZMTP_INPUT_BUFFER_SIZE
  #define ZMTP_INPUT_BUFFER_SIZE 400
#endif

#ifndef ZMTP_OUTPUT_BUFFER_SIZE
  #define ZMTP_OUTPUT_BUFFER_SIZE 400
#endif

struct zmtp_connection {
  // struct psock ps;
  // uint8_t buffer[ZMTP_INPUT_BUFFER_SIZE];
  struct tcp_socket socket;
  uip_ipaddr_t addr;
  uint16_t port;
  uint8_t inputbuf[ZMTP_INPUT_BUFFER_SIZE];
  uint8_t outputbuf[ZMTP_OUTPUT_BUFFER_SIZE];
};
typedef struct zmtp_connection zmtp_connection_t;

struct _zmtp_channel_t {
    zmtp_connection_t conn;
};
typedef struct _zmtp_channel_t zmtp_channel_t;

void zmtp_init();
void zmtp_listen(zmtp_channel_t *chan, unsigned short port);

zmtp_channel_t *zmtp_channel_new ();
void zmtp_channel_destroy (zmtp_channel_t **self_p);
void zmtp_channel_register(zmtp_channel_t *self);

/*------------ PRIVATE API ------------*/


PROCESS(zmtp_process, "ZMTP process");

static int zmtp_tcp_input(struct tcp_socket *s, void *conn_ptr, const uint8_t *inputptr, int inputdatalen);
static void zmtp_tcp_event(struct tcp_socket *s, void *conn_ptr, tcp_socket_event_t ev);

int zmtp_channel_tcp_connect (zmtp_channel_t *self);
int zmtp_channel_tcp_listen (zmtp_channel_t *self);

static void print_data(const uint8_t *data, int data_size);

#endif
