#include <iostream>
#include <map>
#include <string>
#include <functional>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <unistd.h>
#include <cerrno>
#include <system_error>
#include <sys/epoll.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cstring>
#include <signal.h>
#include <fcntl.h>
#include <sstream>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>

// Путь к Unix domain socket
const char* SOCKET_PATH = "/tmp/network_daemon.sock";

// Класс EventLoop с использованием epoll
class EventLoop {
public:
    EventLoop() {
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ == -1) {
            throw std::system_error(errno, std::generic_category(), "Не удалось создать epoll");
        }
    }

    ~EventLoop() {
        close(epoll_fd_);
    }

    void add(int fd, uint32_t events, std::function<void(int, uint32_t)> handler) {
        struct epoll_event ev;
        ev.events = events;
        ev.data.fd = fd;
        handlers_[fd] = handler;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
            throw std::system_error(errno, std::generic_category(), "Не удалось добавить fd в epoll");
        }
    }

    void remove(int fd) {
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
            std::cerr << "Не удалось удалить fd из epoll: " << strerror(errno) << std::endl;
        }
        handlers_.erase(fd);
    }

    void run() {
        struct epoll_event events[10];
        while (running_) {
            int nfds = epoll_wait(epoll_fd_, events, 10, -1);
            if (nfds == -1) {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "Ошибка epoll_wait");
            }
            for (int i = 0; i < nfds; ++i) {
                int fd = events[i].data.fd;
                uint32_t ev = events[i].events;
                if (handlers_.count(fd)) {
                    handlers_[fd](fd, ev);
                }
            }
        }
    }

    void stop() {
        running_ = false;
    }

private:
    int epoll_fd_;
    std::map<int, std::function<void(int, uint32_t)>> handlers_;
    bool running_ = true;
};

class NetworkDaemon {
public:
    NetworkDaemon() : loop_() {
        // Установить обработчик SIGCHLD
        signal(SIGCHLD, [](int) {
            int status;
            pid_t pid;
            while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
                std::cout << "Процесс с PID " << pid << " завершился с кодом " << WEXITSTATUS(status) << std::endl;
            }
        });

        // Создать netlink сокет для мониторинга
        createNetlinkSocket();

        // Добавить netlink сокет в цикл событий
        loop_.add(netlink_fd_, EPOLLIN, 
            std::bind(&NetworkDaemon::handleNetlinkEvent, this, std::placeholders::_1, std::placeholders::_2));

        // Создать Unix domain socket для общения
        createUnixSocket();

        // Добавить unix сокет в цикл событий
        loop_.add(unix_fd_, EPOLLIN, 
            std::bind(&NetworkDaemon::handleUnixEvent, this, std::placeholders::_1, std::placeholders::_2));
    }

    ~NetworkDaemon() {
        // Остановить все процессы dhcpcd
        for (const auto& [ifname, pid] : dhcpcd_pids_) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
        }
        // Закрыть все клиентские соединения
        for (const auto& [fd, _] : client_handlers_) {
            loop_.remove(fd);
            close(fd);
        }
        if (netlink_fd_ != -1) {
            loop_.remove(netlink_fd_);
            close(netlink_fd_);
        }
        if (unix_fd_ != -1) {
            loop_.remove(unix_fd_);
            close(unix_fd_);
            unlink(SOCKET_PATH);
        }
    }

    void run() {
        std::cout << "Демон запущен, ожидание команд..." << std::endl;
        loop_.run();
    }

    void stop() {
        loop_.stop();
    }

