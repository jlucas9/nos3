#ifndef SAMPLE_SIM_H
#define SAMPLE_SIM_H

#include <stdint.h>
#include "simulith.h"

// Status codes
#define SAMPLE_SIM_SUCCESS 0
#define SAMPLE_SIM_ERROR  1

// Protocol constants
#define SAMPLE_SIM_UART_ID 5  // Port ID (0-7)
#define SAMPLE_SIM_UPDATE_RATE_HZ 10
#define HEADER_MARKER 0xDEAD
#define TRAILER_MARKER 0xBEEF

// Command IDs
#define CMD_NOOP 0
#define CMD_GET_HK 1
#define CMD_GET_SAMPLE 2
#define CMD_SET_CONFIG 3

// Command structure
typedef struct {
    uint16_t header;     // 0xDEAD
    uint8_t cmd_id;      // Command identifier
    uint32_t payload;    // Command payload (used for set config)
    uint16_t trailer;    // 0xBEEF
} __attribute__((packed)) sample_cmd_t;

// Housekeeping response structure
typedef struct {
    uint16_t header;     // 0xDEAD
    uint32_t cmd_counter;// Command counter
    uint32_t config;     // Current configuration
    uint32_t status;     // Component status
    uint16_t trailer;    // 0xBEEF
} __attribute__((packed)) sample_hk_resp_t;

// Sample data response structure
typedef struct {
    uint16_t header;     // 0xDEAD
    uint32_t cmd_counter;// Command counter
    uint16_t data_x;     // X component
    uint16_t data_y;     // Y component
    uint16_t data_z;     // Z component
    uint16_t trailer;    // 0xBEEF
} __attribute__((packed)) sample_data_resp_t;

// Sample simulator state structure
typedef struct {
    // Communication handles
    uint8_t uart_port;
    uint32_t uart_handle;
    void* time_handle;

    // Simulator state
    uint32_t cmd_counter;
    uint32_t config;
    uint32_t status;
    uint16_t data_x;
    uint16_t data_y;
    uint16_t data_z;
    double last_update_time;
} sample_sim_state_t;

// Function declarations
int sample_sim_init(sample_sim_state_t* state);
void sample_sim_cleanup(sample_sim_state_t* state);

#endif /* SAMPLE_SIM_H */ 