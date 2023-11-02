/*
** server.c -- a datagram sockets "server" demo
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

#define MYPORT "7777"  // the port users will be connecting to
// #define MAXBUFLEN 1024  // TODO: delete
int completed_files = 0;  // TODO: avoid globals
char concatedFilePath[256] = "./server_files/concatenated.txt";

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

void initializeFileBuffers(FileBuffer file_buffers[]) {
    for (int i = 0; i < MAX_FILES; i++) {
        file_buffers[i].next_expected_line_index = 0;
    }
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
    while (current_line < packet->file_size) {
        if (current_line == packet->line_index) {
            fprintf(file, "%s", packet->buffer);
        }
        current_line++;
    }

    if (packet->line_end_index >= packet->file_size - 1) {
        completed_files++;
        printf("End of file %d transmission!\n", packet->file_index);
        printf("completed_files=%d\n", completed_files);
    }

    fclose(file);
}

void concatFiles() {
    FILE *concatedFile = fopen(concatedFilePath, "w");
    if (concatedFile == NULL) {
        perror("Error opening concatenated file");
        return;
    }

    for (int i = 0; i < MAX_FILES; i++) {
        char filePath[256];
        snprintf(filePath, sizeof(filePath), "./server_files/quote%d.txt", i);

        FILE *individualFile = fopen(filePath, "r");
        if (individualFile == NULL) {
            perror("Error opening individual file");
            return;
        }
        char buffer[MAXBUFLEN];
        size_t bytesRead;
        while ((bytesRead = fread(buffer, 1, sizeof(buffer), individualFile)) >
               0) {
            fwrite(buffer, 1, bytesRead, concatedFile);
        }
        fclose(individualFile);
    }
    fclose(concatedFile);
}

void sendConcatedFile(int sockfd, struct sockaddr_storage their_addr,
                      socklen_t addr_len) {
    FILE *concatedFile = fopen(concatedFilePath, "r");
    if (concatedFile == NULL) {
        perror("Error opening concatenated file");
        return;
    }

    // packet setup
    Packet packet;
    packet.file_index = 0;
    packet.line_index = 0;
    // read entire file into the packet buffer
    size_t bytesRead = fread(packet.buffer, 1, MAXBUFLEN, concatedFile);
    // Set the file_size to the number of bytes read
    packet.file_size = (int)bytesRead;
    packet.checksum = getChecksum(packet.buffer);

    fclose(concatedFile);

    int retransmissions = 0;

    while (retransmissions <= MAX_RETRANSMISSIONS) {
        if (sendto(sockfd, &packet, sizeof(Packet), 0,
                   (struct sockaddr *)&their_addr, addr_len) == -1) {
            perror("Error sending packet to client");
            break;
        }

        // wait for ACK/NACK
        int ackResult =
            waitForACK(sockfd, (struct sockaddr *)&their_addr, addr_len);
        if (ackResult == 0) {
            printf("Timeout or NACK! Retransmitting...\n");
            retransmissions++;
        } else if (ackResult == -1) {
            printf("Error!\n");
            break;
        } else {
            printf("ACK received!\n");
            break;
        }
    }
}

void concatAndSendToClient(int sockfd, struct sockaddr_storage their_addr,
                           socklen_t addr_len) {
    concatFiles();
    sendConcatedFile(sockfd, their_addr, addr_len);
}

int main(void) {
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_storage their_addr;
    Packet packet;
    FileBuffer file_buffers[MAX_FILES];
    socklen_t addr_len;
    char s[INET6_ADDRSTRLEN];

    // server initialization
    clearServerFiles();
    initializeFileBuffers(file_buffers);

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
            perror("server: socket");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind socket\n");
        return 2;
    }

    freeaddrinfo(servinfo);

    printf("server: waiting to recvfrom...\n");
    while (1) {
        addr_len = sizeof their_addr;
        if ((numbytes = recvfrom(sockfd, &packet, sizeof(Packet), 0,
                                 (struct sockaddr *)&their_addr, &addr_len)) ==
            -1) {
            perror("recvfrom");
            exit(1);
        }

        printf("server: got packet from %s\n",
               inet_ntop(their_addr.ss_family,
                         get_in_addr((struct sockaddr *)&their_addr), s,
                         sizeof s));
        printf("server: packet is %d bytes long\n", numbytes);
        printf(
            "Received Packet: file_size: %d, file_index: %d, line_index: %d, "
            "line_end_index: %d, buffer = %s\n",
            packet.file_size, packet.file_index, packet.line_index,
            packet.line_end_index, packet.buffer);

        // Check for damaged data and ask for re-tx if needed
        if (isDamagedPacket(&packet)) {
            sendClientNACK(sockfd, their_addr, addr_len);
            continue;  // go back to listening
        }

        // Simulate a lost ACKs (~10% chance) TODO: remove once done
        if (rand() % 10 != 0) {
            // Send ACK back to the client
            sendClientACK(sockfd, their_addr, addr_len);

            FileBuffer *file_buffer = &file_buffers[packet.file_index];

            printf(
                "packet.line_index=%d, "
                "file_buffer->next_expected_line_index=%d\n",
                packet.line_index, file_buffer->next_expected_line_index);
            if (packet.line_index == file_buffer->next_expected_line_index) {
                // Packet is in order, write it to the file
                printf("packet is in order!\n");
                writeBufferToFile(&packet);

                // Increment next expected line index
                file_buffer->next_expected_line_index =
                    packet.line_end_index + 1;

                // see if any subsequent out-of-order packets can now be written
                while (file_buffer->next_expected_line_index < MAX_LINES) {
                    Packet *next_packet =
                        &file_buffer
                             ->buffer[file_buffer->next_expected_line_index];
                    if (next_packet->line_index ==
                        file_buffer->next_expected_line_index) {
                        // write to file and increment next expected line index
                        printf(
                            "found the next line in the buffer! line_index: %d",
                            next_packet->line_index);
                        writeBufferToFile(next_packet);
                        file_buffer->next_expected_line_index =
                            next_packet->line_end_index + 1;
                    } else {
                        break;  // Packet is still out-of-order
                    }
                }
            } else if (packet.line_index >
                       file_buffer->next_expected_line_index) {
                // Packet is out-of-order, store it in the file buffer
                printf("packet is out-of-order!\n");
                file_buffer->buffer[packet.line_index] = packet;
            }

            if (completed_files == MAX_FILES) {
                printf("All transmissions have been completed!\n");
                concatAndSendToClient(sockfd, their_addr, addr_len);

                break;
                // reset the server to receive again
                // completed_files =
                //     0;  // TODO: this doesn't seem to work on second try
                // clearServerFiles();
            }
        }
        printf("\n");
    }
    close(sockfd);
    return 0;
}
