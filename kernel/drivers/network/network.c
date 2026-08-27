#include "network.h"
#include "e1000.h"

int network_e1000_transmit(const void *frame, uint16_t length) {
    ethernet_frame_view_t view;
    if (!frame || !ethernet_frame_parse(frame, length, &view)) return 0;
    return e1000_transmit(frame, length);
}

uint32_t network_e1000_poll(network_packet_queue_t *queue, uint32_t budget) {
    if (!queue || budget == 0 || e1000_controller_count() == 0) return 0;
    uint32_t processed = 0;
    (void)e1000_service();
    while (processed < budget) {
        uint8_t frame[ETHERNET_MAX_FRAME_SIZE];
        uint16_t length = 0;
        if (!e1000_receive(frame, sizeof(frame), &length)) break;
        if (network_packet_queue_push(queue, frame, length)) ++processed;
    }
    return processed;
}
