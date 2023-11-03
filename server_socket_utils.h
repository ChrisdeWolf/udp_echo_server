/*
 *   Author: Christopher deWolf
 *   server_socket_utils.h -- utility functions used by server
 */

#ifndef SERVER_SOCKET_UTILS_H
#define SERVER_SOCKET_UTILS_H

#include "connection_structs.h"

extern void sendClientACK(int sockfd, struct sockaddr_storage their_addr,
                          socklen_t addr_len) {
    Packet ackPacket;
    ackPacket.ack = 1;
    if (sendto(sockfd, &ackPacket, sizeof(Packet), 0,
               (struct sockaddr *)&their_addr, addr_len) < 0) {
        perror("ACK sending failed");
    }
}

extern void sendClientNACK(int sockfd, struct sockaddr_storage their_addr,
                           socklen_t addr_len) {
    Packet nackPacket;
    nackPacket.nack = 1;
    if (sendto(sockfd, &nackPacket, sizeof(Packet), 0,
               (struct sockaddr *)&their_addr, addr_len) < 0) {
        perror("NACK sending failed");
    }
}

int serverWaitForACK(int sockfd, struct sockaddr *their_addr,
                     socklen_t addr_len) {
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
    ssize_t ack_length = recvfrom(sockfd, &receivedPacket, sizeof(Packet), 0,
                                  their_addr, &addr_len);

    if (ack_length < 0) {
        perror("ACK receive error");
        return -1;
    }

    if (receivedPacket.ack == 1) {
        return 1;  // ACK received
    } else if (receivedPacket.nack == 1) {
        printf("NACK recvd!\n");
        return 0;  // NACK received
    } else {
        printf("Received an unknown response\n");
        printf("%s\n", receivedPacket.buffer);
        return -1;
    }
}

#endif
