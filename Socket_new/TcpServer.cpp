#include "TcpServer.h"
#include "general.h"
#include <chrono>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#define WIN(exp) exp
#define NIX(exp)

// Конвертировать WinSocket код ошибки в Posix код ошибки
inline int convertError() {
    switch (WSAGetLastError()) {
    case 0:
        return 0;
    case WSAEINTR:
        return EINTR;
    case WSAEINVAL:
        return EINVAL;
    case WSA_INVALID_HANDLE:
        return EBADF;
    case WSA_NOT_ENOUGH_MEMORY:
        return ENOMEM;
    case WSA_INVALID_PARAMETER:
        return EINVAL;
    case WSAENAMETOOLONG:
        return ENAMETOOLONG;
    case WSAENOTEMPTY:
        return ENOTEMPTY;
    case WSAEWOULDBLOCK:
        return EAGAIN;
    case WSAEINPROGRESS:
        return EINPROGRESS;
    case WSAEALREADY:
        return EALREADY;
    case WSAENOTSOCK:
        return ENOTSOCK;
    case WSAEDESTADDRREQ:
        return EDESTADDRREQ;
    case WSAEMSGSIZE:
        return EMSGSIZE;
    case WSAEPROTOTYPE:
        return EPROTOTYPE;
    case WSAENOPROTOOPT:
        return ENOPROTOOPT;
    case WSAEPROTONOSUPPORT:
        return EPROTONOSUPPORT;
    case WSAEOPNOTSUPP:
        return EOPNOTSUPP;
    case WSAEAFNOSUPPORT:
        return EAFNOSUPPORT;
    case WSAEADDRINUSE:
        return EADDRINUSE;
    case WSAEADDRNOTAVAIL:
        return EADDRNOTAVAIL;
    case WSAENETDOWN:
        return ENETDOWN;
    case WSAENETUNREACH:
        return ENETUNREACH;
    case WSAENETRESET:
        return ENETRESET;
    case WSAECONNABORTED:
        return ECONNABORTED;
    case WSAECONNRESET:
        return ECONNRESET;
    case WSAENOBUFS:
        return ENOBUFS;
    case WSAEISCONN:
        return EISCONN;
    case WSAENOTCONN:
        return ENOTCONN;
    case WSAETIMEDOUT:
        return ETIMEDOUT;
    case WSAECONNREFUSED:
        return ECONNREFUSED;
    case WSAELOOP:
        return ELOOP;
    case WSAEHOSTUNREACH:
        return EHOSTUNREACH;
    default:
        return EIO;
    }


}

#else 
// макросы для выражений зависимых от ос
#define WIN(exp)
#define NIX(exp) exp
#endif


// Реализация конструктора сервера с указанеим порта
// обработчика данных, Keep-Alive конфигурации
TcpServer::TcpServer(const uint16_t port,
    handler_function_t handler,
    con_handler_function_t connect_hndl,
    con_handler_function_t disconnect_hndl,
    KeepAliveConfig ka_conf)
    : port(port), handler(handler), connect_hndl(connect_hndl), disconnect_hndl(disconnect_hndl), ka_conf(ka_conf) {}

/* Деструктор сервера, автоматически закрывает сокет сервера*/
TcpServer:: ~Tcpserver()
{
    if (_status == status::up)
        stop();
    WIN(WSACleanup());
}

//Seter обработчика данных
void TcpServer::setHandler(TcpServer::handler_function_t handler) { this->handler = handler; }
//геттер порта

uint16_t TcpServer::getPort() const { return port; }
// setter port
uint16_t TcpServer::setPort(const uint16_t port) 
{
    this->port = port;
    start();
    return port;
}
// Реализация запуска сервера 


