/*
GDBot - A Lightweight GDB C Interface for Windows

Open-source under the GNU General Public License (GPL)

GitHub Repository: https://github.com/cesarruano/gdbot

Author: Cesar Ruano
Email: cesar.ruanoalv@gmail.com
Version: 0.1

*/

#ifdef _WIN32
#define TARGET_WINDOWS
#else
#define TARGET_POSIX
#endif 

#ifdef TARGET_WINDOWS
#include <windows.h>
typedef HANDLE PIPE_TYPE;
#elif defined(TARGET_POSIX)
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>
typedef int PIPE_TYPE;
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <gdbot.h>

#define DEFAULT_CMD_WAIT (1000)

typedef struct{
    #ifdef TARGET_WINDOWS
    SECURITY_ATTRIBUTES sa;
    #endif
    PIPE_TYPE hChildStd_IN_Rd;
    PIPE_TYPE hChildStd_IN_Wr;
    PIPE_TYPE hChildStd_OUT_Rd;
    PIPE_TYPE hChildStd_OUT_Wr;
    #ifdef TARGET_WINDOWS
    PROCESS_INFORMATION piProcInfo;
    STARTUPINFO siStartInfo;
    #elif defined(TARGET_POSIX)
    pid_t child_pid;
    #endif
    bool debug;
} GDB_t;

static void __sleep(int ms){
    
    #ifdef TARGET_WINDOWS
    Sleep(ms);
    #elif defined(TARGET_POSIX)
    usleep(ms * 1000);
    #endif
}

static uint64_t get_current_time(void){
    #ifdef TARGET_WINDOWS
    return GetTickCount();
    #elif defined(TARGET_POSIX)
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
    #endif
}

static void send_command(void * _gdb, const char *cmd) {
    GDB_t * gdb = (GDB_t *) _gdb;
#ifdef TARGET_WINDOWS
    HANDLE hWrite = gdb->hChildStd_IN_Wr;
    DWORD written_data;
    if (!WriteFile(hWrite, cmd, strlen(cmd), &written_data, NULL)) {
        fprintf(stderr, "Error writing to the pipe\n");
    } else {
        if(gdb->debug)
            printf("\n\n->[GDB] %s", cmd);
    }
#elif defined(TARGET_POSIX)
    ssize_t written_data = write(gdb->hChildStd_IN_Wr, cmd, strlen(cmd));
    if (written_data < 0) {
        perror("Error writing to the pipe");
    } else {
        if(gdb->debug)
            printf("\n\n->[GDB] %s", cmd);
    }
#endif
}

static int read_output(void *_gdb, char * buffer, int timeout_ms) {
    GDB_t * gdb = (GDB_t *)_gdb;   

    uint64_t startTime = get_current_time();
    
    char *output;
    if(buffer == NULL)
        output = malloc(GDBOT_MIN_RESP_BUFFER_SIZE);
    else
        output = buffer;
    
    size_t total_size_read = 0;

    while (get_current_time() - startTime < timeout_ms) {
#ifdef TARGET_WINDOWS
        DWORD size_read, data_available = 0;
        bool res_ok = PeekNamedPipe(gdb->hChildStd_OUT_Rd, NULL, 0, NULL, &data_available, NULL);
        if (res_ok && data_available > 0) {
            size_t size_to_read = min(GDBOT_MIN_RESP_BUFFER_SIZE - 1 - total_size_read, data_available);
            res_ok = ReadFile(gdb->hChildStd_OUT_Rd, output + total_size_read, size_to_read, &size_read, NULL);

            if (size_read <= 0) {
                if (GetLastError() == ERROR_BROKEN_PIPE) {
                    printf("GDB process has exited or pipe closed.\n");
                    break;
                }
            } else {
                total_size_read += size_read;
                output[total_size_read] = '\0';
            }
        }
#elif defined(TARGET_POSIX)
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(gdb->hChildStd_OUT_Rd, &read_fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 10000; // 10 ms 

        int select_result = select(gdb->hChildStd_OUT_Rd + 1, &read_fds, NULL, NULL, &tv);
        if (select_result > 0) {
            ssize_t size_read = read(gdb->hChildStd_OUT_Rd, output + total_size_read, GDBOT_MIN_RESP_BUFFER_SIZE - 1 - total_size_read);
            if (size_read <= 0) {
                printf("GDB process has exited or pipe closed.\n");
                break;
            } else {
                total_size_read += size_read;
                output[total_size_read] = '\0';
            }
        } else if (select_result < 0) {
            perror("Select failed");
            break;
        }
#endif

        __sleep(10);
    }
    output[total_size_read] = '\0';
    
    if(gdb->debug)
        printf("\n\n[GDB]-> %s", output);
    
    if (buffer == NULL)
        free(output);
    return total_size_read;
}

static void send_ctrl_c(void *_gdb){
    
    #ifdef TARGET_WINDOWS
    SetConsoleCtrlHandler(NULL, TRUE);
    if (!GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0)) {
        fprintf(stderr, "Failed to send CTRL+C event to GDB process\n");
        return;
    }
    SetConsoleCtrlHandler(NULL, FALSE);
    #elif defined(TARGET_POSIX)
    GDB_t * gdb = (GDB_t *)_gdb;
    if (gdb->child_pid > 0) {
        if (kill(gdb->child_pid, SIGINT) < 0) {
            perror("Failed to send SIGINT to GDB process");
        }
    }
    #endif
    __sleep(500);
}


