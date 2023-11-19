/*
GDBot - A Lightweight GDB C Interface for Windows

Open-source under the GNU General Public License (GPL)

GitHub Repository: https://github.com/cesarruano/gdbot

Author: Cesar Ruano
Email: cesar.ruanoalv@gmail.com
Version: 0.1

*/

#ifndef GDBOT_H
#define GDBOT_H

#include <stdint.h>
#include <stdbool.h>

#define GDBOT_MIN_RESP_BUFFER_SIZE (4*1024)

/*Starts the process with gdb*/
void * gdb_start(char * executable);

/*Cleans up the created resources*/
void gdb_finish(void *gdb);

/*Pauses execution, returns 0 if pause was successful*/
int gdb_pause(void *gdb);

/*Sets a breakpoint, it won't be effective until execution is paused*/
void gdb_set_break(void *gdb, char *cmd);

/*Sets a breakpoint and waits until it is reached. 
It doesn't matter if the process was already running.
Returns 0 only if the breakpoint was reached.*/
int gdb_set_and_wait_break(void *gdb, char *breakpoint_location, int timeout_ms);

/*Deletes all existing breakpoints*/
void gdb_delete_breaks(void *gdb);

/*Runs the process (from main)*/
void gdb_run(void *gdb);

/*Continues execution of the process if it was paused*/
void gdb_continue(void *gdb);

/*Reads a variable given its name.
result will contain a string with the expresion evaluated by gdb.
The int return value is the length of the result.*/
int gdb_read_var(void *gdb, char *name, char * result);

/*Reads a variable given its name and treats its value as an int64_t*/
int64_t gdb_read_var_int(void *gdb, char *name);

/*Reads a variable given its name and treats its value as an uint64_t*/
uint64_t gdb_read_var_uint(void *gdb, char *name);

/*Reads a variable given its name and treats its value as a double*/
double gdb_read_var_double(void *gdb, char *name);

/*Writes a variable given its name with the value in the expression string*/
void gdb_write_var(void *gdb, char *name, char *expression);

/*Writes a variable given its name with the int64_t value passed*/
void gdb_write_var_int(void *gdb, char *name, int64_t value);

/*Writes a variable given its name with the uint64_t value passed*/
void gdb_write_var_uint(void *gdb, char *name, uint64_t value);

/*Writes a variable given its name with the double value passed*/
void gdb_write_var_double(void *gdb, char *name, double value);

/*Runs a the given command in gdb.
The result string contains the output that gdb can generate
within timeout_ms*/
int gdb_run_command(void *gdb, const char *cmd, char * result, int timeout_ms);

/*Evaluates an expression in gdb.
It can be used to read variables and to assign them.*/
int gdb_eval(void* gdb, const char* expression, char * result);

/*In debug mode data sent to and received from gdb is printed in stdout*/
void gdb_set_debug_mode(void *gdb, bool value);

#endif //GDBOT_H