#include "sys/autostart.h"
#include "zmq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PROCESS(test_pub_bind, "Test process bind");
AUTOSTART_PROCESSES(&test_pub_bind);

zmq_socket_t my_sock;
struct etimer et;

PROCESS_THREAD(test_pub_bind, ev, data) {
    // printf("> test_pub_bind %d, %d, %p\n", process_pt->lc, ev, data);
    // print_event_name(ev);
    // printf("\n");
    PROCESS_BEGIN();

    zmq_init();
    zmq_socket_init(&my_sock, ZMQ_PUB);
    // zmq_bind("tcp://*:9999");
    zmq_bind(&my_sock, 9999);

    while(1) {
        if(etimer_expired(&et))
            etimer_set(&et, CLOCK_SECOND);

        PROCESS_WAIT_EVENT();

        if(ev == PROCESS_EVENT_TIMER) {
            printf("Publishing 'Hi there!'\r\n");
            static zmq_msg_t *msg = NULL;
            msg = zmq_msg_from_const_data(0, "Hi there!", 9);
            PT_WAIT_THREAD(process_pt, my_sock.send(&my_sock, msg));
            zmq_msg_destroy(&msg);
        }
    }

    PROCESS_END();
}