static int busy_read(void * _gdb, char * buffer, int timeout_ms){
    GDB_t * gdb = (GDB_t *)_gdb;
    send_command(gdb, "ping\n");
    int output_len = read_output(gdb, buffer, timeout_ms);
    buffer[output_len] = '\0';
    return output_len;
}

void * gdb_start(char * executable){
    GDB_t * gdb = malloc(sizeof(GDB_t));
    
    gdb->debug = false;
    
#ifdef TARGET_WINDOWS
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
    
#elif defined(TARGET_POSIX)

    // Linux-specific process creation and pipe setup
    int pipe_stdin[2], pipe_stdout[2];
    if (pipe(pipe_stdin) == -1 || pipe(pipe_stdout) == -1) {
        perror("Pipe creation failed");
        exit(1);
    }

    gdb->child_pid = fork();
    if (gdb->child_pid == -1) {
        perror("Fork failed");
        exit(1);
    }

    if (gdb->child_pid == 0) { // Child process
        close(pipe_stdin[1]);  // Close unused write end
        close(pipe_stdout[0]); // Close unused read end

        dup2(pipe_stdin[0], STDIN_FILENO);
        dup2(pipe_stdout[1], STDOUT_FILENO);
        dup2(pipe_stdout[1], STDERR_FILENO);

        execlp("gdb", "gdb", "--interpreter=mi", executable, (char *)NULL);
        perror("execlp failed");
        exit(1);
    } else { // Parent process
        close(pipe_stdin[0]);  // Close unused read end
        close(pipe_stdout[1]); // Close unused write end

        gdb->hChildStd_IN_Rd = pipe_stdin[0];
        gdb->hChildStd_IN_Wr = pipe_stdin[1];
        gdb->hChildStd_OUT_Rd = pipe_stdout[0];
        gdb->hChildStd_OUT_Wr = pipe_stdout[1];
    }
#endif
    return gdb;
}

void gdb_finish(void *_gdb) {
    GDB_t * gdb = (GDB_t *)_gdb;
    
    #ifdef TARGET_WINDOWS
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
    
#elif defined(TARGET_POSIX)

    // Close the pipe file descriptors
    if (gdb->hChildStd_OUT_Rd > 0) {
        close(gdb->hChildStd_OUT_Rd);
    }
    if (gdb->hChildStd_IN_Wr > 0) {
        close(gdb->hChildStd_IN_Wr);
    }

    // Terminate the GDB process
    if (gdb->child_pid > 0) {
        kill(gdb->child_pid, SIGTERM);  // Send a SIGTERM signal to terminate the process
        waitpid(gdb->child_pid, NULL, 0);  // Wait for the process to exit
    }
    #endif
    
    free(_gdb);
}

void gdb_set_debug_mode(void *_gdb, bool value){
    GDB_t * gdb = (GDB_t *) _gdb;
    gdb->debug = value;
}

int gdb_run_command(void *_gdb, const char *cmd, char * result, int timeout_ms){
    GDB_t * gdb = (GDB_t *)_gdb;
    send_command(gdb, cmd);

    int response_size = 0;

    uint64_t start_time = get_current_time();

    while (1) {
        if (get_current_time() - start_time > timeout_ms) {
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
    uint64_t startTime = get_current_time();
    while (get_current_time() - startTime < timeout_ms) {
        int output_len = busy_read(gdb, output, DEFAULT_CMD_WAIT);
        if(output_len > 0){
            if (strstr(output, "*stopped,reason=\"breakpoint-hit\"") && strstr(output, breakpoint_location)) {
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
