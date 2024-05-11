#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <functional>
#include <list>

#include <thread>
#include <mutex>
#include <shared_mutex>

#ifdef _WIN32 // Windows NT

#include <WinSock2.h>
#include <mstcpip.h>

#else // *nix

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#endif

#include "general.h"

#ifdef _WIN32 // Windows NT
typedef int SockLen_t;
typedef SOCKADDR_IN SocketAddr_in;
typedef SOCKET Socket;
typedef u_long ka_prop_t;
#else // POSIX
typedef socklen_t SockLen_t;
typedef struct sockaddr_in SocketAddr_in;
typedef int Socket;
typedef int ka_prop_t;
#endif

// Конфигурация Keep-Alive соединения
struct KeepAliveConfig {
    ka_prop_t ka_idle = 120;
    ka_prop_t ka_intvl = 3;
    ka_prop_t ka_cnt = 5;
};

// Класс Tcp сервера
struct TcpServer {
    // Класс клиента сервера (реализация определена ниже)
    struct Client;
    // Тип обработчик данных клиента
    typedef std::function<void(DataBuffer, Client&)> handler_function_t;
    // Тип обработчика подключения/отсоединения клиента
    typedef std::function<void(Client&)> con_handler_function_t;

    // Коды статуса сервера
    enum class status : uint8_t {
        up = 0,
        err_socket_init = 1,
        err_socket_bind = 2,
        err_scoket_keep_alive = 3,
        err_socket_listening = 4,
        close = 5
    };

private:
    // Сокет сервера
    Socket serv_socket;
    // Порт сервера
    uint16_t port;
    // Код статуса
    status _status = status::close;
    // Обработчик данных от клиента
    handler_function_t handler;
    // Обработчик подключения клиента
    con_handler_function_t connect_hndl = [](Client&) {};
    // Обработчик отсоединения клиента
    con_handler_function_t disconnect_hndl = [](Client&) {};
    // Поток-обработчик подключений
    std::thread accept_handler_thread;
    // Поток ожидания данных
    std::thread data_waiter_thread;
    // Тип итератора клиента
    typedef std::list<std::unique_ptr<Client>>::iterator ClientIterator;

    // Keep-Alive конфигурация
    KeepAliveConfig ka_conf;

    // Список клиентов
    std::list<std::unique_ptr<Client>> client_list;
    // Мьютекс для синзронизации потоков подключения и ожидания данных
    std::mutex client_mutex;

    // Для систем Windows так же требуется
    // структура определяющая версию WinSocket
#ifdef _WIN32 // Windows NT
    WSAData w_data;
#endif

    // Включить Keep-Alive для сокета
    bool enableKeepAlive(Socket socket);
    // Метод обработчика подключений
    void handlingAcceptLoop();
    // Метод ожидания данных
    void waitingDataLoop();

public:
    // Упрощённый конструктор с указанием:
    // * порта
    // * обработчика данных
    // * конфигурации Keep-Alive
    TcpServer(const uint16_t port,
        handler_function_t handler,
        KeepAliveConfig ka_conf = {});
    // Конструктор с указанием:
    // * порта
    // * обработчика данных
    // * обработчика подключений
    // * обработчика отключений
    // * конфигурации Keep-Alive
    TcpServer(const uint16_t port,
        handler_function_t handler,
        con_handler_function_t connect_hndl,
        con_handler_function_t disconnect_hndl,
        KeepAliveConfig ka_conf = {});

    // Деструктор
    ~TcpServer();

    // Заменить обработчик данных
    void setHandler(handler_function_t handler);
    // Getter порта
    uint16_t getPort() const;
    // Setter порта
    uint16_t setPort(const uint16_t port);
    // Getter кода статуса сервера
    status getStatus() const { return _status; }
    // Метод запуска сервера
    status start();
    // Метод остановки сервера
    void stop();
    // Метод для входа присоединения циклических потоков сервера
    void joinLoop();

    // Исходящее подключение от сервера к другому серверу
    bool connectTo(uint32_t host, uint16_t port, con_handler_function_t connect_hndl);

    // Отправить данные всем клиентам сервера
    void sendData(const void* buffer, const size_t size);
    // Отправить данные клиенту по порту и хосту
    bool sendDataBy(uint32_t host, uint16_t port, const void* buffer, const size_t size);
    // Отключить клиента по порту и хосту
    bool disconnectBy(uint32_t host, uint16_t port);
    // Отключить всех клиентов
    void disconnectAll();
};

// Класс клиента (со стороны сервера)
struct TcpServer::Client : public TcpClientBase {
    friend struct TcpServer;

    // Мьютекс для синхронизации обработки данныз
    std::mutex access_mtx;
    // Адрес клиента
    SocketAddr_in address;
    // Сокет слиента
    Socket socket;
    // Код статуса клиента
    status _status = status::connected;

public:
    // Конструктор с указанием:
    // * сокета клиента
    // * адреса клиента
    Client(Socket socket, SocketAddr_in address);
    // Деструктор
    virtual ~Client() override;
    // Getter хоста
    virtual uint32_t getHost() const override;
    // Getter порта
    virtual uint16_t getPort() const override;
    // Getter кода статуса подключения
    virtual status getStatus() const override { return _status; }
    // Отключить клиента
    virtual status disconnect() override;
    // Получить данные от клиента
    virtual DataBuffer loadData() override;
    // Отправить данные клиенту
    virtual bool sendData(const void* buffer, const size_t size) const override;
    // Определить "сторону" клиента
    virtual SocketType getType() const override { return SocketType::server_socket; }
};

#endif // TCPSERVER_H