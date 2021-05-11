//Source1.cpp
#include <stdio.h>
#include "Header.h"
int g_int = 3;
void TestSource1() {

    printf("g_int's address in Source1.cpp: %08x\n", &g_int);
   g_int = 5;
    printf("g_int's value in Source1.cpp: %d\n", g_int);
}