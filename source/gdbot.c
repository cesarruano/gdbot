/*
GDBot - A Lightweight GDB C Interface for Windows

Open-source under the GNU General Public License (GPL)

GitHub Repository: https://github.com/cesarruano/gdbot

Author: Cesar Ruano
Email: cesar.ruanoalv@gmail.com
Version: 0.1

*/

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#include <gdbot.h>

#define DEFAULT_CMD_WAIT (1000)

typedef struct{
    SECURITY_ATTRIBUTES sa;
    HANDLE hChildStd_IN_Rd;
    HANDLE hChildStd_IN_Wr;
    HANDLE hChildStd_OUT_Rd;
    HANDLE hChildStd_OUT_Wr;
    PROCESS_INFORMATION piProcInfo;
    STARTUPINFO siStartInfo;
    bool debug;
} GDB_t;

static void __sleep(int ms){
    Sleep(ms);
}

static void send_command(void * _gdb, const char *cmd) {
    GDB_t * gdb = (GDB_t *) _gdb;
    HANDLE hWrite = gdb->hChildStd_IN_Wr;
    DWORD written_data;
    if (!WriteFile(hWrite, cmd, strlen(cmd), &written_data, NULL)) {
        fprintf(stderr, "Error writing to the pipe\n");
    } else {
        if(gdb->debug)
            printf("\n\n->[GDB] %s", cmd);
    }
}

static int read_output(void *_gdb, char * buffer, int timeout_ms) {
    GDB_t * gdb = (GDB_t *)_gdb;
    
    DWORD size_read, data_available = 0;
    uint64_t startTime = GetTickCount();
    
    char *output;
    if(buffer == NULL)
        output = malloc(GDBOT_MIN_RESP_BUFFER_SIZE);
    else
        output = buffer;
    
    size_t total_size_read = 0;

    /*This is peeking before reading. Concatenating all data read in the buffer before the timeout expires.*/
    while (GetTickCount() - startTime < timeout_ms) {
        bool res_ok = PeekNamedPipe(gdb->hChildStd_OUT_Rd, NULL, 0, NULL, &data_available, NULL);
        if (res_ok && data_available > 0) {
            size_t size_to_read = min(GDBOT_MIN_RESP_BUFFER_SIZE - 1 - total_size_read, data_available);
            res_ok = ReadFile(gdb->hChildStd_OUT_Rd, output + total_size_read, size_to_read, &size_read, NULL);

            if (!res_ok) {
                if (GetLastError() == ERROR_BROKEN_PIPE) {
                    printf("GDB process has exited or pipe closed.\n");
                    break;
                }
            } else if (size_read > 0) {
                total_size_read += size_read;
                output[total_size_read] = '\0';
            }
        }

        __sleep(10);
    }
    output[total_size_read] = '\0';
    
    if(gdb->debug)
        printf("\n\n[GDB]-> %s", output);
    
    if (buffer == NULL)
        free(output);
    return total_size_read;
}

static int busy_read(void * _gdb, char * buffer, int timeout_ms){
    GDB_t * gdb = (GDB_t *)_gdb;
    send_command(gdb, "ping\n");
    int output_len = read_output(gdb, buffer, timeout_ms);
    buffer[output_len] = '\0';
    return output_len;
}

static void send_ctrl_c(void *_gdb){
    (void)_gdb;
    SetConsoleCtrlHandler(NULL, TRUE);
    if (!GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0)) {
        fprintf(stderr, "Failed to send CTRL+C event to GDB process\n");
        return;
    }
    __sleep(500);
    SetConsoleCtrlHandler(NULL, FALSE);
}

void gdb_set_debug_mode(void *_gdb, bool value){
    GDB_t * gdb = (GDB_t *) _gdb;
    gdb->debug = value;
}

