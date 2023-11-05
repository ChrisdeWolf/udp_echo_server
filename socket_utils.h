/*
 *   Author: Christopher deWolf
 *   socket_utils.h -- shared utility functions used by both clients/server
 */

#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H

#include "connection_structs.h"

/*
 * getChecksum - computes a rudimentary checksum on the data
 */
extern unsigned short getChecksum(const char *data) {
    unsigned int sum = 0;
    int i;
    for (i = 0; i < MAXBUFLEN; i++) {
        sum += (unsigned char)data[i];
    }
    return (unsigned short)(sum & 0xFFFF);
}

/*
 * isDamagedPacket - detects if Packet data is damaged by running a checksum and
 * comparing it to the Packet.checksum.
 * returns 1 - damaged packet
 * returns 0 - undamaged packet
 */
extern int isDamagedPacket(Packet *packet) {
    unsigned short computedChecksum = getChecksum(packet->buffer);
    printf("packet checksum=%d, server checksum=%d\n", packet->checksum,
           computedChecksum);

    if (computedChecksum != packet->checksum) {
        printf("Received packet with invalid checksum. Sending NACK.\n");
        return 1;  // request retransmission
    }

    if (packet->file_index < 0 || packet->file_index >= MAX_FILES ||
        packet->line_index < 0) {
        printf("Invalid/out-of-range packet. Sending NACK.\n");
        return 1;  // request retransmission
    }

    return 0;  // no retransmission required
}

#endif
