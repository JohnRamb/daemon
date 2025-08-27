#include "network_daemon.h"
#include "command_processor.h"
#include "s_expression_parser.h"
#include <netlink/msg.h>
#include <netlink/route/addr.h>
#include <netlink/route/link.h>
#include <netlink/route/route.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/rtnetlink.h>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <chrono>
#include <signal.h>
#include <sys/wait.h>

const char* SOCKET_PATH = "/tmp/network_daemon.sock";

NetworkDaemon::NetworkDaemon() 
    : netlink_mgr_(),
      unix_server_(loop_, SOCKET_PATH),
      network_mgr_(netlink_mgr_),
      command_processor_(std::make_unique<CommandProcessor>(
          unix_server_, netlink_mgr_, network_mgr_, std::make_unique<SExpressionParser>())) {

    std::cout << "[" << getTimestamp() << "] NetworkDaemon: Starting initialization" << std::endl;
    
    setupSignalHandlers();
    std::cout << "[" << getTimestamp() << "] NetworkDaemon: Signal handlers configured" << std::endl;
    
    try {
        std::cout << "[" << getTimestamp() << "] NetworkDaemon: Initializing NetlinkManager" << std::endl;
        netlink_mgr_.init();
        std::cout << "[" << getTimestamp() << "] NetworkDaemon: NetlinkManager initialized successfully" << std::endl;
        
        // Callback для обработки сообщений об адресах
        netlink_mgr_.setAddrCallback([this](struct nl_msg* msg) {
            struct nlmsghdr* nlh = nlmsg_hdr(msg);
            struct ifaddrmsg* ifa = (struct ifaddrmsg*)nlmsg_data(nlh);
            struct nlattr* tb[IFA_MAX + 1];
            char ip_str[INET_ADDRSTRLEN] = {0};
            char mask_str[INET_ADDRSTRLEN] = {0};
            char ifname[IFNAMSIZ] = {0};

            nla_parse(tb, IFA_MAX, nlmsg_attrdata(nlh, sizeof(*ifa)), nlmsg_attrlen(nlh, sizeof(*ifa)), NULL);
            
            if (tb[IFA_ADDRESS]) {
                struct in_addr* addr = (struct in_addr*)nla_data(tb[IFA_ADDRESS]);
                inet_ntop(AF_INET, addr, ip_str, sizeof(ip_str));
            }
            
            int mask_len = ifa->ifa_prefixlen;
            uint32_t mask = htonl(~((1U << (32 - mask_len)) - 1));
            struct in_addr mask_addr = { mask };
            inet_ntop(AF_INET, &mask_addr, mask_str, sizeof(mask_str));

            if_indextoname(ifa->ifa_index, ifname);

            std::string msg_type = (nlh->nlmsg_type == RTM_NEWADDR) ? "NEWADDR" : 
                                  (nlh->nlmsg_type == RTM_DELADDR) ? "DELADDR" : "UNKNOWN";

            std::cout << "[" << this->getTimestamp() << "] NetworkDaemon: Received address message (" << msg_type << "): "
                      << "Interface: " << ifname << " (index: " << ifa->ifa_index << ")"
                      << ", IP: " << ip_str
                      << ", Mask: " << mask_str << "/" << mask_len
                      << ", Family: " << (ifa->ifa_family == AF_INET ? "IPv4" : "IPv6")
                      << ", Scope: " << (int)ifa->ifa_scope
                      << std::endl;
        });
        std::cout << "[" << getTimestamp() << "] NetworkDaemon: Address callback set" << std::endl;
        
        // Callback для обработки сообщений об интерфейсах
        netlink_mgr_.setLinkCallback([this](struct nl_msg* msg) {
            struct nlmsghdr* nlh = nlmsg_hdr(msg);
            struct ifinfomsg* ifi = (struct ifinfomsg*)nlmsg_data(nlh);
            struct nlattr* tb[IFLA_MAX + 1];
            char ifname[IFNAMSIZ] = {0};
            char mac_str[18] = {0};

            nla_parse(tb, IFLA_MAX, nlmsg_attrdata(nlh, sizeof(*ifi)), nlmsg_attrlen(nlh, sizeof(*ifi)), NULL);
            
            if (tb[IFLA_IFNAME]) {
                strncpy(ifname, (char*)nla_data(tb[IFLA_IFNAME]), IFNAMSIZ - 1);
            }
            if (tb[IFLA_ADDRESS]) {
                unsigned char* mac = (unsigned char*)nla_data(tb[IFLA_ADDRESS]);
                snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }

            std::string msg_type = (nlh->nlmsg_type == RTM_NEWLINK) ? "NEWLINK" : 
                                  (nlh->nlmsg_type == RTM_DELLINK) ? "DELLINK" : "UNKNOWN";

            std::cout << "[" << this->getTimestamp() << "] NetworkDaemon: Received link message (" << msg_type << "): "
                      << "Interface: " << ifname
                      << ", Index: " << ifi->ifi_index
                      << ", MAC: " << (tb[IFLA_ADDRESS] ? mac_str : "none")
                      << ", Flags: 0x" << std::hex << ifi->ifi_flags << std::dec
                      << " (" << (ifi->ifi_flags & IFF_UP ? "UP" : "DOWN")
                      << (ifi->ifi_flags & IFF_RUNNING ? ",RUNNING" : "")
                      << (ifi->ifi_flags & IFF_LOOPBACK ? ",LOOPBACK" : "")
                      << (ifi->ifi_flags & IFF_BROADCAST ? ",BROADCAST" : "")
                      << (ifi->ifi_flags & IFF_MULTICAST ? ",MULTICAST" : "")
                      << ")" << std::endl;
        });
        std::cout << "[" << getTimestamp() << "] NetworkDaemon: Link callback set" << std::endl;
        
        // Callback для обработки сообщений о маршрутах
        netlink_mgr_.setRouteCallback([this](struct nl_msg* msg) {
            struct nlmsghdr* nlh = nlmsg_hdr(msg);
            struct rtmsg* rtm = (struct rtmsg*)nlmsg_data(nlh);
            struct nlattr* tb[RTA_MAX + 1];
            char gw_str[INET_ADDRSTRLEN] = {0};
            char dst_str[INET_ADDRSTRLEN] = {0};
            char ifname[IFNAMSIZ] = {0};

            nla_parse(tb, RTA_MAX, nlmsg_attrdata(nlh, sizeof(*rtm)), nlmsg_attrlen(nlh, sizeof(*rtm)), NULL);
            
            if (tb[RTA_GATEWAY]) {
                struct in_addr* gw = (struct in_addr*)nla_data(tb[RTA_GATEWAY]);
                inet_ntop(AF_INET, gw, gw_str, sizeof(gw_str));
            }
            if (tb[RTA_DST]) {
                struct in_addr* dst = (struct in_addr*)nla_data(tb[RTA_DST]);
                inet_ntop(AF_INET, dst, dst_str, sizeof(dst_str));
            }
            if (tb[RTA_OIF]) {
                if_indextoname(*(int*)nla_data(tb[RTA_OIF]), ifname);
            }

            std::string msg_type = (nlh->nlmsg_type == RTM_NEWROUTE) ? "NEWROUTE" : 
                                  (nlh->nlmsg_type == RTM_DELROUTE) ? "DELROUTE" : "UNKNOWN";

            std::string protocol;
            switch (rtm->rtm_protocol) {
                case RTPROT_UNSPEC: protocol = "UNSPEC"; break;
                case RTPROT_BOOT: protocol = "BOOT"; break;
                case RTPROT_STATIC: protocol = "STATIC"; break;
                default: protocol = std::to_string(rtm->rtm_protocol); break;
            }

            std::cout << "[" << this->getTimestamp() << "] NetworkDaemon: Received route message (" << msg_type << "): "
                      << "Destination: " << (tb[RTA_DST] ? dst_str : "default")
                      << ", Gateway: " << (tb[RTA_GATEWAY] ? gw_str : "none")
                      << ", Interface: " << (tb[RTA_OIF] ? ifname : "none")
                      << ", Table: " << (int)rtm->rtm_table
                      << ", Protocol: " << protocol
                      << ", Scope: " << (rtm->rtm_scope == RT_SCOPE_LINK ? "LINK" : 
                                        rtm->rtm_scope == RT_SCOPE_HOST ? "HOST" : 
                                        std::to_string(rtm->rtm_scope))
                      << std::endl;
        });
        std::cout << "[" << getTimestamp() << "] NetworkDaemon: Route callback set" << std::endl;
        
        // Добавляем netlink сокет в цикл событий
        std::cout << "[" << getTimestamp() << "] NetworkDaemon: Adding netlink socket to event loop (fd: " 
                  << netlink_mgr_.getSocketFd() << ")" << std::endl;
        loop_.add(netlink_mgr_.getSocketFd(), EPOLLIN, 
            std::bind(&NetworkDaemon::handleNetlinkEvent, this, std::placeholders::_1, std::placeholders::_2));
        std::cout << "[" << getTimestamp() << "] NetworkDaemon: Netlink socket added to event loop" << std::endl;

        std::cout << "[" << getTimestamp() << "] NetworkDaemon: Starting UNIX server at " << SOCKET_PATH << std::endl;
        unix_server_.start();
        std::cout << "[" << getTimestamp() << "] NetworkDaemon: UNIX server started successfully" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[" << getTimestamp() << "] NetworkDaemon: Error initializing NetlinkManager: " 
                  << e.what() << std::endl;
        throw;
    }

    std::cout << "[" << getTimestamp() << "] NetworkDaemon: Initialization completed successfully" << std::endl;
}

NetworkDaemon::~NetworkDaemon() {
    // Очистка ресурсов
}

std::string NetworkDaemon::getTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void NetworkDaemon::setupSignalHandlers() {
    struct sigaction sa;
    sa.sa_handler = [](int) {
        int status;
        while (waitpid(-1, &status, WNOHANG) > 0) {}
    };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, nullptr) == -1) {
        throw std::system_error(errno, std::generic_category(), "Ошибка установки обработчика SIGCHLD");
    }
}

void NetworkDaemon::handleNetlinkEvent(int fd, uint32_t events) {
    try {
        netlink_mgr_.processEvents();
    } catch (const std::exception& e) {
        std::cerr << "[" << getTimestamp() << "] NetworkDaemon: Error processing netlink events: " << e.what() << std::endl;
    }
}

void NetworkDaemon::run() {
    std::cout << "[" << getTimestamp() << "] NetworkDaemon: Daemon started, waiting for commands..." << std::endl;
    loop_.run();
}

void NetworkDaemon::stop() {
    loop_.stop();
}