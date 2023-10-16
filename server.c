/*
** listener.c -- a datagram sockets "server" demo
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

#define MYPORT "7777"  // the port users will be connecting to

#define MAXBUFLEN 1024

void clearServerFiles() {
    const char *directory_path = "./server_files";
    DIR *dir = opendir(directory_path);

    if (dir == NULL) {
        perror("Error opening directory");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            char filepath[256];
            snprintf(filepath, sizeof(filepath), "%s/%s", directory_path,
                     entry->d_name);

            if (remove(filepath) == 0) {
                printf("Deleted: %s\n", filepath);
            } else {
                perror("Error deleting file");
            }
        }
    }
    closedir(dir);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

void writeBufferToFile(Packet *packet) {
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "./server_files/quote%d.txt",
             packet->file_index);

    // Open the file for appending (or create if it doesn't exist)
    FILE *file = fopen(file_path, "a+");  // "a+" mode for append and create

    if (file == NULL) {
        perror("Error opening file");
        return;
    }

    // Write to the specified line_index
    int current_line = 0;
    // char *token = strtok(packet->buffer, "\n");

    // while (token != NULL) {
    while (current_line < packet->file_size) {
        printf("current=%d, line_index=%d\n", current_line, packet->line_index);
        if (current_line == packet->line_index) {
            // fprintf(file, "%s\n", token);
            fprintf(file, "%s", packet->buffer);
        }
        current_line++;
        // token = strtok(NULL, "\n");
    }

    fclose(file);
}

int main(void) {
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_storage their_addr;
    // char buf[MAXBUFLEN];
    Packet packet;
    socklen_t addr_len;
    char s[INET6_ADDRSTRLEN];

    clearServerFiles();

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6;  // set to AF_INET to use IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;  // use my IP

    if ((rv = getaddrinfo(NULL, MYPORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) ==
            -1) {
            perror("listener: socket");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("listener: bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "listener: failed to bind socket\n");
        return 2;
    }

    freeaddrinfo(servinfo);

    printf("listener: waiting to recvfrom...\n");

    while (1) {
        addr_len = sizeof their_addr;
        if ((numbytes = recvfrom(sockfd, &packet, sizeof(Packet), 0,
                                 (struct sockaddr *)&their_addr, &addr_len)) ==
            -1) {
            perror("recvfrom");
            exit(1);
        }

        printf("listener: got packet from %s\n",
               inet_ntop(their_addr.ss_family,
                         get_in_addr((struct sockaddr *)&their_addr), s,
                         sizeof s));
        printf("listener: packet is %d bytes long\n", numbytes);
        printf("Received Packet: file_size = %d\n", packet.file_size);
        printf("Received Packet: file_index = %d\n", packet.file_index);
        printf("Received Packet: line_index = %d\n", packet.line_index);
        printf("Received Packet: buffer = %s\n", packet.buffer);

        writeBufferToFile(&packet);
    }
    close(sockfd);

    return 0;
}