void * gdb_start(char * executable){
    GDB_t * gdb = malloc(sizeof(GDB_t));
    
    gdb->debug = false;
    
    // Initialize SECURITY_ATTRIBUTES
    gdb->sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    gdb->sa.bInheritHandle = TRUE;
    gdb->sa.lpSecurityDescriptor = NULL;

    // Create pipes for STDOUT
    if (!CreatePipe(&gdb->hChildStd_OUT_Rd, &gdb->hChildStd_OUT_Wr, &gdb->sa, 0)) {
        fprintf(stderr, "Stdout pipe creation failed\n");
        exit(1);
    }
    SetHandleInformation(gdb->hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0);

    // Create pipes for STDIN
    if (!CreatePipe(&gdb->hChildStd_IN_Rd, &gdb->hChildStd_IN_Wr, &gdb->sa, 0)) {
        fprintf(stderr, "Stdin pipe creation failed\n");
        exit(1);
    }
    SetHandleInformation(gdb->hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0);

    // Initialize STARTUPINFO
    ZeroMemory(&gdb->siStartInfo, sizeof(STARTUPINFO));
    gdb->siStartInfo.cb = sizeof(STARTUPINFO);
    gdb->siStartInfo.hStdError = gdb->hChildStd_OUT_Wr;
    gdb->siStartInfo.hStdOutput = gdb->hChildStd_OUT_Wr;
    gdb->siStartInfo.hStdInput = gdb->hChildStd_IN_Rd;
    gdb->siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    // Initialize PROCESS_INFORMATION
    ZeroMemory(&gdb->piProcInfo, sizeof(PROCESS_INFORMATION));

    // Format the executable command
    char command[256];
    //snprintf(command, sizeof(command), "gdb  %s", executable);
    snprintf(command, sizeof(command), "gdb --interpreter=mi %s", executable);

    // Create the GDB process
    if (!CreateProcess(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &gdb->siStartInfo, &gdb->piProcInfo)) {
        fprintf(stderr, "CreateProcess failed\n");
        exit(1);
    }

    // Close handles to the pipes not used by this process
    CloseHandle(gdb->hChildStd_OUT_Wr);
    CloseHandle(gdb->hChildStd_IN_Rd);
    
    return gdb;
}

void gdb_finish(void *_gdb) {
    GDB_t * gdb = (GDB_t *)_gdb;
    // Close the read end of the STDOUT pipe
    if (gdb->hChildStd_OUT_Rd != NULL) {
        CloseHandle(gdb->hChildStd_OUT_Rd);
    }

    // Close the write end of the STDIN pipe
    if (gdb->hChildStd_IN_Wr != NULL) {
        CloseHandle(gdb->hChildStd_IN_Wr);
    }

    // Terminate the GDB process forcefully
    TerminateProcess(gdb->piProcInfo.hProcess, 0);

    // Close the process and thread handles
    CloseHandle(gdb->piProcInfo.hProcess);
    CloseHandle(gdb->piProcInfo.hThread);
    
    free(_gdb);
}

int gdb_run_command(void *_gdb, const char *cmd, char * result, int timeout_ms){
    GDB_t * gdb = (GDB_t *)_gdb;
    send_command(gdb, cmd);

    int response_size = 0;

    uint64_t start_time = GetTickCount();

    while (1) {
        if (GetTickCount() - start_time > timeout_ms) {
            break; // Timeout reached
        }

        char buffer[GDBOT_MIN_RESP_BUFFER_SIZE];
        int bytes_read = read_output(gdb, buffer, 100);

        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';

            int new_size = response_size + bytes_read;

            strcat(result, buffer);
            response_size = new_size;

            if (strstr(result, "(gdb)")) {
                return response_size; // Command completed
            }
        }
    }
    return response_size;
}

int gdb_pause(void *_gdb){
    
    char busy_read_buffer[GDBOT_MIN_RESP_BUFFER_SIZE];
    
    for(int i=0; i<3; i++){
        int output_len = busy_read(_gdb, busy_read_buffer, DEFAULT_CMD_WAIT);
        if(output_len > 0)//already paused
            return 0;
        else
            send_ctrl_c(_gdb);
    }
    
    return 1;
    
}

void gdb_set_break(void *_gdb, char *cmd) {
    GDB_t * gdb = (GDB_t *)_gdb;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "break %s\n", cmd);
    send_command(gdb, buffer);
    (void)read_output(gdb, NULL, DEFAULT_CMD_WAIT);
}

