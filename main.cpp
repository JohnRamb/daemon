#include "network_daemon.h"
#include <iostream>

int main() {
    try {
        NetworkDaemon daemon;
        daemon.run();
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}