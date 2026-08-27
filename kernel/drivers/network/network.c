#include "network.h"
#include "e1000.h"

int network_decode_frame(const void *frame, uint16_t length,
                         network_frame_view_t *view) {
    if (!frame || !view || !ethernet_frame_parse(frame, length,
                                                  &view->ethernet)) return 0;
    if (view->ethernet.ether_type == 0x0806U) {
        if (!arp_packet_parse(view->ethernet.payload,
                              view->ethernet.payload_length, &view->arp))
            return 0;
        view->kind = NETWORK_FRAME_ARP;
        return 1;
    }
    if (view->ethernet.ether_type != 0x0800U ||
        !ipv4_packet_parse(view->ethernet.payload,
                           view->ethernet.payload_length, &view->ipv4))
        return 0;
    if (view->ipv4.protocol == 17U) {
        if (!udp_packet_parse(view->ipv4.payload, view->ipv4.payload_length,
                              view->ipv4.source, view->ipv4.destination,
                              &view->udp)) return 0;
        view->kind = NETWORK_FRAME_UDP;
    } else if (view->ipv4.protocol == 1U) {
        if (!icmp_echo_parse(view->ipv4.payload, view->ipv4.payload_length,
                             &view->icmp)) return 0;
        view->kind = NETWORK_FRAME_ICMP;
    } else {
        view->kind = NETWORK_FRAME_IPV4;
    }
    return 1;
}

int network_deliver_frame(const void *frame, uint16_t length,
                          udp_endpoint_table_t *udp_table) {
    if (!udp_table) return 0;
    network_frame_view_t view;
    if (!network_decode_frame(frame, length, &view) ||
        view.kind != NETWORK_FRAME_UDP) return 0;
    return udp_endpoint_deliver(udp_table, view.ipv4.destination,
                                view.udp.destination_port,
                                view.ipv4.source, view.udp.source_port,
                                view.udp.payload, view.udp.payload_length);
}

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
