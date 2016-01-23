#include "sys/autostart.h"
#include "zmq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PROCESS(test_push_pull_bind, "Test push-pull bind");
AUTOSTART_PROCESSES(&test_push_pull_bind);

zmq_socket_t push, pull;
struct etimer et;
LIST(read_list);

PROCESS_THREAD(test_push_pull_bind, ev, data) {
    // printf("> test_pub_bind %d, %d, %p\n", process_pt->lc, ev, data);
    // print_event_name(ev);
    // printf("\r\n");
    PROCESS_BEGIN();

    zmq_init();

    zmq_socket_init(&pull, ZMQ_PULL);
    zmq_bind(&pull, 8888);

    zmq_socket_init(&push, ZMQ_PUSH);
    zmq_bind(&push, 9999);

    while(1) {
        if(etimer_expired(&et))
            etimer_set(&et, CLOCK_SECOND);

        PROCESS_WAIT_EVENT_UNTIL((ev == PROCESS_EVENT_TIMER) || (ev == zmq_socket_input_activity));

        if(ev == PROCESS_EVENT_TIMER) {
            printf("Publishing 'Hi there!'\r\n");
            static zmq_msg_t *msg = NULL;
            msg = zmq_msg_from_const_data(0, "Hi there!", 9);
            PT_WAIT_THREAD(process_pt, push.send(&push, msg));
            zmq_msg_destroy(&msg);
        }

        if(ev == zmq_socket_input_activity) {
            PT_WAIT_THREAD(process_pt, pull.recv_multipart(&pull, read_list));

            zmq_msg_t *msg = list_pop(read_list);
            while(msg != NULL) {
                printf("Received: ");
                uint8_t *data = zmq_msg_data(msg);
                uint8_t *pos = data;
                size_t size = zmq_msg_size(msg);
                while(pos < (data + size))
                    printf("%c", *pos++);
                printf("\r\n");

                zmq_msg_destroy(&msg);
                msg = list_pop(read_list);
            }
        }
    }

    PROCESS_END();
}
