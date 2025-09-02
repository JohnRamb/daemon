#include "network_daemon.h"
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdlib>
#include <errno.h>
#include <string.h>

#ifdef ENABLE_DAEMON
void daemonize() {
    // First fork to create a child process
    pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error(std::string("Fork failed: ") + strerror(errno));
    }
    if (pid > 0) {
        // Parent process exits
        exit(0);
    }

    // Create a new session and set the process as session leader
    if (setsid() < 0) {
        throw std::runtime_error(std::string("Setsid failed: ") + strerror(errno));
    }

    // Second fork to prevent acquiring a controlling terminal
    pid = fork();
    if (pid < 0) {
        throw std::runtime_error(std::string("Second fork failed: ") + strerror(errno));
    }
    if (pid > 0) {
        // Parent process exits
        exit(0);
    }

    // Change working directory to root to avoid holding onto directories
    if (chdir("/") < 0) {
        throw std::runtime_error(std::string("Chdir failed: ") + strerror(errno));
    }

    // Redirect standard file descriptors to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        throw std::runtime_error(std::string("Open /dev/null failed: ") + strerror(errno));
    }
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > 2) {
        close(fd);
    }

    // Set umask to 0 for default file permissions
    umask(0);
}
#endif

int main() {
    try {
#ifdef ENABLE_DAEMON
        // Daemonize the process if ENABLE_DAEMON is defined
        daemonize();
#endif
        // Initialize and run the network daemon
        NetworkDaemon daemon;
        daemon.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown error occurred" << std::endl;
        return 1;
    }
    return 0;
}