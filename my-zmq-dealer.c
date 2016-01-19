#include "sys/autostart.h"
#include "zmq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PROCESS(test_dealer_bind, "Test process bind");
AUTOSTART_PROCESSES(&test_dealer_bind);

zmq_socket_t my_sock;
LIST(read_list);

PROCESS_THREAD(test_dealer_bind, ev, data) {
    PROCESS_BEGIN();

    list_init(read_list);

    zmq_init();
    zmq_socket_init(&my_sock, ZMQ_DEALER);
    // zmq_bind("tcp://*:9999");
    zmq_bind(&my_sock, 9999);

    while(1) {
        PROCESS_PAUSE();

        static zmq_msg_t *msg = NULL;
        msg = zmq_msg_from_const_data(0, "Hi there!", 9);
        PT_WAIT_THREAD(process_pt, my_sock.send(&my_sock, msg));
        zmq_msg_destroy(&msg);

        PT_WAIT_THREAD(process_pt, my_sock.recv_multipart(&my_sock, read_list));

        msg = list_pop(read_list);
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
