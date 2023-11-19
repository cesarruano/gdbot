#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>    
#endif

double dummy_double = 0;
int dummy_int = 0;

void sleep_ms(int ms){
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

int main() {
    printf("at first dummy_double is: %g\r\n", dummy_double);
    dummy_double++;
    printf("but now, dummy_double is: %g\r\n", dummy_double);
    
    sleep_ms(2000);
    printf("Sleeping...\r\n");
    sleep_ms(2000);
    
    dummy_double++;
    printf("eventually dummy_double is: %g\r\n", dummy_double);
    return 0;
}
