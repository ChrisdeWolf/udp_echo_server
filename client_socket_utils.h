/*
 *   Author: Christopher deWolf
 *   client_socket_utils.h -- utility functions used by clients
 */

#ifndef CLIENT_SOCKET_UTILS_H
#define CLIENT_SOCKET_UTILS_H

#include "connection_structs.h"

/* clientPrintUsage - client user options and help */
extern void clientPrintUsage() {
    printf(
        "usage: ./client SERVER_IP [options]\nor\n"
        "       ./client --enable-service-discovery [options]\n");
    printf("Options:\n");
    printf(
        "  --simulate-unordered-packets: Enables unordered packet delivery "
        "simulation\n");
    printf(
        "  --enable-service-discovery: Enables automatic service discovery, "
        "SERVER_IP does not need to be provided\n");
    printf("  -h, --help: Display this help message\n");
}

/*
 * sendPacket - uses sendTo to send data via socket connection
 *  returns -1 - failure
 *  returns positive int - success
 */
int sendPacket(int sockfd, struct addrinfo *p, Packet *packet) {
    int numbytes;
    if ((numbytes = sendto(sockfd, packet, sizeof(Packet), 0, p->ai_addr,
                           p->ai_addrlen)) == -1) {
        perror("sendPacket error:");
        return -1;
    }
    printf(
        "packet->file_size: %d, packet->file_index: %d, packet->line_index: "
        "%d, line_end_index: %d\n",
        packet->file_size, packet->file_index, packet->line_index,
        packet->line_end_index);
    return numbytes;
}

/*
 *  waitForACK - listens on a timeout for an ACK/NACK from the server
 *  returns -1 - failure
 *  returns  0 - timeout or NACK
 *  returns  1 - ACK
 */
int waitForACK(int sockfd, struct addrinfo *p) {
    fd_set read_fds;
    struct timeval timeout;

    // setup timeout timer
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = 0;
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);

    // use of select here for the async timer
    // https://beej.us/guide/bgnet/html/split/slightly-advanced-techniques.html#select
    int ready = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
    if (ready < 0) {
        perror("Select failed");
        return -1;
    } else if (ready == 0) {
        return 0;  // TIMED-OUT!
    }

    // check for a returned ACK or NACK
    Packet receivedPacket;
    socklen_t addr_len = sizeof(p);
    ssize_t ack_length = recvfrom(sockfd, &receivedPacket, sizeof(Packet), 0,
                                  (struct sockaddr *)&p, &addr_len);
    if (ack_length < 0) {
        perror("ACK receive error");
        return -1;
    }

    if (receivedPacket.ack == 1) {
        return 1;  // ACK received
    } else if (receivedPacket.nack == 1) {
        return 0;  // NACK received
    } else {
        printf("Received an unknown response\n");
        printf("%s\n", receivedPacket.buffer);
        return -1;
    }
}

/* sendAndWaitForACK - sends a Packet, handling retransmissions if required */
extern int sendAndWaitForACK(int sockfd, struct addrinfo *p, Packet *packet) {
    int retransmissions = 0;

    while (retransmissions <= MAX_RETRANSMISSIONS) {
        int result = sendPacket(sockfd, p, packet);
        if (result == -1) {
            printf("Error occurred while sending the packet.\n");
            return -1;
        } else {
            // Wait for ACK/NACK
            int ackResult = waitForACK(sockfd, p);
            if (ackResult == 0) {
                printf("Timeout or NACK! Retransmitting...\n");
                retransmissions++;
            } else if (ackResult == -1) {
                printf("Error waiting for ACK/NACK!\n");
                return -1;
            } else {
                printf("ACK received!\n");
                break;
            }
        }
    }

    if (retransmissions > MAX_RETRANSMISSIONS) {
        printf("Max retransmissions reached for the packet. Aborting.\n");
        return 0;
    }

    return 1;  // Packet sent and ACK received
}

/* sendServerACK - send an acknowledgement (ACK) to the server */
extern void sendServerACK(int sockfd, struct addrinfo *p) {
    Packet ackPacket;
    ackPacket.ack = 1;
    ackPacket.nack = 0;
    if (sendPacket(sockfd, p, &ackPacket) < 0) {
        perror("ACK sending failed");
    }
}

/* sendServerNACK - send a negative-acknowledgement (NACK) to the server */
extern void sendServerNACK(int sockfd, struct addrinfo *p) {
    Packet nackPacket;
    nackPacket.ack = 0;
    nackPacket.nack = 1;
    if (sendPacket(sockfd, p, &nackPacket) < 0) {
        perror("NACK sending failed");
    }
}

#endif
