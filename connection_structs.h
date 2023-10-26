typedef struct {
    int file_size;      // total lines in the file
    int file_index;     // file index 0-9 (effectively the filename)
    int line_index;     // current line index 0-file_size-1
    char buffer[1024];  // data for that line
} Packet;

typedef struct {
    int initialized;  // bool - is connection initialized
    int finished;     // bool - is connection finished
    int file_size;    // total lines in the file
    int file_index;   // file index 0-9 (effectively the filename)
    int line_index;   // current line index 0-file_size-1
} Connection;

// typedef struct
// {
//     int connections[10]
// } Connections;