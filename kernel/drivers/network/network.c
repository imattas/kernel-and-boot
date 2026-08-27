#include "network.h"
#include "e1000.h"

static int prepare_network_frame(uint8_t *frame, uint16_t *length,
                                 ipv4_reassembly_table_t *reassembly,
                                 void *reassembly_output,
                                 uint16_t reassembly_capacity, uint64_t now) {
    ethernet_frame_view_t ethernet;
    ipv4_packet_view_t ipv4;
    if (!ethernet_frame_parse(frame, *length, &ethernet) ||
        ethernet.ether_type != 0x0800U ||
        !ipv4_packet_parse(ethernet.payload, ethernet.payload_length, &ipv4))
        return 1;
    if (ipv4.fragment_offset == 0 && !ipv4.more_fragments) return 1;
    if (!reassembly || !reassembly_output) return 0;
    uint16_t payload_length = 0;
    if (!ipv4_reassembly_add(reassembly, ipv4.identification, ipv4.source,
                             ipv4.destination, ipv4.protocol,
                             ipv4.fragment_offset, ipv4.more_fragments,
                             ipv4.payload, ipv4.payload_length, now, 1000,
                             reassembly_output, reassembly_capacity,
                             &payload_length)) return 0;
    uint8_t rebuilt_ip[ETHERNET_MAX_PAYLOAD_SIZE];
    uint8_t rebuilt_frame[ETHERNET_MAX_FRAME_SIZE];
    uint16_t rebuilt_ip_length = 0, rebuilt_frame_length = 0;
    if (!ipv4_packet_build(rebuilt_ip, sizeof(rebuilt_ip), ipv4.source,
                           ipv4.destination, ipv4.protocol, ipv4.ttl,
                           ipv4.identification, reassembly_output,
                           payload_length, &rebuilt_ip_length) ||
        !ethernet_frame_build(rebuilt_frame, sizeof(rebuilt_frame),
                              ethernet.destination, ethernet.source, 0x0800U,
                              rebuilt_ip, rebuilt_ip_length,
                              &rebuilt_frame_length)) return 0;
    for (uint16_t i = 0; i < rebuilt_frame_length; ++i) frame[i] = rebuilt_frame[i];
    *length = rebuilt_frame_length;
    return 1;
}

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

int network_build_icmp_echo_reply(const void *frame, uint16_t length,
                                  void *reply, uint16_t capacity,
                                  uint16_t *reply_length) {
    if (!frame || !reply || !reply_length) return 0;
    network_frame_view_t view;
    if (!network_decode_frame(frame, length, &view) ||
        view.kind != NETWORK_FRAME_ICMP ||
        view.icmp.type != ICMP_TYPE_ECHO_REQUEST)
        return 0;

    uint8_t icmp_packet[ICMP_ECHO_HEADER_SIZE + ETHERNET_MAX_PAYLOAD_SIZE];
    uint8_t ipv4_packet[ETHERNET_MAX_PAYLOAD_SIZE];
    uint16_t icmp_length = 0;
    uint16_t ipv4_length = 0;
    if (!icmp_echo_build(icmp_packet, sizeof(icmp_packet),
                         ICMP_TYPE_ECHO_REPLY, view.icmp.identifier,
                         view.icmp.sequence, view.icmp.payload,
                         view.icmp.payload_length, &icmp_length) ||
        !ipv4_packet_build(ipv4_packet, sizeof(ipv4_packet),
                           view.ipv4.destination, view.ipv4.source, 1, 64,
                           view.ipv4.identification, icmp_packet, icmp_length,
                           &ipv4_length) ||
        !ethernet_frame_build(reply, capacity, view.ethernet.source,
                              view.ethernet.destination, 0x0800,
                              ipv4_packet, ipv4_length, reply_length))
        return 0;
    return 1;
}

int network_build_arp_reply(const void *frame, uint16_t length,
                            const uint8_t local_hardware[ETHERNET_ADDRESS_SIZE],
                            const uint8_t local_protocol[4], void *reply,
                            uint16_t capacity, uint16_t *reply_length) {
    if (!frame || !local_hardware || !local_protocol || !reply ||
        !reply_length) return 0;
    network_frame_view_t view;
    if (!network_decode_frame(frame, length, &view) ||
        view.kind != NETWORK_FRAME_ARP ||
        view.arp.operation != ARP_OPERATION_REQUEST)
        return 0;
    for (uint32_t i = 0; i < ETHERNET_ADDRESS_SIZE; ++i)
        if (view.ethernet.source[i] != view.arp.sender_hardware[i]) return 0;
    for (uint32_t i = 0; i < 4; ++i)
        if (view.arp.target_protocol[i] != local_protocol[i]) return 0;

    uint8_t packet[ARP_PACKET_SIZE];
    uint16_t packet_length = 0;
    if (!arp_packet_build(packet, sizeof(packet), ARP_OPERATION_REPLY,
                          local_hardware, local_protocol,
                          view.arp.sender_hardware, view.arp.sender_protocol,
                          &packet_length) ||
        !ethernet_frame_build(reply, capacity, view.arp.sender_hardware,
                              local_hardware, 0x0806U, packet,
                              packet_length, reply_length)) return 0;
    return 1;
}

uint32_t network_service(network_packet_queue_t *queue,
                         const uint8_t local_hardware[ETHERNET_ADDRESS_SIZE],
                         const uint8_t local_protocol[4], arp_cache_t *cache,
                         udp_endpoint_table_t *udp_table, uint64_t now,
                         uint32_t budget, ipv4_reassembly_table_t *reassembly,
                         void *reassembly_output,
                         uint16_t reassembly_capacity) {
    if (!queue || !local_hardware || !local_protocol || !cache ||
        budget == 0) return 0;
    (void)network_e1000_poll(queue, budget);
    uint32_t serviced = 0;
    while (serviced < budget) {
        uint8_t frame[ETHERNET_MAX_FRAME_SIZE];
        uint16_t length = 0;
        if (!network_packet_queue_pop(queue, frame, sizeof(frame), &length))
            break;
        network_frame_view_t view;
        if (!prepare_network_frame(frame, &length, reassembly,
                                   reassembly_output, reassembly_capacity, now) ||
            !network_decode_frame(frame, length, &view)) {
            ++serviced;
            continue;
        }
        uint8_t reply[ETHERNET_MAX_FRAME_SIZE];
        uint16_t reply_length = 0;
        if (view.kind == NETWORK_FRAME_ARP) {
            (void)arp_cache_update(cache, view.arp.sender_protocol,
                                   view.arp.sender_hardware, now);
            if (network_build_arp_reply(frame, length, local_hardware,
                                        local_protocol, reply, sizeof(reply),
                                        &reply_length))
                (void)network_e1000_transmit(reply, reply_length);
        } else if (view.kind == NETWORK_FRAME_ICMP &&
                   view.icmp.type == ICMP_TYPE_ECHO_REQUEST &&
                   network_build_icmp_echo_reply(frame, length, reply,
                                                 sizeof(reply), &reply_length)) {
            (void)network_e1000_transmit(reply, reply_length);
        } else if (view.kind == NETWORK_FRAME_UDP && udp_table) {
            (void)udp_endpoint_deliver(udp_table, view.ipv4.destination,
                                       view.udp.destination_port,
                                       view.ipv4.source,
                                       view.udp.source_port,
                                       view.udp.payload,
                                       view.udp.payload_length);
        }
        ++serviced;
    }
    return serviced;
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