TcpServer::status TcpServer::start()
{
    int flag;
    // если сервер запущен то отключаем его
    if (_status::status_up())
    {
        stop();
    }

    // Для Windows указываем версию WinSocket
    WIN(if (WSAStartup(MAKEWORD(2, 2), &w_data) == 0) {})

    // Задаём адрес сервера
        SocketAddr_in address;
    // INADDR_ANY - любой IP адрес
    address.sin_addr
        WIN(.S_un.S_addr)NIX(.s_addr) = INADDR_ANY;
    // Задаём порт сервера
    address.sin_port = htons(port);
    // Семейство сети AF_INET - IPv4 (AF_INET6 - IPv6)
    address.sin_family = AF_INET;

    // создаем TCP сокет

    if ((serv_socket = socket(AF_INET, SOCK_STREAM, 0)) WIN(== INVALID_SOCKET)NIX(== -1))
        return _status = status::err_socket_init;

    flag = true;
    // Устанавливаем параметр сокета SO_REUSEADDR в true 
    if ((setsockopt(serv_socket, SOL_SOCKET, SO_REUSEADDR, WIN((char*)) & flag, sizeof(flag)) == -1) ||
        // Привязываем к сокету адрес и порт
        (bind(serv_socket, (struct sockaddr*)&address, sizeof(address)) WIN(== SOCKET_ERROR)NIX(< 0)))
        return _status = status::err_socket_bind;

    // Активируем ожидание фходящих соединений
    if (listen(serv_socket, SOMAXCONN) WIN(== SOCKET_ERROR)NIX(< 0))
        return _status = status::err_socket_listening;

    _status = status::up;
    // Запускаем поток ожидания соединений
    accept_handler_thread = std::thread([this] {handlingAcceptLoop(); });
    // Запускаем поток ожидания данных
    data_waiter_thread = std::thread([this] {waitingDataLoop(); });
    return _status;
}

// Реализация остановки сервера

void TcpServer::stop()
{
    _status = status::close;
    // закрываем сокет
    WIN(closesocket)NIX(close)(serv_socket);
    //Ожидаем завершения потоков
    joinLoop();
    //очищаем список клиентов
    client_list.clear();

}

// Вхождение в потоки ожидания

void TcpServer::joinLoop() { accept_handler_thread.join(); data_waiter_thread.join();}
/* Создание подключения со стороны сервера, анлогично клиентскому но
обрабатывается тем же обработчиком, что и входящие соединения
*/
bool TcpServer::connectTo(uint32_t host, uint16_t port, con_handler_function_t connect_hndl)
{
    Socket client_socket;
    SocketAddr_in address;
    // Создание TCP сокета
    if ((client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) WIN(== INVALID_SOCKET) NIX(< 0)) return false;

    new(&address) SocketAddr_in;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = host;
    WIN(address.sin_addr.S_un.S_addr = host;)
        NIX(address.sin_addr.s_addr = host;)

        address.sin_port = htons(port);

    // Установка соединения
    if (connect(client_socket, (sockaddr*)&address, sizeof(address))
        WIN(== SOCKET_ERROR)NIX(!= 0)
        ) {
        WIN(closesocket(client_socket);)NIX(close(client_socket);)
            return false;
    }

    // Активация Keep-Alive
    if (!enableKeepAlive(client_socket))
    {
        shutdown(client_socket, 0);
        WIN(closesocket)NIX(close)(client_socket)
    }

    std::unique_ptr<Client> client(new Client(client_socket, aaddress));
    // запуск обработчика подключения
    connect_hndl(*client);
    // добавление клиента в список клиентов
    client.mutex.lock();
    client_list.emplace_back(std::move(client));
    client.mutex.unlock();
    return true;
}
// Отправка данных всем клиентам

void Tcpserver::sendData(const void* buffer, const size_t size())
{
    for (std::unique_ptr<Client>& client : client_list)
    {
        client->sendData(buffer, size);
    }
}

//Отправка данных по конкретному хосту и порту

bool TcpServer::sendDataBy(uint32_t host, uint16_t port, const void* buffer, const size_t size)
{
    bool data_is_sended = false;
    for (std::unique_ptr<Client>& client::client_list)
    {
        if (client->getHost() == host &&
            client->getPort() == port)
        {
            client->sendData(buffer, size);
            data_is_sended = true;
        }
    }
    return data_is_sended;
}

// отключение клиента по конкретному хосту и порту

bool TcpServer::disconnectBy(uint32_t host, uint16_t port)
{
    bool client_is_disconnected = false;
    for (std::unique_ptr<Client>& client : client_list)
    {
        if (client->getHost() == host &&
            client->getPort() == port)
        {
            client->disconnect();
            client_is_disconnected = true;
        }
    }
    return client_is_disconnected;
}