int gdb_set_and_wait_break(void *_gdb, char *breakpoint_location, int timeout_ms) {
    GDB_t * gdb = (GDB_t *)_gdb;
    gdb_pause(gdb);
    gdb_set_break(gdb, breakpoint_location);
    send_command(gdb, "continue\n");
    
    char output[GDBOT_MIN_RESP_BUFFER_SIZE];
    uint64_t startTime = GetTickCount();
    while (GetTickCount() - startTime < timeout_ms) {
        int output_len = busy_read(gdb, output, DEFAULT_CMD_WAIT);
        if(output_len > 0){
            if (strstr(output, "hit Breakpoint") && strstr(output, breakpoint_location)) {
                //printf("breakpoint hit: %s\n", breakpoint_location);
                return 0;  // Breakpoint hit
            } else  if (!strstr(output, "^running")) {
                send_command(gdb, "continue\n");
            }
        }

        __sleep(DEFAULT_CMD_WAIT/10);
    }
    
    printf("$[GDBOT INFO] Timeout waiting for breakpoint %s\n", breakpoint_location);
    (void)read_output(gdb, NULL, DEFAULT_CMD_WAIT);
    return 1;
}

void gdb_delete_breaks(void *_gdb) {
    GDB_t * gdb = (GDB_t *)_gdb;
    send_command(gdb, "delete\n");
    (void)read_output(gdb, NULL, DEFAULT_CMD_WAIT);
}

void gdb_run(void *_gdb) {
    GDB_t * gdb = (GDB_t *)_gdb;
    send_command(gdb, "run > nul\n");
    (void)read_output(gdb, NULL, DEFAULT_CMD_WAIT);
}

void gdb_continue(void *_gdb) {
    GDB_t * gdb = (GDB_t *)_gdb;
    send_command(gdb, "continue\n");
    (void)read_output(gdb, NULL, DEFAULT_CMD_WAIT);
}

int gdb_read_var(void *_gdb, char *name, char * result){
    return gdb_eval(_gdb, name, result);
}

int64_t gdb_read_var_int(void *_gdb, char *name) {
    char buffer[64];
    (void)gdb_eval(_gdb, name, buffer);
    return atoll(buffer);
}

uint64_t gdb_read_var_uint(void *_gdb, char *name) {
    char buffer[64];
    (void)gdb_eval(_gdb, name, buffer);
    return atoll(buffer);
}

double gdb_read_var_double(void *_gdb, char *name) {
    char buffer[64];
    (void)gdb_eval(_gdb, name, buffer);
    return atof(buffer);
}

void gdb_write_var_int(void *gdb, char *name, int64_t value){
    char value_str[64];  
    sprintf(value_str, "%" PRId64, value);
    gdb_write_var(gdb, name, value_str);
}

void gdb_write_var_uint(void *gdb, char *name, uint64_t value){
    char value_str[64];  
    sprintf(value_str, "%" PRId64, value);
    gdb_write_var(gdb, name, value_str);
}

void gdb_write_var_double(void *gdb, char *name, double value) {
    char value_str[64];
    snprintf(value_str, sizeof(value_str), "%lf", value);  // Use %lf for double
    gdb_write_var(gdb, name, value_str);
}

void gdb_write_var(void *_gdb, char *name, char * value) {
    char expression[GDBOT_MIN_RESP_BUFFER_SIZE];
    sprintf(expression, "%s = %s", name, value);
    gdb_eval(_gdb, expression, NULL);
}

int gdb_eval(void* gdb, const char* expression, char * result) {
    char command[GDBOT_MIN_RESP_BUFFER_SIZE];
    snprintf(command, sizeof(command), "-data-evaluate-expression \"%s\"\n", expression);
    char intermediate_result[GDBOT_MIN_RESP_BUFFER_SIZE];
    send_command(gdb, command);
    int out_len = read_output(gdb, intermediate_result, DEFAULT_CMD_WAIT);
    if (out_len && result != NULL) {
        char* result_start = strstr(intermediate_result, "^done,value=\"");
        if (result_start) {
            result_start += strlen("^done,value=\"");
            char* result_end = strchr(result_start, '\"');
            if (result_end) {
                size_t result_length = result_end - result_start;
                strncpy(result, result_start, result_length);
                result[result_length] = '\0'; // Terminate the result string
                return out_len;
            }
        }
    }

    return out_len;
}
