#include "AndroidLanSocket.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <limits>

namespace AndroidLan
{
TcpSocket::TcpSocket(int nativeHandle) noexcept
    : m_fd(nativeHandle)
{
}

TcpSocket::~TcpSocket()
{
    Close();
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept
{
    m_fd = other.m_fd;
    other.m_fd = -1;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept
{
    if (this != &other)
    {
        Close();
        m_fd = other.m_fd;
        other.m_fd = -1;
    }
    return *this;
}

void TcpSocket::SetError(std::string* error, const char* operation, int errorCode)
{
    if (error)
    {
        *error = operation;
        *error += ": ";
        *error += std::strerror(errorCode);
    }
}

bool TcpSocket::SetSocketOptions(int fd, std::string* error)
{
    const int flags = fcntl(fd, F_GETFD);
    if (flags == -1 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
    {
        SetError(error, "fcntl(FD_CLOEXEC)", errno);
        return false;
    }

    const int enabled = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) == -1)
    {
        SetError(error, "setsockopt(TCP_NODELAY)", errno);
        return false;
    }
    return true;
}

bool TcpSocket::ConnectIpv4(const char* address, std::uint16_t port, std::string* error)
{
    Close();

    const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == -1)
    {
        SetError(error, "socket", errno);
        return false;
    }

    if (!SetSocketOptions(fd, error))
    {
        close(fd);
        return false;
    }

    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    if (!address || inet_pton(AF_INET, address, &endpoint.sin_addr) != 1)
    {
        if (error)
            *error = "address must be a numeric IPv4 address";
        close(fd);
        return false;
    }

    int result;
    do
    {
        result = connect(fd, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint));
    } while (result == -1 && errno == EINTR);

    if (result == -1)
    {
        SetError(error, "connect", errno);
        close(fd);
        return false;
    }

    m_fd = fd;
    return true;
}

bool TcpSocket::ListenIpv4(std::uint16_t port, int backlog, std::string* error)
{
    Close();

    const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == -1)
    {
        SetError(error, "socket", errno);
        return false;
    }

    const int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) == -1)
    {
        SetError(error, "setsockopt(SO_REUSEADDR)", errno);
        close(fd);
        return false;
    }

    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    endpoint.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) == -1)
    {
        SetError(error, "bind", errno);
        close(fd);
        return false;
    }

    if (listen(fd, backlog) == -1)
    {
        SetError(error, "listen", errno);
        close(fd);
        return false;
    }

    m_fd = fd;
    return true;
}

bool TcpSocket::SetReceiveTimeoutMs(int timeoutMs, std::string* error)
{
    if (m_fd == -1)
    {
        if (error)
            *error = "socket is closed";
        return false;
    }

    if (timeoutMs < 0)
    {
        if (error)
            *error = "timeout must not be negative";
        return false;
    }

    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    if (setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1)
    {
        SetError(error, "setsockopt(SO_RCVTIMEO)", errno);
        return false;
    }
    return true;
}

bool TcpSocket::Accept(TcpSocket& client, std::string* peerAddress,
    std::uint16_t* peerPort, std::string* error)
{
    if (m_fd == -1)
    {
        if (error)
            *error = "listener socket is closed";
        return false;
    }

    pollfd listener{};
    listener.fd = m_fd;
    listener.events = POLLIN;

    int ready;
    do
    {
        ready = poll(&listener, 1, 100);
    } while (ready == -1 && errno == EINTR);

    if (ready == 0)
        return false;

    if (ready == -1)
    {
        SetError(error, "poll(listener)", errno);
        return false;
    }

    if (listener.revents & (POLLERR | POLLHUP | POLLNVAL))
    {
        if (error)
            *error = "listener socket is no longer available";
        return false;
    }

    sockaddr_in peer{};
    socklen_t peerSize = sizeof(peer);
    int accepted;
    do
    {
        accepted = accept(m_fd, reinterpret_cast<sockaddr*>(&peer), &peerSize);
    } while (accepted == -1 && errno == EINTR);

    if (accepted == -1)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return false;
        SetError(error, "accept", errno);
        return false;
    }

    if (!SetSocketOptions(accepted, error))
    {
        close(accepted);
        return false;
    }

    char addressBuffer[INET_ADDRSTRLEN]{};
    if (peerAddress && inet_ntop(AF_INET, &peer.sin_addr, addressBuffer, sizeof(addressBuffer)))
        *peerAddress = addressBuffer;
    if (peerPort)
        *peerPort = ntohs(peer.sin_port);

    client = TcpSocket(accepted);
    return true;
}

bool TcpSocket::SendAll(int fd, const void* data, std::size_t size, std::string* error)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t sent = 0;
    while (sent < size)
    {
        const ssize_t result = send(fd, bytes + sent, size - sent, MSG_NOSIGNAL);
        if (result > 0)
        {
            sent += static_cast<std::size_t>(result);
            continue;
        }
        if (result == -1 && errno == EINTR)
            continue;

        SetError(error, "send", result == 0 ? EPIPE : errno);
        return false;
    }
    return true;
}

bool TcpSocket::ReceiveAll(int fd, void* data, std::size_t size, std::string* error)
{
    auto* bytes = static_cast<std::uint8_t*>(data);
    std::size_t received = 0;
    while (received < size)
    {
        const ssize_t result = recv(fd, bytes + received, size - received, 0);
        if (result > 0)
        {
            received += static_cast<std::size_t>(result);
            continue;
        }
        if (result == -1 && errno == EINTR)
            continue;

        SetError(error, result == 0 ? "peer closed connection" : "recv",
            result == 0 ? ECONNRESET : errno);
        return false;
    }
    return true;
}

bool TcpSocket::SendFrame(const void* data, std::size_t size, std::string* error)
{
    if (m_fd == -1)
    {
        if (error)
            *error = "socket is closed";
        return false;
    }
    if (!data || size == 0 || size > DefaultMaxFrameSize ||
        size > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        if (error)
            *error = "invalid frame size";
        return false;
    }

    const std::uint32_t length = htonl(static_cast<std::uint32_t>(size));
    std::lock_guard<std::mutex> guard(m_sendMutex);
    return SendAll(m_fd, &length, sizeof(length), error) &&
        SendAll(m_fd, data, size, error);
}

bool TcpSocket::ReceiveFrame(std::vector<std::uint8_t>& data,
    std::size_t maxFrameSize, std::string* error)
{
    data.clear();
    if (m_fd == -1)
    {
        if (error)
            *error = "socket is closed";
        return false;
    }

    std::uint32_t encodedLength = 0;
    if (!ReceiveAll(m_fd, &encodedLength, sizeof(encodedLength), error))
        return false;

    const std::size_t length = ntohl(encodedLength);
    if (length == 0 || length > maxFrameSize)
    {
        if (error)
            *error = "received frame length is invalid or exceeds the configured limit";
        return false;
    }

    data.resize(length);
    if (!ReceiveAll(m_fd, data.data(), data.size(), error))
    {
        data.clear();
        return false;
    }
    return true;
}

bool TcpSocket::IsOpen() const noexcept
{
    return m_fd != -1;
}

int TcpSocket::NativeHandle() const noexcept
{
    return m_fd;
}

void TcpSocket::Shutdown() noexcept
{
    if (m_fd != -1)
    {
        shutdown(m_fd, SHUT_RDWR);
    }
}

void TcpSocket::Close() noexcept
{
    if (m_fd != -1)
    {
        Shutdown();
        close(m_fd);
        m_fd = -1;
    }
}
} // namespace AndroidLan
