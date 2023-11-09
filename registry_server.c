/*
 *  Author: Christopher deWolf
 *  registry_server.c -- server to register UDP services for UDP clients
 */

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "connection_structs.h"
#include "server_socket_utils.h"
#include "socket_utils.h"

/* initBroadcastPacket - initialize the registered_service data */
void initBroadcastPacket(BroadcastPacket *packet) {
    if (packet != NULL) {
        memset(packet->service_ip, 0, INET_ADDRSTRLEN);
        packet->service_port = -1;
        packet->register_service = -1;
        packet->receive_service = -1;
    }
}

/* registerService - registers a service with the registry server */
void registerService(BroadcastPacket *registered_service,
                     BroadcastPacket *packet) {
    if (registered_service != NULL && packet != NULL) {
        // copy service information from the packet to the registry
        strncpy(registered_service->service_ip, packet->service_ip,
                INET_ADDRSTRLEN);
        registered_service->service_port = packet->service_port;
        registered_service->register_service = packet->register_service;
        registered_service->receive_service = packet->receive_service;
    }
}

/*
 * sendRegisteredServiceInfo - send the registered service info to received
 * packet's address/port
 */
void sendRegisteredServiceInfo(BroadcastPacket *registered_service,
                               BroadcastPacket *packet) {
    int sockfd;
    struct sockaddr_in dest_addr;
    socklen_t addr_len = sizeof dest_addr;

    // initialize a UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("sendRegisteredServiceInfo: socket");
        return;
    }

    // setup to return data back to sender (client)
    memset(&dest_addr, 0, addr_len);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(packet->service_port);
    if (inet_pton(AF_INET, packet->service_ip, &dest_addr.sin_addr) <= 0) {
        perror("sendRegisteredServiceInfo: inet_pton");
        close(sockfd);
        return;
    }

    // send the registered service info to the destination
    if (sendto(sockfd, registered_service, sizeof(BroadcastPacket), 0,
               (struct sockaddr *)&dest_addr, addr_len) == -1) {
        perror("sendRegisteredServiceInfo: sendto");
    } else {
        printf("Sent registered service info to %s:%d\n", packet->service_ip,
               packet->service_port);
    }

    close(sockfd);
}

int main() {
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_storage their_addr;
    BroadcastPacket packet;
    socklen_t addr_len;
    BroadcastPacket registered_service;

    /* server initialization */
    initBroadcastPacket(&packet);

    /* socket configuration */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, REGISTRY_SERVER_PORT, &hints, &servinfo)) !=
        0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    /* loop through all the results and bind to the first we can */
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) ==
            -1) {
            perror("registry_server: socket");
            continue;
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("registry_server: bind");
            continue;
        }
        break;
    }
    if (p == NULL) {
        fprintf(stderr, "registry_server: failed to bind socket\n");
        return 2;
    }
    freeaddrinfo(servinfo);

    /* main registry_server event loop */
    printf("registry_server: waiting to recvfrom...\n");
    while (1) {
        addr_len = sizeof their_addr;
        if ((numbytes = recvfrom(sockfd, &packet, sizeof(BroadcastPacket), 0,
                                 (struct sockaddr *)&their_addr, &addr_len)) ==
            -1) {
            perror("recvfrom");
            exit(1);
        }
        printf(
            "Received Packet: service_ip=%s, service_port=%d, "
            "register_service=%d, receive_service=%d\n",
            packet.service_ip, packet.service_port, packet.register_service,
            packet.receive_service);

        // register a service if the register flag is set
        if (packet.register_service == 1) {
            registerService(&registered_service, &packet);
            printf("%s:%d registered with registry_server", packet.service_ip,
                   packet.service_port);
        }

        // send out the registered service info if receive flag is set
        if (packet.receive_service == 1) {
            sendRegisteredServiceInfo(&registered_service, &packet);
        }

        printf("\n");
    }
    close(sockfd);
    return 0;
}