private:
    EventLoop loop_;
    int netlink_fd_ = -1;
    int unix_fd_ = -1;
    std::map<int, std::string> interface_ips_; // Хранит IP-адреса по индексу интерфейса
    std::map<int, uint32_t> interface_masks_; // Хранит маски подсети по индексу интерфейса
    std::map<std::string, pid_t> dhcpcd_pids_; // Хранит PID процессов dhcpcd по имени интерфейса
    std::map<int, std::function<void(int, uint32_t)>> client_handlers_; // Хранит обработчики клиентских соединений

    void createNetlinkSocket() {
        struct sockaddr_nl sa;
        netlink_fd_ = socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK, NETLINK_ROUTE);
        if (netlink_fd_ == -1) {
            throw std::system_error(errno, std::generic_category(), "Не удалось создать сокет AF_NETLINK");
        }
        memset(&sa, 0, sizeof(sa));
        sa.nl_family = AF_NETLINK;
        sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE;
        sa.nl_pid = getpid();
        if (bind(netlink_fd_, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
            close(netlink_fd_);
            throw std::system_error(errno, std::generic_category(), "Не удалось привязать netlink сокет");
        }
        std::cout << "Netlink сокет создан и привязан, группы: RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE" << std::endl;
    }

    void handleNetlinkEvent(int fd, uint32_t events) {
        char buffer[4096];
        struct nlmsghdr* nlh;
        ssize_t len = recv(fd, buffer, sizeof(buffer), 0);
        if (len <= 0) {
            if (len == 0 || errno != EAGAIN) {
                std::cerr << "Ошибка netlink сокета: " << strerror(errno) << std::endl;
                loop_.remove(fd);
                close(fd);
                netlink_fd_ = -1;
            }
            return;
        }
        std::cout << "Получено Netlink-сообщение, длина: " << len << " байт" << std::endl;
        for (nlh = reinterpret_cast<nlmsghdr*>(buffer); NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
            if (nlh->nlmsg_type == NLMSG_DONE) {
                std::cout << "Netlink: Получено сообщение NLMSG_DONE" << std::endl;
                break;
            }
            std::cout << "Netlink: Обработка сообщения, тип=" << nlh->nlmsg_type 
                      << ", флаги=" << nlh->nlmsg_flags 
                      << ", длина=" << nlh->nlmsg_len 
                      << ", pid=" << nlh->nlmsg_pid << std::endl;
            processNetlinkMessage(nlh);
        }
    }

    void processNetlinkMessage(struct nlmsghdr* nlh) {
        switch (nlh->nlmsg_type) {
            case RTM_NEWLINK:
            case RTM_DELLINK: {
                struct ifinfomsg* ifi = reinterpret_cast<ifinfomsg*>(NLMSG_DATA(nlh));
                char ifname[IF_NAMESIZE];
                if (!if_indextoname(ifi->ifi_index, ifname)) {
                    std::cerr << "Netlink: Не удалось получить имя интерфейса для индекса " << ifi->ifi_index << std::endl;
                    return;
                }
                std::cout << "Netlink: Событие интерфейса: " 
                          << (nlh->nlmsg_type == RTM_NEWLINK ? "НОВЫЙ" : "УДАЛЕН") 
                          << ", индекс=" << ifi->ifi_index 
                          << ", имя=" << ifname 
                          << ", флаги=0x" << std::hex << ifi->ifi_flags << std::dec 
                          << " (UP=" << (ifi->ifi_flags & IFF_UP ? "yes" : "no")
                          << ", RUNNING=" << (ifi->ifi_flags & IFF_RUNNING ? "yes" : "no") << ")" << std::endl;
                break;
            }
            case RTM_NEWADDR:
            case RTM_DELADDR: {
                struct ifaddrmsg* ifa = reinterpret_cast<ifaddrmsg*>(NLMSG_DATA(nlh));
                struct rtattr* rta = IFA_RTA(ifa);
                int len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifa));
                char addr_buf[INET_ADDRSTRLEN];
                char ifname[IF_NAMESIZE];
                if (!if_indextoname(ifa->ifa_index, ifname)) {
                    std::cerr << "Netlink: Не удалось получить имя интерфейса для индекса " << ifa->ifa_index << std::endl;
                    return;
                }
                bool found_addr = false;
                while (RTA_OK(rta, len)) {
                    if (rta->rta_type == IFA_LOCAL) {
                        inet_ntop(AF_INET, RTA_DATA(rta), addr_buf, sizeof(addr_buf));
                        std::string addr_str(addr_buf);
                        int if_index = ifa->ifa_index;
                        found_addr = true;
                        if (nlh->nlmsg_type == RTM_NEWADDR) {
                            std::cout << "Netlink: Назначен новый IP: " << addr_str 
                                      << ", интерфейс=" << if_index 
                                      << " (" << ifname << ")" 
                                      << ", маска=" << ifa->ifa_prefixlen << std::endl;
                            interface_ips_[if_index] = addr_str;
                            interface_masks_[if_index] = ifa->ifa_prefixlen;
                        } else {
                            std::cout << "Netlink: IP удален: " << addr_str 
                                      << ", интерфейс=" << if_index 
                                      << " (" << ifname << ")" << std::endl;
                            interface_ips_.erase(if_index);
                            interface_masks_.erase(if_index);
                        }
                    }
                    rta = RTA_NEXT(rta, len);
                }
                if (!found_addr) {
                    std::cout << "Netlink: Сообщение " << (nlh->nlmsg_type == RTM_NEWADDR ? "RTM_NEWADDR" : "RTM_DELADDR")
                              << " для интерфейса " << ifname << " не содержит IFA_LOCAL" << std::endl;
                }
                break;
            }
            case RTM_NEWROUTE:
            case RTM_DELROUTE: {
                struct rtmsg* rtm = reinterpret_cast<rtmsg*>(NLMSG_DATA(nlh));
                if (rtm->rtm_family == AF_INET) {
                    struct rtattr* rta = RTM_RTA(rtm);
                    int len = RTM_PAYLOAD(nlh);
                    char addr_buf[INET_ADDRSTRLEN];
                    int oif_index = -1;
                    bool found_gateway = false;
                    while (RTA_OK(rta, len)) {
                        if (rta->rta_type == RTA_GATEWAY) {
                            inet_ntop(AF_INET, RTA_DATA(rta), addr_buf, sizeof(addr_buf));
                            found_gateway = true;
                            std::cout << "Netlink: " << (nlh->nlmsg_type == RTM_NEWROUTE ? "Новый шлюз: " : "Шлюз удален: ") 
                                      << addr_buf 
                                      << ", таблица=" << (int)rtm->rtm_table 
                                      << ", протокол=" << (int)rtm->rtm_protocol << std::endl;
                        } else if (rta->rta_type == RTA_OIF) {
                            oif_index = *(int*)RTA_DATA(rta);
                            char ifname[IF_NAMESIZE];
                            if (!if_indextoname(oif_index, ifname)) {
                                std::cerr << "Netlink: Не удалось получить имя интерфейса для OIF индекса " << oif_index << std::endl;
                            } else {
                                std::cout << "Netlink: Выходной интерфейс для маршрута: индекс=" << oif_index << ", имя=" << ifname << std::endl;
                            }
                        }
                        rta = RTA_NEXT(rta, len);
                    }
                    if (!found_gateway) {
                        std::cout << "Netlink: Сообщение " << (nlh->nlmsg_type == RTM_NEWROUTE ? "RTM_NEWROUTE" : "RTM_DELROUTE")
                                  << " не содержит RTA_GATEWAY" << std::endl;
                    }
                } else {
                    std::cout << "Netlink: Игнорируется маршрут с семейством " << (int)rtm->rtm_family << " (не AF_INET)" << std::endl;
                }
                break;
            }
            default:
                std::cout << "Netlink: Необработанное сообщение, тип=" << nlh->nlmsg_type << std::endl;
                break;
        }
    }

    void createUnixSocket() {
        struct sockaddr_un addr;
        unix_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (unix_fd_ == -1) {
            throw std::system_error(errno, std::generic_category(), "Не удалось создать unix сокет");
        }
        unlink(SOCKET_PATH); // Удалить старый сокет, если существует
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
        if (bind(unix_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            close(unix_fd_);
            throw std::system_error(errno, std::generic_category(), "Не удалось привязать unix сокет");
        }
        if (listen(unix_fd_, 5) == -1) {
            close(unix_fd_);
            throw std::system_error(errno, std::generic_category(), "Не удалось прослушивать unix сокет");
        }
        chmod(SOCKET_PATH, 0666); // Установить права доступа
        std::cout << "Unix сокет создан и прослушивается по пути " << SOCKET_PATH << std::endl;
    }

    void handleUnixEvent(int fd, uint32_t events) {
        struct sockaddr_un addr;
        socklen_t addrlen = sizeof(addr);
        int client_fd = accept(fd, (struct sockaddr*)&addr, &addrlen);
        if (client_fd == -1) {
            std::cerr << "Ошибка принятия соединения: " << strerror(errno) << std::endl;
            return;
        }
        std::cout << "Новое клиентское соединение, fd=" << client_fd << std::endl;

        // Установить сокет в неблокирующий режим
        int flags = fcntl(client_fd, F_GETFL, 0);
        if (flags == -1) {
            std::cerr << "Ошибка получения флагов сокета: " << strerror(errno) << std::endl;
            close(client_fd);
            return;
        }
        if (fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            std::cerr << "Ошибка установки неблокирующего режима: " << strerror(errno) << std::endl;
            close(client_fd);
            return;
        }

        // Добавить клиентский сокет в epoll
        loop_.add(client_fd, EPOLLIN, 
            std::bind(&NetworkDaemon::handleClientEvent, this, std::placeholders::_1, std::placeholders::_2));
        client_handlers_[client_fd] = std::bind(&NetworkDaemon::handleClientEvent, this, std::placeholders::_1, std::placeholders::_2);
    }

    void handleClientEvent(int client_fd, uint32_t events) {
        char buffer[1024];
        ssize_t len = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (len <= 0) {
            if (len == 0) {
                std::cout << "Клиент (fd=" << client_fd << ") закрыл соединение" << std::endl;
                loop_.remove(client_fd);
                client_handlers_.erase(client_fd);
                close(client_fd);
                return;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            std::cerr << "Ошибка чтения из клиентского сокета (fd=" << client_fd << "): " << strerror(errno) << std::endl;
            loop_.remove(client_fd);
            client_handlers_.erase(client_fd);
            close(client_fd);
            return;
        }
        buffer[len] = '\0';
        std::cout << "Получена команда от клиента (fd=" << client_fd << "): " << buffer << std::endl;
        processCommand(buffer, client_fd);
    }

    void processCommand(const char* command, int client_fd) {
        std::string cmd(command);
        if (cmd.find("set_static ") == 0) {
            std::string params = cmd.substr(11);
            size_t space1 = params.find(' ');
            size_t space2 = params.find(' ', space1 + 1);
            if (space1 == std::string::npos || space2 == std::string::npos) {
                sendResponse(client_fd, "Неверный формат команды set_static");
                return;
            }
            std::string ifname = params.substr(0, space1);
            std::string ip_mask = params.substr(space1 + 1, space2 - space1 - 1);
            std::string gateway = params.substr(space2 + 1);
            stopDhcpcd(ifname);
            setStaticIP(ifname, ip_mask, gateway);
            sendResponse(client_fd, "Статическая конфигурация применена");
        } else if (cmd.find("set_dynamic ") == 0) {
            std::string ifname = cmd.substr(12);
            std::string result = setDynamicIP(ifname);
            sendResponse(client_fd, result.c_str());
        } else if (cmd == "get_status") {
            std::string status;
            for (const auto& [index, ip] : interface_ips_) {
                char ifname[IF_NAMESIZE];
                if_indextoname(index, ifname);
                std::string mask = prefixToMask(interface_masks_[index]);
                std::string gateway = getGateway(index);
                std::string dns = getDNS();
                status += std::string(ifname) + ": IP=" + ip + ", Mask=" + mask + 
                          ", Gateway=" + gateway + ", DNS=" + dns + "\n";
            }
            sendResponse(client_fd, status.empty() ? "Нет активных IP" : status.c_str());
        } else if (cmd.find("stop_dhcpcd ") == 0) {
            std::string ifname = cmd.substr(12);
            stopDhcpcd(ifname);
            sendResponse(client_fd, "dhcpcd остановлен для интерфейса");
        } else {
            sendResponse(client_fd, "Неизвестная команда");
        }
    }

    void sendResponse(int client_fd, const char* response) {
        std::cout << "Отправка ответа клиенту (fd=" << client_fd << "): " << response << std::endl;
        if (send(client_fd, response, strlen(response), 0) < 0) {
            std::cerr << "Ошибка отправки ответа клиенту (fd=" << client_fd << "): " << strerror(errno) << std::endl;
            loop_.remove(client_fd);
            client_handlers_.erase(client_fd);
            close(client_fd);
        }
    }

    std::string prefixToMask(uint32_t prefix) {
        uint32_t mask = (0xFFFFFFFF << (32 - prefix)) & 0xFFFFFFFF;
        char mask_buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &mask, mask_buf, sizeof(mask_buf));
        return std::string(mask_buf);
    }

    std::string getGateway(int if_index) {
        struct {
            struct nlmsghdr nlh;
            struct rtmsg rt;
            char buf[1024];
        } req;
        memset(&req, 0, sizeof(req));
        req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
        req.nlh.nlmsg_type = RTM_GETROUTE;
        req.nlh.nlmsg_flags = NLM_F_REQUEST;
        req.nlh.nlmsg_pid = getpid();
        req.rt.rtm_family = AF_INET;

        struct rtattr* rta = (struct rtattr*)((char*)&req + NLMSG_LENGTH(sizeof(struct rtmsg)));
        rta->rta_type = RTA_OIF;
        rta->rta_len = RTA_LENGTH(sizeof(int));
        *(int*)RTA_DATA(rta) = if_index;
        req.nlh.nlmsg_len += rta->rta_len;

        struct sockaddr_nl sa;
        memset(&sa, 0, sizeof(sa));
        sa.nl_family = AF_NETLINK;
        sa.nl_pid = 0; // Ядро

        std::cout << "Отправка RTM_GETROUTE для интерфейса " << if_index << std::endl;
        if (sendto(netlink_fd_, &req, req.nlh.nlmsg_len, 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
            std::cerr << "Ошибка отправки RTM_GETROUTE: " << strerror(errno) << std::endl;
            return "Неизвестно";
        }

        char buffer[4096];
        ssize_t len = recv(netlink_fd_, buffer, sizeof(buffer), 0);
        if (len <= 0) {
            std::cerr << "Ошибка получения ответа RTM_GETROUTE: " << strerror(errno) << std::endl;
            return "Неизвестно";
        }

        std::cout << "Получен ответ RTM_GETROUTE, длина: " << len << " байт" << std::endl;
        for (struct nlmsghdr* nlh = (struct nlmsghdr*)buffer; NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
            if (nlh->nlmsg_type == NLMSG_DONE) {
                std::cout << "RTM_GETROUTE: Получено сообщение NLMSG_DONE" << std::endl;
                break;
            }
            if (nlh->nlmsg_type == RTM_NEWROUTE) {
                struct rtmsg* rtm = (struct rtmsg*)NLMSG_DATA(nlh);
                if (rtm->rtm_family == AF_INET) {
                    struct rtattr* rta = RTM_RTA(rtm);
                    int rta_len = RTM_PAYLOAD(nlh);
                    char addr_buf[INET_ADDRSTRLEN];
                    while (RTA_OK(rta, rta_len)) {
                        if (rta->rta_type == RTA_GATEWAY) {
                            inet_ntop(AF_INET, RTA_DATA(rta), addr_buf, sizeof(addr_buf));
                            std::cout << "RTM_GETROUTE: Найден шлюз: " << addr_buf << std::endl;
                            return std::string(addr_buf);
                        }
                        rta = RTA_NEXT(rta, rta_len);
                    }
                    std::cout << "RTM_GETROUTE: Сообщение RTM_NEWROUTE не содержит RTA_GATEWAY" << std::endl;
                }
            }
        }
        std::cout << "RTM_GETROUTE: Шлюз не найден" << std::endl;
        return "Неизвестно";
    }

    std::string getDNS() {
        std::ifstream resolv_file("/etc/resolv.conf");
        std::vector<std::string> dns_servers;
        std::string line;
        while (std::getline(resolv_file, line)) {
            if (line.find("nameserver ") == 0) {
                std::string dns = line.substr(11);
                dns_servers.push_back(dns);
            }
        }
        if (dns_servers.empty()) {
            std::cout << "getDNS: Файл /etc/resolv.conf пуст или не содержит nameserver" << std::endl;
            return "Неизвестно";
        }
        std::string result;
        for (size_t i = 0; i < dns_servers.size(); ++i) {
            result += dns_servers[i];
            if (i < dns_servers.size() - 1) result += ",";
        }
        std::cout << "getDNS: Найдены DNS-серверы: " << result << std::endl;
        return result;
    }

    bool checkInterfaceUp(const std::string& ifname) {
        int idx = if_nametoindex(ifname.c_str());
        if (idx == 0) {
            std::cerr << "checkInterfaceUp: Интерфейс не найден: " << ifname << std::endl;
            return false;
        }

        struct {
            struct nlmsghdr nlh;
            struct ifinfomsg ifi;
            char buf[1024];
        } req;
        memset(&req, 0, sizeof(req));
        req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
        req.nlh.nlmsg_type = RTM_GETLINK;
        req.nlh.nlmsg_flags = NLM_F_REQUEST;
        req.nlh.nlmsg_pid = getpid();
        req.ifi.ifi_index = idx;

        struct sockaddr_nl sa;
        memset(&sa, 0, sizeof(sa));
        sa.nl_family = AF_NETLINK;
        sa.nl_pid = 0; // Ядро

        std::cout << "checkInterfaceUp: Отправка RTM_GETLINK для интерфейса " << ifname << " (индекс=" << idx << ")" << std::endl;
        if (sendto(netlink_fd_, &req, req.nlh.nlmsg_len, 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
            std::cerr << "checkInterfaceUp: Ошибка отправки RTM_GETLINK: " << strerror(errno) << std::endl;
            return false;
        }

        char buffer[4096];
        ssize_t len = recv(netlink_fd_, buffer, sizeof(buffer), 0);
        if (len <= 0) {
            std::cerr << "checkInterfaceUp: Ошибка получения ответа RTM_GETLINK: " << strerror(errno) << std::endl;
            return false;
        }

        std::cout << "checkInterfaceUp: Получен ответ RTM_GETLINK, длина: " << len << " байт" << std::endl;
        for (struct nlmsghdr* nlh = (struct nlmsghdr*)buffer; NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
            if (nlh->nlmsg_type == NLMSG_DONE) {
                std::cout << "checkInterfaceUp: Получено сообщение NLMSG_DONE" << std::endl;
                break;
            }
            if (nlh->nlmsg_type == RTM_NEWLINK) {
                struct ifinfomsg* ifi = (struct ifinfomsg*)NLMSG_DATA(nlh);
                bool is_up = (ifi->ifi_flags & IFF_UP) && (ifi->ifi_flags & IFF_RUNNING);
                std::cout << "checkInterfaceUp: Интерфейс " << ifname 
                          << ", флаги=0x" << std::hex << ifi->ifi_flags << std::dec 
                          << ", UP=" << (ifi->ifi_flags & IFF_UP ? "yes" : "no")
                          << ", RUNNING=" << (ifi->ifi_flags & IFF_RUNNING ? "yes" : "no")
                          << ", результат=" << (is_up ? "активен" : "не активен") << std::endl;
                return is_up;
            }
        }
        std::cout << "checkInterfaceUp: Ответ RTM_GETLINK не содержит RTM_NEWLINK" << std::endl;
        return false;
    }

    void checkCurrentIPs(const std::string& ifname) {
        int idx = if_nametoindex(ifname.c_str());
        if (idx == 0) {
            std::cerr << "checkCurrentIPs: Интерфейс не найден: " << ifname << std::endl;
            return;
        }

        struct {
            struct nlmsghdr nlh;
            struct ifaddrmsg ifa;
            char buf[1024];
        } req;
        memset(&req, 0, sizeof(req));
        req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
        req.nlh.nlmsg_type = RTM_GETADDR;
        req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
        req.nlh.nlmsg_pid = getpid();
        req.ifa.ifa_family = AF_INET;
        req.ifa.ifa_index = idx;

        struct sockaddr_nl sa;
        memset(&sa, 0, sizeof(sa));
        sa.nl_family = AF_NETLINK;
        sa.nl_pid = 0; // Ядро

        std::cout << "checkCurrentIPs: Отправка RTM_GETADDR для интерфейса " << ifname << " (индекс=" << idx << ")" << std::endl;
        if (sendto(netlink_fd_, &req, req.nlh.nlmsg_len, 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
            std::cerr << "checkCurrentIPs: Ошибка отправки RTM_GETADDR: " << strerror(errno) << std::endl;
            return;
        }

        char buffer[4096];
        ssize_t len = recv(netlink_fd_, buffer, sizeof(buffer), 0);
        if (len <= 0) {
            std::cerr << "checkCurrentIPs: Ошибка получения ответа RTM_GETADDR: " << strerror(errno) << std::endl;
            return;
        }

        std::cout << "checkCurrentIPs: Получен ответ RTM_GETADDR, длина: " << len << " байт" << std::endl;
        bool found = false;
        for (struct nlmsghdr* nlh = (struct nlmsghdr*)buffer; NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
            if (nlh->nlmsg_type == NLMSG_DONE) {
                std::cout << "checkCurrentIPs: Получено сообщение NLMSG_DONE" << std::endl;
                break;
            }
            if (nlh->nlmsg_type == RTM_NEWADDR) {
                struct ifaddrmsg* ifa = (struct ifaddrmsg*)NLMSG_DATA(nlh);
                if (ifa->ifa_index == idx) {
                    struct rtattr* rta = IFA_RTA(ifa);
                    int rta_len = IFA_PAYLOAD(nlh);
                    char addr_buf[INET_ADDRSTRLEN];
                    while (RTA_OK(rta, rta_len)) {
                        if (rta->rta_type == IFA_LOCAL) {
                            inet_ntop(AF_INET, RTA_DATA(rta), addr_buf, sizeof(addr_buf));
                            std::cout << "checkCurrentIPs: Текущий IP: " << addr_buf << ", маска=" << ifa->ifa_prefixlen << " для " << ifname << std::endl;
                            found = true;
                        }
                        rta = RTA_NEXT(rta, rta_len);
                    }
                }
            }
        }
        if (!found) {
            std::cout << "checkCurrentIPs: Нет IP-адресов для " << ifname << std::endl;
        }
    }

    bool waitForDhcpcd(const std::string& ifname, int timeout_seconds = 60) {
        int if_index = if_nametoindex(ifname.c_str());
        if (if_index == 0) {
            std::cerr << "waitForDhcpcd: Интерфейс не найден: " << ifname << std::endl;
            return false;
        }

        auto start_time = std::chrono::steady_clock::now();
        std::cout << "waitForDhcpcd: Ожидание IP для " << ifname << " (индекс=" << if_index << "), тайм-аут=" << timeout_seconds << " секунд" << std::endl;
        while (true) {
            if (interface_ips_.count(if_index)) {
                std::cout << "waitForDhcpcd: IP получен для " << ifname << ": " << interface_ips_[if_index] << std::endl;
                return true;
            }
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
            if (elapsed % 5 == 0) { // Логировать каждые 5 секунд
                std::cout << "waitForDhcpcd: Прошло " << elapsed << " секунд, IP для " << ifname << " всё ещё не получен" << std::endl;
            }
            if (elapsed >= timeout_seconds) {
                std::cerr << "waitForDhcpcd: Тайм-аут ожидания IP для " << ifname << " (" << timeout_seconds << " секунд)" << std::endl;
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    void setStaticIP(const std::string& ifname, const std::string& ip_mask, const std::string& gateway) {
        int idx = if_nametoindex(ifname.c_str());
        if (idx == 0) {
            std::cerr << "setStaticIP: Интерфейс не найден: " << ifname << std::endl;
            return;
        }

        std::cout << "setStaticIP: Установка статического IP для " << ifname << ": " << ip_mask << ", шлюз=" << gateway << std::endl;
        removeIP(ifname);

        struct sockaddr_nl sa;
        struct {
            struct nlmsghdr nlh;
            struct ifaddrmsg ifa;
            char attrbuf[512];
        } req;
        memset(&req, 0, sizeof(req));
        req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
        req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL;
        req.nlh.nlmsg_type = RTM_NEWADDR;
        req.nlh.nlmsg_pid = getpid();
        req.ifa.ifa_family = AF_INET;
        req.ifa.ifa_index = idx;
        req.ifa.ifa_prefixlen = std::stoi(ip_mask.substr(ip_mask.find('/') + 1));

        struct rtattr* rta = (struct rtattr*)((char*)&req + NLMSG_LENGTH(sizeof(struct ifaddrmsg)));
        rta->rta_type = IFA_ADDRESS;
        rta->rta_len = RTA_LENGTH(4);
        inet_pton(AF_INET, ip_mask.substr(0, ip_mask.find('/')).c_str(), RTA_DATA(rta));
        req.nlh.nlmsg_len += rta->rta_len;

        rta = RTA_NEXT(rta, req.nlh.nlmsg_len);
        rta->rta_type = IFA_LOCAL;
        rta->rta_len = RTA_LENGTH(4);
        inet_pton(AF_INET, ip_mask.substr(0, ip_mask.find('/')).c_str(), RTA_DATA(rta));
        req.nlh.nlmsg_len += rta->rta_len;

        memset(&sa, 0, sizeof(sa));
        sa.nl_family = AF_NETLINK;
        sa.nl_pid = 0; // Ядро
        std::cout << "setStaticIP: Отправка RTM_NEWADDR для IP=" << ip_mask.substr(0, ip_mask.find('/')) << std::endl;
        if (sendto(netlink_fd_, &req, req.nlh.nlmsg_len, 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
            std::cerr << "setStaticIP: Ошибка отправки netlink для IP: " << strerror(errno) << std::endl;
        }

        addGateway(gateway, idx);
    }

    void addGateway(const std::string& gateway, int idx) {
        struct sockaddr_nl sa;
        struct {
            struct nlmsghdr nlh;
            struct rtmsg rt;
            char attrbuf[512];
        } req;
        memset(&req, 0, sizeof(req));
        req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
        req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL;
        req.nlh.nlmsg_type = RTM_NEWROUTE;
        req.nlh.nlmsg_pid = getpid();
        req.rt.rtm_family = AF_INET;
        req.rt.rtm_table = RT_TABLE_MAIN;
        req.rt.rtm_scope = RT_SCOPE_UNIVERSE;
        req.rt.rtm_protocol = RTPROT_BOOT;
        req.rt.rtm_type = RTN_UNICAST;

        struct rtattr* rta = (struct rtattr*)((char*)&req + NLMSG_LENGTH(sizeof(struct rtmsg)));
        rta->rta_type = RTA_GATEWAY;
        rta->rta_len = RTA_LENGTH(4);
        inet_pton(AF_INET, gateway.c_str(), RTA_DATA(rta));
        req.nlh.nlmsg_len += rta->rta_len;

        rta = RTA_NEXT(rta, req.nlh.nlmsg_len);
        rta->rta_type = RTA_OIF;
        rta->rta_len = RTA_LENGTH(4);
        *(int*)RTA_DATA(rta) = idx;
        req.nlh.nlmsg_len += rta->rta_len;

        char ifname[IF_NAMESIZE];
        if_indextoname(idx, ifname);
        std::cout << "addGateway: Отправка RTM_NEWROUTE для шлюза=" << gateway << ", интерфейс=" << ifname << std::endl;
        memset(&sa, 0, sizeof(sa));
        sa.nl_family = AF_NETLINK;
        sa.nl_pid = 0;
        if (sendto(netlink_fd_, &req, req.nlh.nlmsg_len, 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
            std::cerr << "addGateway: Ошибка отправки netlink для шлюза: " << strerror(errno) << std::endl;
        }
    }

    void removeIP(const std::string& ifname) {
        std::cout << "removeIP: Удаление существующих IP для " << ifname << " (заглушка)" << std::endl;
    }

    std::string setDynamicIP(const std::string& ifname) {
        std::cout << "setDynamicIP: Запуск обработки для интерфейса " << ifname << std::endl;
        // Проверить текущие IP-адреса
        checkCurrentIPs(ifname);

        // Проверить, активен ли интерфейс
        if (!checkInterfaceUp(ifname)) {
            std::cerr << "setDynamicIP: Интерфейс " << ifname << " не активен или не существует" << std::endl;
            return "Интерфейс " + ifname + " не активен или не существует";
        }

        // Остановить существующий dhcpcd, если запущен
        stopDhcpcd(ifname);

        // Запустить dhcpcd с отладочным режимом и без перехода в фон
        pid_t pid = fork();
        if (pid == -1) {
            std::cerr << "setDynamicIP: Ошибка fork для dhcpcd: " << strerror(errno) << std::endl;
            return "Ошибка запуска dhcpcd";
        }
        if (pid == 0) {
            // Дочерний процесс
            int fd = open("/tmp/dhcpcd.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd == -1) {
                std::cerr << "setDynamicIP: Ошибка открытия /tmp/dhcpcd.log: " << strerror(errno) << std::endl;
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
            std::cout << "setDynamicIP: Запуск dhcpcd -d -B " << ifname << std::endl;
            execlp("dhcpcd", "dhcpcd", "-d", "-B", ifname.c_str(), nullptr);
            std::cerr << "setDynamicIP: Ошибка запуска dhcpcd: " << strerror(errno) << std::endl;
            exit(1);
        } else {
            // Родительский процесс
            dhcpcd_pids_[ifname] = pid;
            std::cout << "setDynamicIP: Запущен dhcpcd для " << ifname << " с PID " << pid << std::endl;

            // Ожидать получения IP
            if (waitForDhcpcd(ifname, 60)) {
                int if_index = if_nametoindex(ifname.c_str());
                std::string ip = interface_ips_.count(if_index) ? interface_ips_[if_index] : "Неизвестно";
                std::string mask = interface_ips_.count(if_index) ? prefixToMask(interface_masks_[if_index]) : "Неизвестно";
                std::string gateway = getGateway(if_index);
                std::string dns = getDNS();
                std::string result = ifname + ": IP=" + ip + ", Mask=" + mask + ", Gateway=" + gateway + ", DNS=" + dns;
                std::cout << "setDynamicIP: Успешно получены параметры: " << result << std::endl;
                return result;
            } else {
                std::cerr << "setDynamicIP: Не удалось получить IP для " << ifname << std::endl;
                return "Не удалось получить IP для " + ifname;
            }
        }
    }

    void stopDhcpcd(const std::string& ifname) {
        if (dhcpcd_pids_.count(ifname)) {
            pid_t pid = dhcpcd_pids_[ifname];
            std::cout << "stopDhcpcd: Остановка dhcpcd для " << ifname << " с PID " << pid << std::endl;
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
            dhcpcd_pids_.erase(ifname);
            std::cout << "stopDhcpcd: dhcpcd остановлен для " << ifname << std::endl;
        } else {
            std::cout << "stopDhcpcd: Нет запущенного dhcpcd для " << ifname << std::endl;
        }
    }
};

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