#include "sample_sim.h"
#include "simulith_uart.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Command definitions
#define CMD_NOOP  0x00
#define CMD_START 0x01
#define CMD_STOP  0x02
#define CMD_RESET 0x03

// Response definitions
#define RSP_COUNT 0x01
#define RSP_ERROR 0xFF

// Global state pointer for callback access
static sample_sim_state_t* g_state = NULL;

// UART port struct for Simulith
static uart_port_t g_uart_port = {0};

static void send_command_echo(sample_sim_state_t* state, const sample_cmd_t* cmd) {
    if (!state || !cmd) return;
    simulith_uart_send(&g_uart_port, (const uint8_t*)cmd, sizeof(sample_cmd_t));
}

static void send_housekeeping(sample_sim_state_t* state) {
    if (!state) return;
    uint8_t response[16];  // Header(2) + Data(12) + Trailer(2)
    response[0] = 0xDE;
    response[1] = 0xAD;
    response[2] = (state->cmd_counter >> 24) & 0xFF;
    response[3] = (state->cmd_counter >> 16) & 0xFF;
    response[4] = (state->cmd_counter >> 8) & 0xFF;
    response[5] = state->cmd_counter & 0xFF;
    response[6] = (state->config >> 24) & 0xFF;
    response[7] = (state->config >> 16) & 0xFF;
    response[8] = (state->config >> 8) & 0xFF;
    response[9] = state->config & 0xFF;
    response[10] = (state->status >> 24) & 0xFF;
    response[11] = (state->status >> 16) & 0xFF;
    response[12] = (state->status >> 8) & 0xFF;
    response[13] = state->status & 0xFF;
    response[14] = 0xBE;
    response[15] = 0xEF;
    simulith_uart_send(&g_uart_port, response, sizeof(response));
}

static void send_sample_data(sample_sim_state_t* state) {
    if (!state) return;
    uint8_t response[14];  // Header(2) + Counter(4) + Data(6) + Trailer(2)
    response[0] = 0xDE;
    response[1] = 0xAD;
    response[2] = (state->cmd_counter >> 24) & 0xFF;
    response[3] = (state->cmd_counter >> 16) & 0xFF;
    response[4] = (state->cmd_counter >> 8) & 0xFF;
    response[5] = state->cmd_counter & 0xFF;
    response[6] = (state->data_x >> 8) & 0xFF;
    response[7] = state->data_x & 0xFF;
    response[8] = (state->data_y >> 8) & 0xFF;
    response[9] = state->data_y & 0xFF;
    response[10] = (state->data_z >> 8) & 0xFF;
    response[11] = state->data_z & 0xFF;
    response[12] = 0xBE;
    response[13] = 0xEF;
    simulith_uart_send(&g_uart_port, response, sizeof(response));
}

static void handle_command(sample_sim_state_t* state, const uint8_t* data, size_t length) 
{
    printf("handle_command: Received command of length %zu\n", length);

    if (!state || !data || length < sizeof(sample_cmd_t)) 
    {  // Check for minimum command size
        printf("Invalid command parameters: state=%p, data=%p, length=%zu\n", 
               (void*)state, (const void*)data, length);
        if (state) state->status |= SAMPLE_SIM_ERROR;  // Set error bit if state exists
        return;
    }
    
    // Parse command fields from bytes (big-endian/network order)
    if (length < 9) {
        printf("Command too short to parse fields.\n");
        state->status |= SAMPLE_SIM_ERROR;
        return;
    }

    uint16_t header = ((uint16_t)data[0] << 8) | data[1];
    uint8_t cmd_id = data[2];
    uint32_t payload = ((uint32_t)data[3] << 24) |
                       ((uint32_t)data[4] << 16) |
                       ((uint32_t)data[5] << 8)  |
                       data[6];
    uint16_t trailer = ((uint16_t)data[7] << 8) | data[8];

    // Validate header (0xDEAD)
    if (header != HEADER_MARKER) {
        printf("Invalid command header (0x%04X)\n", header);
        state->status |= SAMPLE_SIM_ERROR;
        return;
    }

    // Validate trailer (0xBEEF)
    if (trailer != TRAILER_MARKER) {
        printf("Invalid command trailer (0x%04X)\n", trailer);
        state->status |= SAMPLE_SIM_ERROR;
        return;
    }

    // Echo command back
    printf("handle_command: Echo command back to UART: ID=%d, Payload=0x%08X\n", cmd_id, payload);
    simulith_uart_send(&g_uart_port, data, length);

    // Process command
    switch (cmd_id) {
        case CMD_NOOP:
            printf("Processing NOOP command\n");
            // Just echo the command back, which was already done
            break;

        case CMD_GET_HK:
            printf("Processing GET_HK command\n");
            send_housekeeping(state);
            break;

        case CMD_GET_SAMPLE:
            printf("Processing GET_SAMPLE command\n");
            send_sample_data(state);
            break;

        case CMD_SET_CONFIG:
            printf("Processing SET_CONFIG command with payload 0x%08X\n", payload);
            state->config = payload;
            break;

        default:
            printf("Unknown command ID: %d\n", cmd_id);
            state->status |= SAMPLE_SIM_ERROR;  // Set error bit
            break;
    }

    // Increment command counter
    state->cmd_counter++;
}

