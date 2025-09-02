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
#include <string>
#include <optional>
#include <format>
#include <iostream>
#include <chrono>
#include <signal.h>
#include <sys/wait.h>
#include <format>

const char* SOCKET_PATH = "/sdz/control_sock";

// Класс для представления данных сетевого события
class NetworkEvent {
public:
    NetworkEvent(std::string iface, std::string addr, std::string mac,
                 std::string gateway, std::string mask, std::string flags)
        : iface_(std::move(iface)), addr_(std::move(addr)), mac_(std::move(mac)),
          gateway_(std::move(gateway)), mask_(std::move(mask)), flags_(std::move(flags)) {}

    std::string toString() const {
        return std::format("iface={} addr={} mac={} gateway={} mask={} flag={}",
                           iface_, addr_, mac_, gateway_, mask_, flags_);
    }

private:
    std::string iface_;
    std::string addr_;
    std::string mac_;
    std::string gateway_;
    std::string mask_;
    std::string flags_;
};

// Вспомогательная функция для разбора атрибутов Netlink
template<typename MsgType, int MaxAttr>
std::optional<std::array<nlattr*, MaxAttr + 1>> parseNetlinkAttributes(nlmsghdr* nlh, MsgType* msg) {
    std::array<nlattr*, MaxAttr + 1> tb{};
    if (nla_parse(tb.data(), MaxAttr, nlmsg_attrdata(nlh, sizeof(*msg)), nlmsg_attrlen(nlh, sizeof(*msg)), nullptr) < 0) {
        std::cerr << "[NetworkDaemon] Failed to parse Netlink attributes" << std::endl;
        return std::nullopt;
    }
    return tb;
}

// Вспомогательная функция для преобразования in_addr в строку
std::optional<std::string> inetToString(const in_addr* addr) {
    if (!addr) return std::nullopt;
    char buffer[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, addr, buffer, sizeof(buffer))) {
        return std::string(buffer);
    }
    return std::nullopt;
}

// Вспомогательная функция для получения имени интерфейса
std::string getInterfaceName(int ifindex) {
    char ifname[IFNAMSIZ] = {0};
    if (if_indextoname(ifindex, ifname)) {
        return ifname;
    }
    return "none";
}

// Конструктор NetworkDaemon (без изменений, но с лямбда-функциями вместо std::bind)
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
        
        netlink_mgr_.setAddrCallback([this](nl_msg* msg) { handleAddrEvent(msg); });
        netlink_mgr_.setLinkCallback([this](nl_msg* msg) { handleLinkEvent(msg); });
        netlink_mgr_.setRouteCallback([this](nl_msg* msg) { handleRouteEvent(msg); });
        
        std::cout << "[" << getTimestamp() << "] NetworkDaemon: Netlink callbacks configured" << std::endl;
        
        std::cout << "[" << getTimestamp() << "] NetworkDaemon: Adding netlink socket to event loop (fd: " 
                  << netlink_mgr_.getSocketFd() << ")" << std::endl;

        loop_.add(netlink_mgr_.getSocketFd(), EV_READ, 
            [this](int fd, uint32_t events) { handleNetlinkEvent(fd, events); });
        
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

