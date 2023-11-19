#include <stdio.h>
#include <windows.h>

double dummy_double = 0;
int dummy_int = 0;

int main() {
    printf("at first dummy_double is: %g\r\n", dummy_double);
    dummy_double++;
    printf("but now, dummy_double is: %g\r\n", dummy_double);
    
    Sleep(2000);
    printf("Sleeping...\r\n");
    Sleep(2000);
    
    dummy_double++;
    printf("eventually dummy_double is: %g\r\n", dummy_double);
    return 0;
}
