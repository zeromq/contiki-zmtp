#include "sys/autostart.h"
#include "zmq.h"

#include "sub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PROCESS(test_sub_bind, "Test sub bind");
AUTOSTART_PROCESSES(&test_sub_bind);

zmq_socket_t my_sock;
LIST(read_list);

PROCESS_THREAD(test_sub_bind, ev, data) {
    // printf("> test_sub_bind %d, %d, %p\n", process_pt->lc, ev, data);
    // print_event_name(ev);
    // printf("\r\n");
    PROCESS_BEGIN();

    list_init(read_list);

    zmq_init();
    zmq_socket_init(&my_sock, ZMQ_SUB);
    // zmq_bind("tcp://*:9999");
    zmq_bind(&my_sock, 9999);

    PROCESS_WAIT_EVENT(); // Wait for a publisher to connect
    // This is actually a defect of the exemple (and of the subscribe implementation)
    // because the subscribe message will only be sent to the first publisher connected.

    printf("Doing subscription\r\n");
    PT_WAIT_THREAD(process_pt, zmq_sub_subscribe(&my_sock, "Hi"));

    while(1) {
        PROCESS_WAIT_EVENT();

        PT_WAIT_THREAD(process_pt, my_sock.recv_multipart(&my_sock, read_list));

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

    PROCESS_END();
}
