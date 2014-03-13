#ifndef PK2DEV
#define PK2DEV


#define ENDPOINT_IN 0x81
#define ENDPOINT_OUT 0x01
#define BUFFER_SIZE 64
#define TIMEOUT 5000

// PK2 commands
#define GET_STATUS        0xA2
#define EXECUTE_SCRIPT    0xA6
#define CLR_DOWNLOAD_BFR  0xA7
#define CLR_UPLOAD_BFR    0xA9
#define DOWNLOAD_DATA     0xA8
#define UPLOAD_DATA       0xAA
#define ENTER_UART_MODE   0xB3
#define EXIT_UART_MODE    0xB4
#define SET_VDD           0xA0
#define RESET             0xAE
#define END_OF_BUFFER     0xAD

// PK2 script instructions
#define VDD_GND_ON        0xFD
#define VDD_ON            0xFF
#define VDD_GND_OFF       0xFC
#define VDD_OFF           0xFE
#define MCLR_GND_ON       0xF7
#define MCLR_GND_OFF      0xF6
#define SET_ISP_PINS      0xF3

#define LOGL(level, ...)   { if (verbose >= level) fprintf(stderr, __VA_ARGS__); }
#define LOG(...)   { LOGL(1, __VA_ARGS__); }

#define LOG_ERROR(...) { fprintf(stderr, __VA_ARGS__); }
#define RAISE_ERROR(...) { LOG_ERROR(__VA_ARGS__); exit(1); }

#endif 
