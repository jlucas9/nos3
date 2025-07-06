#include "sample_sim.h"
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

// Buffer for receiving partial commands
static struct {
    uint8_t buffer[256];
    size_t bytes_received;
} rx_buffer = {0};

static void send_command_echo(sample_sim_state_t* state, const sample_cmd_t* cmd) {
    if (!state || !cmd) return;
    simulith_uart_send(state->uart_port, (const uint8_t*)cmd, sizeof(sample_cmd_t));
}

static void send_housekeeping(sample_sim_state_t* state) {
    if (!state) return;
    
    uint8_t response[16];  // Header(2) + Data(12) + Trailer(2)
    
    // Header
    response[0] = 0xDE;
    response[1] = 0xAD;
    
    // Command counter (big endian)
    response[2] = (state->cmd_counter >> 24) & 0xFF;
    response[3] = (state->cmd_counter >> 16) & 0xFF;
    response[4] = (state->cmd_counter >> 8) & 0xFF;
    response[5] = state->cmd_counter & 0xFF;
    
    // Configuration (big endian)
    response[6] = (state->config >> 24) & 0xFF;
    response[7] = (state->config >> 16) & 0xFF;
    response[8] = (state->config >> 8) & 0xFF;
    response[9] = state->config & 0xFF;
    
    // Status (big endian)
    response[10] = (state->status >> 24) & 0xFF;
    response[11] = (state->status >> 16) & 0xFF;
    response[12] = (state->status >> 8) & 0xFF;
    response[13] = state->status & 0xFF;
    
    // Trailer
    response[14] = 0xBE;
    response[15] = 0xEF;
    
    simulith_uart_send(state->uart_port, response, sizeof(response));
}

static void send_sample_data(sample_sim_state_t* state) {
    if (!state) return;
    
    uint8_t response[14];  // Header(2) + Counter(4) + Data(6) + Trailer(2)
    
    // Header
    response[0] = 0xDE;
    response[1] = 0xAD;
    
    // Command counter (big endian)
    response[2] = (state->cmd_counter >> 24) & 0xFF;
    response[3] = (state->cmd_counter >> 16) & 0xFF;
    response[4] = (state->cmd_counter >> 8) & 0xFF;
    response[5] = state->cmd_counter & 0xFF;
    
    // Data X (big endian)
    response[6] = (state->data_x >> 8) & 0xFF;
    response[7] = state->data_x & 0xFF;
    
    // Data Y (big endian)
    response[8] = (state->data_y >> 8) & 0xFF;
    response[9] = state->data_y & 0xFF;
    
    // Data Z (big endian)
    response[10] = (state->data_z >> 8) & 0xFF;
    response[11] = state->data_z & 0xFF;
    
    // Trailer
    response[12] = 0xBE;
    response[13] = 0xEF;
    
    simulith_uart_send(state->uart_port, response, sizeof(response));
}

static void handle_command(sample_sim_state_t* state, const uint8_t* data, size_t length) {
    if (!state || !data || length < sizeof(sample_cmd_t)) {  // Check for minimum command size
        printf("Invalid command parameters: state=%p, data=%p, length=%zu\n", 
               (void*)state, (const void*)data, length);
        if (state) state->status |= SAMPLE_SIM_ERROR;  // Set error bit if state exists
        return;
    }
    
    // Cast data to command structure for easier access
    const sample_cmd_t* cmd = (const sample_cmd_t*)data;
    
    // Validate header (0xDEAD)
    if (cmd->header != HEADER_MARKER) {
        printf("Invalid command header (0x%04X)\n", cmd->header);
        state->status |= SAMPLE_SIM_ERROR;
        return;
    }
    
    // Validate trailer (0xBEEF)
    if (cmd->trailer != TRAILER_MARKER) {
        printf("Invalid command trailer (0x%04X)\n", cmd->trailer);
        state->status |= SAMPLE_SIM_ERROR;
        return;
    }
    
    // Echo command back
    simulith_uart_send(state->uart_port, data, length);
    
    // Process command
    switch (cmd->cmd_id) {
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
            printf("Processing SET_CONFIG command with payload 0x%08X\n", cmd->payload);
            state->config = cmd->payload;
            break;
            
        default:
            printf("Unknown command ID: %d\n", cmd->cmd_id);
            state->status |= SAMPLE_SIM_ERROR;  // Set error bit
            break;
    }
    
    // Increment command counter
    state->cmd_counter++;
}