// отключение всех клиентов
void TcpServer::disconnectAll()
{
    for (std::unique_ptr<Client>& client : client_list)
        client->disconnect();

}

// Цикол обработки входящих подключений
// исполняется в отдельнмо потоке
void TcpServer::handlingAcceptLoop()
{
    SockLen_t addrlen = sizeof(SocketAddr_in);
    // пока сервер запущен
    while (_status == status::up)
    {
        SocketAddr_in client_addr;
        if (Socket client_socket = accept(serv_socket, (struct sockaddr*)&client_addr, &addrlen);
            client_socket WIN(!= 0)NIX(>= 0) && _status == status::up)
        {
            // Если получен сокет с ошибкой продолжить ожидание
            if (client_socket == WIN(INVALID_SOCKET)NIX(-1)) continue;

            //активировать keep-alive для клиента
            if (!enableKeepAlive(client_socket))
            {
                shutdown(client_socket, 0);
                WIN(closesocket)NIX(close)(client_socket);
            }
            std::unique_ptr<Client> client(new Client(client_socket, client_addr));
            //запустить обработчик подключений
            connect_hndl(*client);
            //ДОбавить клиента в список клиентов
            client_mutex.lock();
            client_list.emplace_back(std::move(client));
            client_mutex.unlock();
        }
    }
}

//цикл ожидания данных
void TcpServer::waitingDataLoop()
{
    using namespace std::chrono_literals;
    while (true)
    {
        client_mutex.lock();
        // перебрать всех клиетов
        for (auto it = client_list.begin(), end = client_list.end(); it != end; ++it)
        {
            auto& client = *it;
            // Если unique_ptr содежит обклет клиета
            if (client)
            {
                if (DataBuffer data = client->loadData(); data.size)
                {
                    // при наличии данных запустить обработку входящих данных в отдельном потоке

                    std::thread([this, _data = std::move(data), &client]
                        {
                            client->access_mtx.lock();
                            handler(_data, *client);
                            client->access_mtx.unlock();
                        }).detach();

                }
                else if (client->status == SocketStatus::disconnected)
                {// при отключении клиента запустить обработку в отдельном потоке
                    std::thread([this, &client, it]
                        {
                            client->access_mtx.lock();
                            Client* pointer = client.release();
                            client = nullptr;
                            pointer->access_mtx.unlock();
                            //запуск обработчика отключения
                            disconnect_hndl(*pointer);
                            //Удалить элемент клиента из списка
                            client_list.erase(it);
                            //Удалить объект клиента
                            delete pointer;
                        }).detach();
                }
            }



        }
        client_mutex.unlock();
       /*ожидание 50мс т.к в даном потоке
       * не содержится блокирующих вызовов и данный
       * цикл сильно повышает загруженность КПУ
       */
        std::this_thread::sleep_for(50ms);
    }
}

// Функция заупуска и конфигурации Keep-Alive для сокета
bool TcpServer::enableKeepAlive(Socket socket) {
    int flag = 1;
#ifdef _WIN32
    tcp_keepalive ka{ 1, ka_conf.ka_idle * 1000, ka_conf.ka_intvl * 1000 };
    if (setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, (const char*)&flag, sizeof(flag)) != 0) return false;
    unsigned long numBytesReturned = 0;
    if (WSAIoctl(socket, SIO_KEEPALIVE_VALS, &ka, sizeof(ka), nullptr, 0, &numBytesReturned, 0, nullptr) != 0) return false;
#else //POSIX
    if (setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag)) == -1) return false;
    if (setsockopt(socket, IPPROTO_TCP, TCP_KEEPIDLE, &ka_conf.ka_idle, sizeof(ka_conf.ka_idle)) == -1) return false;
    if (setsockopt(socket, IPPROTO_TCP, TCP_KEEPINTVL, &ka_conf.ka_intvl, sizeof(ka_conf.ka_intvl)) == -1) return false;
    if (setsockopt(socket, IPPROTO_TCP, TCP_KEEPCNT, &ka_conf.ka_cnt, sizeof(ka_conf.ka_cnt)) == -1) return false;
#endif
    return true;
}
