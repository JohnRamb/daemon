#pragma once

#include <sys/wait.h>

#include "dhcp_client.h"

class DhcpcdClient : public DHCPClient {
public:
    bool start(const std::string& ifname) override {
        pid_t pid = fork();
        if (pid == 0) {
            execlp("dhcpcd", "dhcpcd", "-n", ifname.c_str(), nullptr);
            _exit(EXIT_FAILURE);
        }
        
        int status;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    
    bool stop(const std::string& ifname) override {
        pid_t pid = fork();
        if (pid == 0) {
            execlp("dhcpcd", "dhcpcd", "-k", ifname.c_str(), nullptr);
            _exit(EXIT_FAILURE);
        }
        
        waitpid(pid, nullptr, 0);
        return true;
    }

    bool isRunning(const std::string& interface) override {
        // DUMMY
        return true;
    }
};