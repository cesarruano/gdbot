#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>    
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <gdbot.h>

void sleep_ms(int ms){
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

int main() {
#ifdef _WIN32
    void * hello_gdb = gdb_start("hello.exe");   
#else
    void * hello_gdb = gdb_start("hello");   
#endif
    //gdb_set_debug_mode(hello_gdb, true);
    gdb_set_break(hello_gdb, "main");

    gdb_run(hello_gdb);

    printf("\nValue of dummy_double before running: %d\n", (int)gdb_read_var_int(hello_gdb, "dummy_double"));
    
    printf("\nWriting 1 in dummy_double as int...\n");
    gdb_write_var_int(hello_gdb, "dummy_double", 1);
    
    int64_t dummy_double_read_value = gdb_read_var_double(hello_gdb, "dummy_double");
    printf("\nValue of dummy_double: %d\n", (int)dummy_double_read_value);
    
    printf("\nWriting and reading dummy_double with an expression...\n");
    char expression[] = "dummy_double = 3.1415";
    char expression_result[32];
    gdb_eval(hello_gdb, expression, expression_result);
    printf("\nExpression:  \"%s\" \n\tResult as string %s\n\tResult as int %d \n\tResult as double %g\n\n", 
        expression, 
        expression_result,
        (int)atoll(expression_result),
        atof(expression_result));
    
    printf("\nResume execution from main...\n");
    gdb_continue(hello_gdb);
    
    printf("\nSetting a breakpoint at hello.c line 30...\n");
    int break_result = gdb_set_and_wait_break(hello_gdb, "hello.c:30", 20000);
    if(break_result != 0)
        printf("Couldn't reach the breakpoint!\n");
    else
        printf("Breakpoint reached\n");
    
    dummy_double_read_value = gdb_read_var_int(hello_gdb, "dummy_double");
    printf("\nValue of dummy_double: %d\n", (int)dummy_double_read_value);
    
    if(dummy_double_read_value == 5)
        printf("\nThe demo went OK\n");
    else
        printf("\nThe demo went NOK\n");
    
    gdb_continue(hello_gdb);
    sleep_ms(2000);
    
    gdb_finish(hello_gdb);

    return 0;
}
