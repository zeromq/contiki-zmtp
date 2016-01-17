#include "sys/autostart.h"

#include "router.h"

#define SERVER_PORT 9999


PROCESS(test_router_bind, "Test process bind");
AUTOSTART_PROCESSES(&test_router_bind);

zmtp_channel_t my_chan;

PROCESS_THREAD(test_router_bind, ev, data) {
    PROCESS_BEGIN();

    zmtp_init();

    zmtp_channel_init(&my_chan, ZMQ_ROUTER);
    zmtp_listen(&my_chan, SERVER_PORT);

    while(1) {
        PROCESS_PAUSE();
    }

    PROCESS_END();
}