static int uart_rx_callback(uint8_t port_id, const uint8_t* data, size_t len) {
    printf("UART RX Callback - Port: %d, Length: %zu\n", port_id, len);
    
    if (!g_state || port_id != g_state->uart_port || !data || len == 0) {
        printf("UART RX Error - Invalid parameters (state: %p, expected port: %d)\n", 
               (void*)g_state, g_state ? g_state->uart_port : -1);
        return -1;
    }

    // Add new data to our buffer
    size_t space_available = sizeof(rx_buffer.buffer) - rx_buffer.bytes_received;
    size_t bytes_to_copy = (len < space_available) ? len : space_available;
    
    printf("UART RX Data [%zu bytes]: ", bytes_to_copy);
    for (size_t i = 0; i < bytes_to_copy; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
    
    memcpy(rx_buffer.buffer + rx_buffer.bytes_received, data, bytes_to_copy);
    rx_buffer.bytes_received += bytes_to_copy;

    // Process complete commands
    while (rx_buffer.bytes_received >= sizeof(sample_cmd_t)) {
        // Check if we have a complete command by looking for header and trailer
        sample_cmd_t* cmd = (sample_cmd_t*)rx_buffer.buffer;
        printf("Processing potential command - Header: %04X, Trailer: %04X\n", 
               cmd->header, cmd->trailer);
               
        if (cmd->header == HEADER_MARKER && cmd->trailer == TRAILER_MARKER) {
            // Process the command
            handle_command(g_state, rx_buffer.buffer, sizeof(sample_cmd_t));
            
            // Remove the processed command from buffer
            if (rx_buffer.bytes_received > sizeof(sample_cmd_t)) {
                memmove(rx_buffer.buffer, 
                       rx_buffer.buffer + sizeof(sample_cmd_t),
                       rx_buffer.bytes_received - sizeof(sample_cmd_t));
            }
            rx_buffer.bytes_received -= sizeof(sample_cmd_t);
        } else {
            // Invalid command format, remove one byte and try again
            printf("Invalid command format - Header: %04X, Trailer: %04X\n", 
                   cmd->header, cmd->trailer);
            memmove(rx_buffer.buffer, rx_buffer.buffer + 1, rx_buffer.bytes_received - 1);
            rx_buffer.bytes_received--;
        }
    }

    return len;  // Return number of bytes processed
}

static void on_tick(uint64_t tick_time_ns) 
{
    if (!g_state) return;
    
    // Convert nanoseconds to seconds
    double current_time = tick_time_ns / 1e9;
    
    // Update sample data at the specified rate
    if (current_time - g_state->last_update_time >= (1.0 / SAMPLE_SIM_UPDATE_RATE_HZ)) 
    {
        // Update simulated data based on command counter
        g_state->data_x = (uint16_t)(g_state->cmd_counter * 0.001);
        g_state->data_y = (uint16_t)(g_state->cmd_counter * 0.002);
        g_state->data_z = (uint16_t)(g_state->cmd_counter * 0.003);
        g_state->last_update_time = current_time;
    }
}

int sample_sim_init(sample_sim_state_t* state) 
{
    if (!state) return SAMPLE_SIM_ERROR;
    
    // Initialize state
    memset(state, 0, sizeof(sample_sim_state_t));
    
    // Set global state pointer
    g_state = state;
    
    // Reset receive buffer
    memset(&rx_buffer, 0, sizeof(rx_buffer));
    
    // Wait a bit for the Simulith server to start up
    sleep(1);
    
    // Initialize Simulith client
    if (simulith_client_init(PUB_ADDR, REP_ADDR, "sample_sim", INTERVAL_NS) != 0) {
        printf("Failed to initialize Simulith client\n");
        return SAMPLE_SIM_ERROR;
    }
    
    // Handshake with Simulith server
    if (simulith_client_handshake() != 0) {
        printf("Failed to handshake with Simulith server\n");
        simulith_client_shutdown();
        return SAMPLE_SIM_ERROR;
    }
    
    // Initialize UART communication
    state->uart_port = SAMPLE_SIM_UART_ID;
    printf("Attempting to initialize UART on port %d\n", state->uart_port);
    
    int uart_result = simulith_uart_init(state->uart_port, uart_rx_callback);
    if (uart_result < 0) {
        printf("Failed to initialize UART on port %d\n", state->uart_port);
        simulith_client_shutdown();
        return SAMPLE_SIM_ERROR;
    }
    // Update port if we got a different one
    state->uart_port = uart_result;
    
    // Initialize time provider
    state->time_handle = simulith_time_init();
    if (!state->time_handle) {
        printf("Failed to initialize time provider\n");
        simulith_uart_close(state->uart_port);
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
    
    printf("Sample simulator initialized successfully on UART port %d\n", state->uart_port);
    printf("Waiting for commands...\n");
    return SAMPLE_SIM_SUCCESS;
}

void sample_sim_cleanup(sample_sim_state_t* state) {
    if (!state) return;
    
    g_state = NULL;  // Clear global state pointer
    simulith_uart_close(state->uart_port);
    
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