void NetworkDaemon::handleLinkEvent(nl_msg* msg) {
    nlmsghdr* nlh = nlmsg_hdr(msg);
    ifinfomsg* ifi = (ifinfomsg*)nlmsg_data(nlh);

    auto tb = parseNetlinkAttributes<ifinfomsg, IFLA_MAX>(nlh, ifi);
    if (!tb) return;

    std::string ifname = tb->at(IFLA_IFNAME) ? std::string(static_cast<char*>(nla_data(tb->at(IFLA_IFNAME)))) : "none";
    std::string mac_str = "none";
    if (tb->at(IFLA_ADDRESS)) {
        auto* mac = static_cast<unsigned char*>(nla_data(tb->at(IFLA_ADDRESS)));
        mac_str = std::format("{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
                              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    std::string event_type = (nlh->nlmsg_type == RTM_NEWLINK) ? "add_iface" : "del_iface";
    std::string flags = std::format("{:08x}", ifi->ifi_flags);

    NetworkEvent event{ifname, "none", mac_str, "none", "none", flags};
    std::string event_message = command_processor_->getSerializer()->serializeResponse(event_type, event.toString());

    try {
        unix_server_.broadcastToAllClients(event_message);
    } catch (const std::exception& e) {
        std::cerr << "[" << getTimestamp() << "] NetworkDaemon: Failed to broadcast link event: " << e.what() << std::endl;
    }

    std::string msg_type = (nlh->nlmsg_type == RTM_NEWLINK) ? "NEWLINK" : "DELLINK";
    std::cout << "[" << getTimestamp() << "] NetworkDaemon: " << event_message << std::endl;
}

void NetworkDaemon::handleAddrEvent(nl_msg* msg) {
    nlmsghdr* nlh = nlmsg_hdr(msg);
    ifaddrmsg* ifa = (ifaddrmsg*)nlmsg_data(nlh);

    auto tb = parseNetlinkAttributes<ifaddrmsg, IFA_MAX>(nlh, ifa);
    if (!tb) return;

    std::string ip_str = tb->at(IFA_ADDRESS) ? inetToString(static_cast<in_addr*>(nla_data(tb->at(IFA_ADDRESS)))).value_or("none") : "none";
    std::string mask_str = "none";
    if (ifa->ifa_prefixlen) {
        uint32_t mask = htonl(~((1U << (32 - ifa->ifa_prefixlen)) - 1));
        in_addr mask_addr{mask};
        mask_str = inetToString(&mask_addr).value_or("none");
    }
    std::string ifname = getInterfaceName(ifa->ifa_index);

    std::string event_type = (nlh->nlmsg_type == RTM_NEWADDR) ? "add_addr" : "del_addr";
    NetworkEvent event{ifname, ip_str, "none", "none", mask_str, "00000000"};
    std::string event_message = command_processor_->getSerializer()->serializeResponse(event_type, event.toString());

    try {
        unix_server_.broadcastToAllClients(event_message);
    } catch (const std::exception& e) {
        std::cerr << "[" << getTimestamp() << "] NetworkDaemon: Failed to broadcast addr event: " << e.what() << std::endl;
    }

    std::string msg_type = (nlh->nlmsg_type == RTM_NEWADDR) ? "NEWADDR" : "DELADDR";
    std::cout << "[" << getTimestamp() << "] NetworkDaemon: " << event_message << std::endl;
}

void NetworkDaemon::handleRouteEvent(nl_msg* msg) {
    nlmsghdr* nlh = nlmsg_hdr(msg);
    rtmsg* rtm = (rtmsg*)nlmsg_data(nlh);

    auto tb = parseNetlinkAttributes<rtmsg, RTA_MAX>(nlh, rtm);
    if (!tb) return;

    std::string gw_str = tb->at(RTA_GATEWAY) ? inetToString(static_cast<in_addr*>(nla_data(tb->at(RTA_GATEWAY)))).value_or("none") : "none";
    std::string dst_str = tb->at(RTA_DST) ? inetToString(static_cast<in_addr*>(nla_data(tb->at(RTA_DST)))).value_or("default") : "default";
    std::string ifname = tb->at(RTA_OIF) ? getInterfaceName(*static_cast<int*>(nla_data(tb->at(RTA_OIF)))) : "none";

    std::string event_type = (nlh->nlmsg_type == RTM_NEWROUTE) ? "add_route" : "del_route";
    // Используем "route0" как идентификатор маршрута вместо реального имени интерфейса
    std::string name = "route0";
    std::string mask = rtm->rtm_dst_len ? std::to_string(rtm->rtm_dst_len) : "none";
    std::string flags = std::format("{:08x}", rtm->rtm_flags);

    NetworkEvent event{name, dst_str, "none" /* MAC-адрес недоступен; можно запросить через RTA_OIF */, gw_str, mask, flags};
    std::string event_message = command_processor_->getSerializer()->serializeResponse(event_type, event.toString());

    try {
        unix_server_.broadcastToAllClients(event_message);
    } catch (const std::exception& e) {
        std::cerr << "[" << getTimestamp() << "] NetworkDaemon: Failed to broadcast route event: " << e.what() << std::endl;
    }

    std::string msg_type = (nlh->nlmsg_type == RTM_NEWROUTE) ? "NEWROUTE" : "DELROUTE";
    std::cout << "[" << getTimestamp() << "] NetworkDaemon: " << event_message << std::endl;
}

NetworkDaemon::~NetworkDaemon() {
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