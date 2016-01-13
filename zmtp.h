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
  #define ZMTP_OUTPUT_BUFFER_SIZE 10
#endif

struct zmtp_connection {
  // struct psock ps;
  // uint8_t buffer[ZMTP_INPUT_BUFFER_SIZE];
  struct tcp_socket socket;
  uip_ipaddr_t addr;
  uint16_t port;
  uint8_t inputbuf[ZMTP_INPUT_BUFFER_SIZE];
  uint8_t outputbuf[ZMTP_OUTPUT_BUFFER_SIZE];
  uint8_t sent_done;
  struct pt pt;
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
void zmtp_channel_init(zmtp_channel_t *self);

/*------------ PRIVATE API ------------*/


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

#define PROCESS_WAIT_THREAD(thread) \
  do {						\
    PT_YIELD_FLAG = 0;				\
    LC_SET((pt)->lc);				\
    if((PT_YIELD_FLAG == 0) || !(cond)) {	\
      return PT_YIELDED;			\
    }						\
  } while(0)

#endif
