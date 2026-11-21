#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace AndroidLan
{
// Small blocking IPv4/TCP transport used by the Android OpenXRay backend.
// Frames are encoded as: 4-byte network-order payload length + payload.
// A frame is rejected before allocation when its length exceeds maxFrameSize.
class TcpSocket final
{
public:
    static constexpr std::size_t DefaultMaxFrameSize = 1024 * 1024;

    TcpSocket() noexcept = default;
    explicit TcpSocket(int nativeHandle) noexcept;
    ~TcpSocket();

    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;

    bool ConnectIpv4(const char* address, std::uint16_t port, std::string* error = nullptr);
    bool ListenIpv4(std::uint16_t port, int backlog = 4, std::string* error = nullptr);
    bool Accept(TcpSocket& client, std::string* peerAddress = nullptr,
        std::uint16_t* peerPort = nullptr, std::string* error = nullptr);
    void Shutdown() noexcept;

    bool SendFrame(const void* data, std::size_t size, std::string* error = nullptr);
    bool ReceiveFrame(std::vector<std::uint8_t>& data,
        std::size_t maxFrameSize = DefaultMaxFrameSize, std::string* error = nullptr);

    bool IsOpen() const noexcept;
    int NativeHandle() const noexcept;
    void Close() noexcept;

private:
    static bool SetSocketOptions(int fd, std::string* error);
    static bool SendAll(int fd, const void* data, std::size_t size, std::string* error);
    static bool ReceiveAll(int fd, void* data, std::size_t size, std::string* error);
    static void SetError(std::string* error, const char* operation, int errorCode);

    int m_fd = -1;
    std::mutex m_sendMutex;
};
} // namespace AndroidLan
