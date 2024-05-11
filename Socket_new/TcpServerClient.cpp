#include "TcpServer.h"

#ifdef _WIN32
// Макросы для выражений зависимых от OS
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
// Макросы для выражений зависимых от OS
#define WIN(exp)
#define NIX(exp) exp
#endif

#include <iostream>

// Реализация загрузки данных
DataBuffer TcpServer::Client::loadData() 
{
    // Если клиент не подключён вернуть пустой буффер
    if (_status != SocketStatus::connected) return DataBuffer();
    using namespace std::chrono_literals;
    DataBuffer buffer;
    int err;

    // Чтение длинный данных в неблокирующем режиме
    // MSG_DONTWAIT - Unix флаг неблокирующего режима для recv
    // FIONBIO - Windows-флаг неблокирующего режима для ioctlsocket
    WIN(if (u_long t = true; SOCKET_ERROR == ioctlsocket(socket, FIONBIO, &t)) return DataBuffer();)
        int answ = recv(socket, (char*)&buffer.size, sizeof(buffer.size), NIX(MSG_DONTWAIT)WIN(0));

    // Обработка отключения
    if (!answ) 
    {
        disconnect();
        return DataBuffer();
    }
    else if (answ == -1)
    {
        // Чтение кода ошибки
        WIN(
            err = convertError();
        if (!err)
        {
            SockLen_t len = sizeof(err);
            getsockopt(socket, SOL_SOCKET, SO_ERROR, WIN((char*)) & err, &len);
        }
        )NIX(
            SockLen_t len = sizeof(err);
        getsockopt(socket, SOL_SOCKET, SO_ERROR, WIN((char*)) & err, &len);
        if (!err) err = errno;
        )

            // Отключение неблокирующего режима для Windows
            WIN(if (u_long t = false; SOCKET_ERROR == ioctlsocket(socket, FIONBIO, &t)) return DataBuffer();)

            // Обработка ошибки при наличии
            switch (err) 
            {
            case 0: break;
                // Keep alive timeout
            case ETIMEDOUT:
            case ECONNRESET:
            case EPIPE:
                disconnect();
                [[fallthrough]];
                // No data
            case EAGAIN: return DataBuffer();
            default:
                disconnect();
                std::cerr << "Unhandled error!\n"
                    << "Code: " << err << " Err: " << std::strerror(err) << '\n';
                return DataBuffer();
            }
    }

    // Если прочитанный размер нулевой, то вернуть пустой буффер
    if (!buffer.size) return DataBuffer();
    // Если размер не нулевой выделить буффер в куче для чтения данных
    buffer.data_ptr = (char*)malloc(buffer.size);
    // Чтение данных в блокирующем режиме
    recv(socket, (char*)buffer.data_ptr, buffer.size, 0);
    // Возврат буффера с прочитанными данными
    return buffer;
}

// Обработка отключения клиента
TcpClientBase::status TcpServer::Client::disconnect() {
    _status = status::disconnected;
    // Если сокет не валидный прекратить обработку
    if (socket == WIN(INVALID_SOCKET)NIX(-1)) return _status;
    // Отключение сокета
    shutdown(socket, SD_BOTH)
        // Закрытие сокета
        WIN(closesocket)NIX(close)(socket);
    // Установление в сокета не валидного значения
    socket = WIN(INVALID_SOCKET)NIX(-1);
    return _status;
}

// Отправка данных
bool TcpServer::Client::sendData(const void* buffer, const size_t size) const {
    // Если сокет закрыт вернуть false
    if (_status != SocketStatus::connected) return false;
    // Сформировать сообщение с длинной в начале
    void* send_buffer = malloc(size + sizeof(int));
    memcpy(reinterpret_cast<char*>(send_buffer) + sizeof(int), buffer, size);
    *reinterpret_cast<int*>(send_buffer) = size;

    // Отправить сообщение
    if (send(socket, reinterpret_cast<char*>(send_buffer), size + sizeof(int), 0) < 0) return false;
    // Вычистить буффер сообщения
    free(send_buffer);
    return true;
}

// Конструктор клиента
TcpServer::Client::Client(Socket socket, SocketAddr_in address)
    : address(address), socket(socket) {}

// Деструктор клиента с закрытием сокета
TcpServer::Client::~Client() 
{
    if (socket == WIN(INVALID_SOCKET)NIX(-1)) return;
    shutdown(socket, SD_BOTH);
    WIN(closesocket(socket);)
        NIX(close(socket);)
}

// Получить хост клиента
uint32_t TcpServer::Client::getHost() const { return NIX(address.sin_addr.s_addr) WIN(address.sin_addr.S_un.S_addr); }
// Получить порт клиента
uint16_t TcpServer::Client::getPort() const { return address.sin_port; }