#ifndef ZMTP_H_
#define ZMTP_H_

/*
Limitations:
- Dealer does not support custom identity
- Blocks processing of incoming data and other outgoing data when sending to a peer
*/

/*------------ PUBLIC API -------------*/

#ifndef ZMTP_INPUT_BUFFER_SIZE
  #define ZMTP_INPUT_BUFFER_SIZE 400
#endif

#ifndef ZMTP_OUTPUT_BUFFER_SIZE
  #define ZMTP_OUTPUT_BUFFER_SIZE 400
#endif

#ifndef ZMTP_MAX_CONNECTIONS
  #define ZMTP_MAX_CONNECTIONS 10
#endif

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
} zmq_socket_t;

struct zmtp_connection {
  // struct psock ps;
  // uint8_t buffer[ZMTP_INPUT_BUFFER_SIZE];
  struct zmtp_connection *next;
  uint8_t inputbuf[ZMTP_INPUT_BUFFER_SIZE];
  uint8_t outputbuf[ZMTP_OUTPUT_BUFFER_SIZE];
  struct tcp_socket socket;
  uip_ipaddr_t *addr;
  uint16_t port;
  uint8_t sent_done;
  uint8_t validated;
  struct _zmtp_channel_t *channel;
};
typedef struct zmtp_connection zmtp_connection_t;

struct _zmtp_channel_t {
    LIST_STRUCT(connections);
    // zmtp_connection_t conn;
    zmq_socket_t socket_type;
};
typedef struct _zmtp_channel_t zmtp_channel_t;

zmtp_connection_t *zmtp_connection_new();
void zmtp_connection_destroy (zmtp_connection_t **self_p);
void zmtp_connection_init(zmtp_connection_t *self);
int zmtp_connection_tcp_connect (zmtp_connection_t *self);
int zmtp_connection_tcp_listen (zmtp_connection_t *self);

void zmtp_init();
int zmtp_listen(zmtp_channel_t *chan, unsigned short port);

zmtp_channel_t *zmtp_channel_new (zmq_socket_t socket_type);
void zmtp_channel_destroy (zmtp_channel_t **self_p);
void zmtp_channel_init(zmtp_channel_t *self, zmq_socket_t socket_type);

/*------------ PRIVATE API ------------*/

#define CONNECTION_VALIDATED_SIGNATURE 0x01
#define CONNECTION_VALIDATED_VERSION 0x02
#define CONNECTION_VALIDATED_GREETING 0x04
#define CONNECTION_VALIDATED_READY 0x08
#define CONNECTION_VALIDATED (CONNECTION_VALIDATED_SIGNATURE | \
                              CONNECTION_VALIDATED_VERSION   | \
                              CONNECTION_VALIDATED_GREETING  | \
                              CONNECTION_VALIDATED_READY)

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

#define LOCAL_PT(pt_) static struct pt pt_; static uint8_t pt_inited=0; if(pt_inited == 0) { PT_INIT(&pt_); pt_inited = 1; }

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

#endif