#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sys/event.h>
#include <zconf.h>
#include <arpa/inet.h>

const static int PORT = 8080;

int main() {

    std::ifstream ifs("../index.html");
    if (ifs.fail()) {
        std::cerr << "load failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::istreambuf_iterator<char> first(ifs);
    std::istreambuf_iterator<char> last;
    std::string html(first, last);

    int listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock == -1) {
        std::cerr << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }

    int yes = 1;
    if (setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char *) &yes, sizeof(yes)) == -1) {
        std::cerr << strerror(errno) << std::endl;
        close(listenSock);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = INADDR_ANY;
    if (bind(listenSock, (struct sockaddr *) &server, sizeof(server)) == -1) {
        std::cerr << strerror(errno) << std::endl;
        close(listenSock);
        exit(EXIT_FAILURE);
    }

    if (listen(listenSock, SOMAXCONN) == -1) {
        std::cerr << strerror(errno) << std::endl;
        close(listenSock);
        exit(EXIT_FAILURE);
    }

    int kq = kqueue();
    if (kq == -1) {
        std::cerr << strerror(errno) << std::endl;
        close(listenSock);
        exit(EXIT_FAILURE);
    }

    // register socket
    struct kevent changelist{static_cast<uintptr_t>(listenSock), EVFILT_READ, EV_ADD, 0, 0, nullptr};
    if (kevent(kq, &changelist, 1, nullptr, 0, nullptr) == -1) {
        std::cerr << strerror(errno) << std::endl;
        close(listenSock);
        exit(EXIT_FAILURE);
    }

    while (true) {
        // register event
        struct kevent eventlist{};
        struct timespec timeout{};
        timeout.tv_sec = 1;
        int event = kevent(kq, nullptr, 0, &eventlist, 1, &timeout);
        if (event == -1) {
            std::cerr << strerror(errno) << std::endl;
            break;
        } else if (event == 0) {
            continue;
        }
        std::cout << "event:" << event << " ident: " << eventlist.ident << std::endl;

        if (eventlist.ident == listenSock) {
            struct sockaddr_in client{};
            socklen_t len = sizeof(client);
            int socket = accept(listenSock, (struct sockaddr *) &client, &len);
            if (socket == -1) {
                std::cerr << strerror(errno) << std::endl;
                continue;
            }
            std::cout << "[" << socket << "] " << inet_ntoa(client.sin_addr) << "::" << ntohs(client.sin_port)
                      << std::endl;

            // register socket
            struct kevent list{static_cast<uintptr_t>(socket), EVFILT_READ, EV_ADD, 0, 0, nullptr};
            if (kevent(kq, &list, 1, nullptr, 0, nullptr) == -1) {
                std::cerr << strerror(errno) << std::endl;
                close(socket);
                break;
            }
        } else {
            auto socket = (int) eventlist.ident;
            char buf[1500];
            ssize_t size = read(socket, buf, sizeof(buf));
            if (size == -1) {
                std::cerr << strerror(errno) << std::endl;
                break;
            }

            if (size == 0) {
                std::cout << "close[" << socket << "]" << std::endl;
                close(socket);
            } else {
//                std::cout << buf << std::endl;
                std::string response = "HTTP/1.1 200 OK\r\n"
                                       "Content-Type: text/html;charset=UTF-8\r\n"
                                       "Content-Length: " + std::to_string(html.size()) + "\r\n"
                                       "\r\n" + html;
                if (write(socket, response.c_str(), response.size()) == -1) {
                    std::cerr << strerror(errno) << std::endl;
                    break;
                }
            }
        }
    }

    close(listenSock);
    return 0;
}
