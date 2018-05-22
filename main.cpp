#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sys/event.h>
#include <zconf.h>
#include <arpa/inet.h>
#include <err.h>

const static int PORT = 8080;


static std::string build_response(std::string &html) {
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/html;charset=UTF-8\r\n"
           "Content-Length: " + std::to_string(html.size()) + "\r\n\r\n" + html;
}

int main() {

    std::ifstream ifs("../index.html");
    if (ifs.fail()) {
        err(EXIT_FAILURE, "load failed");
    }

    std::istreambuf_iterator<char> first(ifs);
    std::istreambuf_iterator<char> last;
    std::string html(first, last);

    int listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock == -1) {
        err(EXIT_FAILURE, "socket()");
    }

    int yes = 1;
    if (setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char *) &yes, sizeof(yes)) == -1) {
        close(listenSock);
        err(EXIT_FAILURE, "setsockopt()");
    }

    struct sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = INADDR_ANY;
    if (bind(listenSock, (struct sockaddr *) &server, sizeof(server)) == -1) {
        close(listenSock);
        err(EXIT_FAILURE, "bind()");
    }

    if (listen(listenSock, SOMAXCONN) == -1) {
        close(listenSock);
        err(EXIT_FAILURE, "listen()");
    }

    // create kqueue
    int kq = kqueue();
    if (kq == -1) {
        close(listenSock);
        err(EXIT_FAILURE, "kqueue()");
    }

    // attach the event to the kqueue.
    struct kevent changelist{static_cast<uintptr_t>(listenSock), EVFILT_READ, EV_ADD, 0, 0, nullptr};
    if (kevent(kq, &changelist, 1, nullptr, 0, nullptr) == -1) {
        close(listenSock);
        err(EXIT_FAILURE, "kevent()");
    }

    while (true) {
        // sleep until an event happens.
        struct kevent eventlist{};
        int event = kevent(kq, nullptr, 0, &eventlist, 1, nullptr);
        if (event == -1) {
            close(listenSock);
            err(EXIT_FAILURE, "kevent()");
        }
        std::cout << "event:" << event << " ident: " << eventlist.ident << std::endl;

        if (event == 0) {
            continue;
        }

        if (eventlist.ident == listenSock) {
            // create a new connected socket
            struct sockaddr_in client{};
            socklen_t len = sizeof(client);
            int socket = accept(listenSock, (struct sockaddr *) &client, &len);
            if (socket == -1) {
                warn("accept()");
                continue;
            }
            std::cout << "event:" << event << " accept:" << socket << " "
                      << inet_ntoa(client.sin_addr) << "::" << ntohs(client.sin_port) << std::endl;

            // attach the event
            struct kevent list{static_cast<uintptr_t>(socket), EVFILT_READ, EV_ADD, 0, 0, nullptr};
            if (kevent(kq, &list, 1, nullptr, 0, nullptr) == -1) {
                close(socket);
                warn("kevent()");
            }
            continue;
        }

        // read from socket
        auto socket = (int) eventlist.ident;
        char buf[1500];
        ssize_t size = read(socket, buf, sizeof(buf));
        if (size == -1) {
            close(socket);
            warn("read()");
            continue;
        }

        if (size == 0) {
            std::cout << "event:" << event << " close:" << socket << std::endl;
            close(socket);
            continue;
        }

        // write response
//        std::cout << buf << std::endl;
        std::string response = build_response(html);
        if (write(socket, response.c_str(), response.size()) == -1) {
            close(socket);
            warn("write()");
        }
    }

    close(listenSock);
    return 0;
}