static void on_tick(uint64_t tick_time_ns) 
{
    int bytes;
    uint8_t data[256];

    if (!g_state) return;
    
    // Convert nanoseconds to seconds
    double current_time = tick_time_ns / 1e9;
    
    // Update sample data at the specified rate
    if (current_time - g_state->last_update_time >= (1.0 / SAMPLE_SIM_UPDATE_RATE_HZ)) 
    {
        // Update simulated data based on command counter
        g_state->data_x = (uint16_t)(g_state->cmd_counter * 1);
        g_state->data_y = (uint16_t)(g_state->cmd_counter * 2);
        g_state->data_z = (uint16_t)(g_state->cmd_counter * 3);
        g_state->last_update_time = current_time;
    }

    // Process UART
    bytes = simulith_uart_available(&g_uart_port);
    if (bytes > 0)
    {
        // Read UART
        bytes = simulith_uart_receive(&g_uart_port, data, sizeof(data));

        printf("Received %d bytes from UART\n", bytes);
        for(int i = 0; i < bytes; i++) 
        {
            printf("%02X ", data[i]);
        }
        printf("\n");

        // Process the command
        handle_command(g_state, data, bytes);
    }
}

int sample_sim_init(sample_sim_state_t* state) 
{
    if (!state) return SAMPLE_SIM_ERROR;

    // Initialize state
    memset(state, 0, sizeof(sample_sim_state_t));

    // Set global state pointer
    g_state = state;

    // Wait a bit for the Simulith server to start up
    sleep(1);

    // Initialize Simulith client
    if (simulith_client_init(PUB_ADDR, REP_ADDR, "sample_sim", INTERVAL_NS) != 0) 
    {
        printf("Failed to initialize Simulith client\n");
        return SAMPLE_SIM_ERROR;
    }

    // Handshake with Simulith server
    if (simulith_client_handshake() != 0) 
    {
        printf("Failed to handshake with Simulith server\n");
        simulith_client_shutdown();
        return SAMPLE_SIM_ERROR;
    }

    // Initialize UART port struct for Simulith (server/bind)
    memset(&g_uart_port, 0, sizeof(g_uart_port));
    snprintf(g_uart_port.name, sizeof(g_uart_port.name), "sample_sim_uart");
    snprintf(g_uart_port.address, sizeof(g_uart_port.address), "tcp://*:6005"); // Use port 6000 or as needed
    g_uart_port.is_server = 1; // Always server/bind for the simulator

    int uart_result = simulith_uart_init(&g_uart_port);
    if (uart_result < 0) 
    {
        printf("Failed to initialize Simulith UART server\n");
        simulith_client_shutdown();
        return SAMPLE_SIM_ERROR;
    }

    // Initialize time provider
    state->time_handle = simulith_time_init();
    if (!state->time_handle) 
    {
        printf("Failed to initialize time provider\n");
        simulith_uart_close(&g_uart_port);
        simulith_client_shutdown();
        return SAMPLE_SIM_ERROR;
    }

    // Initialize default values
    state->cmd_counter = 0;
    state->config = 0;
    state->status = SAMPLE_SIM_SUCCESS;  // Initialize status as success
    state->data_x = 0;
    state->data_y = 0;
    state->data_z = 0;
    state->last_update_time = simulith_time_get(state->time_handle);

    printf("Sample simulator initialized successfully as UART server on %s\n", g_uart_port.address);
    printf("Waiting for commands...\n");
    return SAMPLE_SIM_SUCCESS;
}

void sample_sim_cleanup(sample_sim_state_t* state) {
    if (!state) return;

    g_state = NULL;  // Clear global state pointer
    simulith_uart_close(&g_uart_port);

    if (state->time_handle) {
        simulith_time_cleanup(state->time_handle);
        state->time_handle = NULL;
    }

    simulith_client_shutdown();
}

int main(int argc, char* argv[]) {
    sample_sim_state_t state;
    
    if (sample_sim_init(&state) != SAMPLE_SIM_SUCCESS) {
        printf("Failed to initialize sample simulator\n");
        return 1;
    }
    
    printf("Sample simulator running. Press Ctrl+C to exit.\n");
    
    // Run the client loop with our tick callback
    simulith_client_run_loop(on_tick);
    
    sample_sim_cleanup(&state);
    return 0;
} 