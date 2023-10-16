typedef struct {
    int file_size;
    int file_index;
    int line_index;
    char buffer[1024];  // TODO: use MAXBUFLENGTH
} Packet;

// typedef struct
// {
//     // PacketHeader header;
//     PacketData data;
// } Packet;

typedef struct {
    // int syn_sent;
    int initialized;
    int finished;
    int file_size;
    int file_index;
    int line_index;
} Connection;

// typedef struct
// {
//     int connections[10]
// } Connections;