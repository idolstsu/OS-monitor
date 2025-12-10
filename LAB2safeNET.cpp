#include <iostream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

using namespace std;


class NetworkServer {
private:
    int serverSocket;// дескриптор слушающего сокета
    int port;// порт для прослушивания
    vector<int> clientSockets;// список активных клиентских сокетов
    static volatile sig_atomic_t wasSigHup;

    //маски сигналов для блокировки
    sigset_t originalMask;
    sigset_t blockedMask;

    bool stop;//флаг остановки сервера
    int maxClients;

public:
    NetworkServer(int p = 8080) {
        serverSocket = -1;// -1 означает не инициализирован
        port = p;
        maxClients = FD_SETSIZE - 1;//ограничение select() - резервируем 1 для серверного сокета
        clientSockets.reserve(maxClients);
        stop = false;

        sigemptyset(&originalMask);
        sigemptyset(&blockedMask);
    }
    //деструктор
    ~NetworkServer() {
        clean();
    }

    static void sigHupHandler(int signal) {
        wasSigHup = 1;
    }

    void registerSignalHandler() {
        struct sigaction sa;

        //получаем текущие настройки обработки SIGHUP
        if (sigaction(SIGHUP, NULL, &sa) == -1) {
            perror("sigaction get failed");
            exit(EXIT_FAILURE);
        }

        sa.sa_handler = sigHupHandler;
        sa.sa_flags |= SA_RESTART;  //автоматический перезапуск системных вызовов после обработки сигнала
        if (sigaction(SIGHUP, &sa, NULL) == -1) {
            perror("sigaction set failed");
            exit(EXIT_FAILURE);
        }
        cout << "Signal handler registered for SIGHUP" << endl;
    }


    void blockSignal() {
        sigemptyset(&blockedMask);
        sigaddset(&blockedMask, SIGHUP);

        //блокируем сигнал, сохраняя старую маску
        if (sigprocmask(SIG_BLOCK, &blockedMask, &originalMask) == -1) {
            perror("sigprocmask failed");
            exit(EXIT_FAILURE);
        }
        cout << "Signal SIGHUP blocked" << endl;
    }


    void addClient(int clientSocket) {
        if (clientSockets.size() >= maxClients) {
            cerr << "Maximum clients reached (" << maxClients << "). Connection rejected." << endl;
            const char* msg = "Server is full. Please try again later.\n";
            send(clientSocket, msg, strlen(msg), 0);
            close(clientSocket);
            return;
        }
        clientSockets.push_back(clientSocket);
        cout << "Client added. Total clients: " << clientSockets.size() << endl;
    }

    void deleteClient(int clientSocket) {
        for (auto it = clientSockets.begin(); it != clientSockets.end(); ++it) {
            if (*it == clientSocket) {
                shutdown(clientSocket, SHUT_RDWR);
                close(clientSocket);
                clientSockets.erase(it);
                cout << "Client " << clientSocket << " removed. Remaining: " << clientSockets.size() << endl;
                return;
            }
        }
        cerr << "Warning: Client not found for removal" << endl;
    }

    void createSocket() {
        int err = 0;
        int optionVal = 1;

        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket < 0) {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        err = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR,
            &optionVal, sizeof(optionVal));
        if (err < 0) {
            perror("setsockopt failed");
            close(serverSocket);
            exit(EXIT_FAILURE);
        }

        //настройка адреса сервера
        sockaddr_in serverAddress{};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_addr.s_addr = INADDR_ANY;  //принимаем соединения с любого интерфейса
        serverAddress.sin_port = htons(port);        // преобразуем порт в сетевой порядок байт

        //привязка сокета к адресу и порту
        err = bind(serverSocket, (sockaddr*)&serverAddress, sizeof(serverAddress));
        if (err < 0) {
            perror("bind failed");
            close(serverSocket);
            exit(EXIT_FAILURE);
        }

        //перевод сокета в режим прослушивания
        err = listen(serverSocket, 5);
        if (err < 0) {
            perror("listen failed");
            close(serverSocket);
            exit(EXIT_FAILURE);
        }

        cout << "Server socket created and listening on port " << port << endl;
        cout << "Maximum clients: " << maxClients << endl;
    }

    void start() {
        cout << "Server started. Waiting for connections..." << endl;
        cout << "Press Ctrl+C to stop the server" << endl;

        while (!stop) {
            fd_set readFds;
            int maxFd = serverSocket;
            int readyFds;
            FD_ZERO(&readFds);
            FD_SET(serverSocket, &readFds);

            for (int clientFd : clientSockets) {
                if (clientFd > 0) {
                    FD_SET(clientFd, &readFds);
                    if (clientFd > maxFd) {
                        maxFd = clientFd;
                    }
                }
            }


            readyFds = pselect(maxFd + 1, &readFds, NULL, NULL, NULL, &originalMask);

            if (readyFds == -1) {
                if (errno == EINTR) {
                    //pselect был прерван сигналом
                    if (wasSigHup) {
                        cout << "\n=== SIGNAL HANDLING ===" << endl;
                        cout << "SIGHUP received. Server continues working." << endl;
                        cout << "Active clients: " << clientSockets.size() << endl;
                        cout << "=======================\n" << endl;
                        wasSigHup = 0;  //сбрасываем флаг
                    }
                    continue;  //возвращаемся к началу цикла
                }
                else {
                    perror("pselect failed");
                    break;
                }
            }

            if (FD_ISSET(serverSocket, &readFds)) {
                acceptConnection();
            }

            handleClients(readFds);

        }

        cout << "\nServer shutdown complete." << endl;
    }

    int getClientCount() const { return clientSockets.size(); }
    int getPort() const { return port; }
    bool isRunning() const { return !stop; }

