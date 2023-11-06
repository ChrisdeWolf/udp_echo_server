/*
 *  Author: Christopher deWolf
 *  server.c -- a datagram sockets "server"
 *      Reliably processes file data and concatenates all files to
 *      return back to sender.
 *      Adapted from "Beej's Guide to Network Programming" (C) 2017
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

int completed_files = 0;
char concatedFilePath[256] = "./server_files/concatenated.txt";

/* clearServerFiles - clears out files on the server */
void clearServerFiles() {
    const char *directory_path = "./server_files";
    DIR *dir = opendir(directory_path);

    if (dir == NULL) {
        perror("Error opening directory");
        return;
    }

    // loop through all files in the directory and delete them
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

/* initializeFileBuffers - initializes all the file buffers */
void initializeFileBuffers(FileBuffer file_buffers[]) {
    for (int i = 0; i < MAX_FILES; i++) {
        file_buffers[i].next_expected_line_index = 0;
        memset(file_buffers[i].buffer, 0, sizeof(file_buffers[i].buffer));
    }
}

/* get_in_addr - get sockaddr, IPv4 or IPv6 */
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

/*
 * writeBufferToFile - writes a received data packet to a local server file.
 * File will be created if one doesn't exist already. Will detect if packet
 * contains last line in the file transmission.
 */
void writeBufferToFile(Packet *packet) {
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "./server_files/quote%d.txt",
             packet->file_index);

    // open the file for appending (or create if it doesn't exist)
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

    // detect if last packet for the specific file transmission
    if (packet->line_end_index >= packet->file_size - 1) {
        completed_files++;
        printf("End of file %d transmission!\n", packet->file_index);
        printf("completed_files=%d\n", completed_files);
    }

    fclose(file);
}

/* concatFiles - appends all received files together into concatenated.txt */
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

/*
 * sendConcatedFile - reliably sends concatenated.txt back to the client.
 * Handles reliable transmission by issueing a timeout to await ACKs/NACKs.
 */
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
            serverWaitForACK(sockfd, (struct sockaddr *)&their_addr, addr_len);
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

/* concatAndSendToClient - create and send concatenated.txt to the client */
void concatAndSendToClient(int sockfd, struct sockaddr_storage their_addr,
                           socklen_t addr_len) {
    concatFiles();
    sendConcatedFile(sockfd, their_addr, addr_len);
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_storage their_addr;
    Packet packet;
    FileBuffer file_buffers[MAX_FILES];
    socklen_t addr_len;
    char s[INET6_ADDRSTRLEN];
    int reset_env = 0;

    /* server initialization */
    clearServerFiles();
    initializeFileBuffers(file_buffers);

    /* handle help and opts */
    int simulateLostPackets = 0;
    int simulateDamagedPackets = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            serverPrintUsage();
            exit(0);
        } else if (strcmp(argv[i], "--simulate-lost-packets") == 0) {
            simulateLostPackets = 1;
        } else if (strcmp(argv[i], "--simulate-damaged-packets") == 0) {
            simulateDamagedPackets = 1;
        }
    }
    printf("simulateLostPackets: %d, simulateDamagedPackets: %d\n",
           simulateLostPackets, simulateDamagedPackets);

    /* socket configuration */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6;  // set to AF_INET to use IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;  // use my IP
    if ((rv = getaddrinfo(NULL, SERVERPORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    /* loop through all the results and bind to the first we can */
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

    /* main server event loop */
    printf("server: waiting to recvfrom...\n");
    while (1) {
        addr_len = sizeof their_addr;
        if ((numbytes = recvfrom(sockfd, &packet, sizeof(Packet), 0,
                                 (struct sockaddr *)&their_addr, &addr_len)) ==
            -1) {
            perror("recvfrom");
            exit(1);
        }

        // handle re-initialization between client-server transmissions cycles
        if (reset_env == 1) {
            clearServerFiles();
            initializeFileBuffers(file_buffers);
            reset_env = 0;
        }

        printf("server: got packet from %s\n",
               inet_ntop(their_addr.ss_family,
                         get_in_addr((struct sockaddr *)&their_addr), s,
                         sizeof s));
        printf(
            "Received Packet: file_size: %d, file_index: %d, line_index: %d, "
            "line_end_index: %d\n",
            packet.file_size, packet.file_index, packet.line_index,
            packet.line_end_index);

        /* Check for damaged data (or simulate) and ask for re-tx if needed */
        if (isDamagedPacket(&packet) ||
            (simulateLostPackets && (rand() % 10) == 0)) {
            printf("Damaged Packet, sending NACK...\n");
            sendClientNACK(sockfd, their_addr, addr_len);
            continue;  // go back to listening
        }

        // simulate a lost ACK with a 10% chance (if flag was set)
        if (simulateLostPackets && (rand() % 10) == 0) {
            printf("Simulated lost ACK, skipping...\n");
            continue;  // Skip this packet
        }

        /* Send ACK back to the client */
        sendClientACK(sockfd, their_addr, addr_len);

        // get the received files out-of-order buffer
        FileBuffer *file_buffer = &file_buffers[packet.file_index];

        // check if the receieved line index is in expected order
        if (packet.line_index == file_buffer->next_expected_line_index) {
            // packet is in order, write it to the file
            writeBufferToFile(&packet);

            // increment next expected line index
            file_buffer->next_expected_line_index = packet.line_end_index + 1;

            // see if any subsequent out-of-order packets can now be written
            while (file_buffer->next_expected_line_index < MAX_LINES) {
                Packet *next_packet =
                    &file_buffer->buffer[file_buffer->next_expected_line_index];
                if (next_packet->line_index ==
                    file_buffer->next_expected_line_index) {
                    // write to file and increment next expected line index
                    writeBufferToFile(next_packet);
                    file_buffer->next_expected_line_index =
                        next_packet->line_end_index + 1;
                } else {
                    break;  // Packet is still out-of-order
                }
            }
        } else if (packet.line_index > file_buffer->next_expected_line_index) {
            // Packet is out-of-order, store it in the file buffer
            printf("packet is out-of-order!\n");
            file_buffer->buffer[packet.line_index] = packet;
        }

        if (completed_files == MAX_FILES) {
            printf("All transmissions have been completed!\n");
            concatAndSendToClient(sockfd, their_addr, addr_len);

            // reset the server to receive again
            completed_files = 0;
            reset_env = 1;
        }
        printf("\n");
    }
    close(sockfd);
    return 0;
}
