#include "CursorTimer.h"
#include <iostream>

using namespace cursor_timer;

void test_nested() {
    CURSOR_TIMER("outer");
    
    {
        CURSOR_TIMER("inner1");
        volatile int x = 42;
        std::cout << "inner1 created\n";
    }
    std::cout << "inner1 destroyed\n";
    
    {
        CURSOR_TIMER("inner2");
        volatile int y = 84;
        std::cout << "inner2 created\n";
    }
    std::cout << "inner2 destroyed\n";
}

int main() {
    std::cout << "=== Debug Test ===\n";
    
    test_nested();
    
    std::cout << "\n=== Results ===\n";
    TreePrinter::print_tree();
    
    return 0;
}