private:

    void clean() {
        cout << "\nCleaning up resources..." << endl;

        //закрываем все клиентские сокеты
        for (int clientFd : clientSockets) {
            if (clientFd > 0) {
                shutdown(clientFd, SHUT_RDWR);
                close(clientFd);
            }
        }
        clientSockets.clear();
        cout << "  - All client connections closed" << endl;

        //закрываем серверный сокет
        if (serverSocket >= 0) {
            shutdown(serverSocket, SHUT_RDWR);
            close(serverSocket);
            serverSocket = -1;
            cout << "  - Server socket closed" << endl;
        }

        cout << "Cleanup completed successfully." << endl;
    }

    void acceptConnection() {
        sockaddr_in clientAddress{};
        socklen_t clientLen = sizeof(clientAddress);
        int clientSocket;

        //принимаем подключение с защитой от прерываний
        do {
            clientSocket = accept(serverSocket,
                (sockaddr*)&clientAddress,
                &clientLen);
        } while (clientSocket < 0 && errno == EINTR);

        if (clientSocket < 0) {
            perror("accept failed");
            return;
        }

        //получаем информацию о клиенте
        char clientIp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddress.sin_addr, clientIp, sizeof(clientIp));
        unsigned short clientPort = ntohs(clientAddress.sin_port);

        cout << "\n=== NEW CONNECTION ===" << endl;
        cout << "Client IP: " << clientIp << ":" << clientPort << endl;
        cout << "Socket FD: " << clientSocket << endl;
        cout << "Timestamp: " << time(NULL) << endl;
        cout << "=====================\n" << endl;

        addClient(clientSocket);

        const char* welcomeMsg =
            "Welcome to the Network Server!\n"
            "Type any message and press Enter.\n"
            "The server will echo your message back.\n"
            "Type 'exit' to disconnect.\n\n";
        send(clientSocket, welcomeMsg, strlen(welcomeMsg), 0);
    }

    void handleClients(fd_set& readFds) {
        vector<int> clientsToRemove;

        for (int clientFd : clientSockets) {
            if (FD_ISSET(clientFd, &readFds)) {
                char buffer[1024];
                ssize_t bytesReceived;

                //чтение данных с защитой от прерываний
                do {
                    bytesReceived = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
                } while (bytesReceived < 0 && errno == EINTR);

                if (bytesReceived > 0) {
                    buffer[bytesReceived] = '\0';

                    string data(buffer);
                    size_t newlinePos;
                    while ((newlinePos = data.find('\n')) != string::npos) {
                        data.erase(newlinePos, 1);
                    }
                    while ((newlinePos = data.find('\r')) != string::npos) {
                        data.erase(newlinePos, 1);
                    }

                    if (data == "exit" || data == "quit") {
                        cout << "Client " << clientFd << " requested disconnect" << endl;
                        const char* goodbye = "Goodbye!\n";
                        send(clientFd, goodbye, strlen(goodbye), 0);
                        clientsToRemove.push_back(clientFd);
                        continue;
                    }

                    if (data == "stats") {
                        string stats = "Server statistics:\n"
                            "Active clients: " + to_string(clientSockets.size()) + "\n"
                            "Your FD: " + to_string(clientFd) + "\n"
                            "Server port: " + to_string(port) + "\n\n";
                        send(clientFd, stats.c_str(), stats.length(), 0);
                        continue;
                    }

                    cout << "\n=== DATA RECEIVED ===" << endl;
                    cout << "From client FD: " << clientFd << endl;
                    cout << "Bytes: " << bytesReceived << endl;
                    cout << "Content: \"" << data << "\"" << endl;
                    cout << "====================\n" << endl;

                    string echo = "Echo: " + data + "\n";
                    send(clientFd, echo.c_str(), echo.length(), 0);

                }
                else if (bytesReceived == 0) {
                    cout << "Client" << clientFd << "disconnected gracefully" << endl;
                    clientsToRemove.push_back(clientFd);

                }
                else {
                    perror("recv error");
                    clientsToRemove.push_back(clientFd);
                }
            }
        }

        for (int clientFd : clientsToRemove) {
            deleteClient(clientFd);
        }
    }
};

volatile sig_atomic_t NetworkServer::wasSigHup = 0;


int main(int argc, char* argv[]) {
    int port = 8080;

    cout << "==================================================" << endl;
    cout << "OPERATING SYSTEMS - LAB 2: NETWORK SERVER" << endl;
    cout << "Safe Handling of Network Connections and Signals" << endl;
    cout << "==================================================" << endl;

    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            cerr << "Error: Invalid port number. Using default 8080." << endl;
            port = 8080;
        }
    }

    cout << "Server configuration:" << endl;
    cout << "Port: " << port << endl;
    cout << "Max clients: " << FD_SETSIZE - 1 << endl;
    cout << "PID: " << getpid() << endl;
    cout << "To send SIGHUP: kill -HUP " << getpid() << endl;
    cout << "==================================================\n" << endl;

    try {
        NetworkServer server(port);
        server.createSocket();
        server.registerSignalHandler();
        server.blockSignal();
        server.start();
    }
    catch (const exception& e) {
        cerr << "Fatal error: " << e.what() << endl;
        return EXIT_FAILURE;
    }

    cout << "Server terminated successfully." << endl;
    return EXIT_SUCCESS;
}