#include "../AndroidLanSocket.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main()
{
    constexpr std::uint16_t testPort = 39137;
    const std::vector<std::uint8_t> payload{
        'S', 'T', 'A', 'L', 'K', 'E', 'R', '-', 'L', 'A', 'N', '-', 'T', 'E', 'S', 'T'
    };

    AndroidLan::TcpSocket listener;
    std::string error;
    if (!listener.ListenIpv4(testPort, 1, &error))
    {
        std::cerr << "Listen failed: " << error << '\n';
        return 1;
    }

    std::atomic<bool> serverSucceeded{false};
    std::thread serverThread([&]()
    {
        AndroidLan::TcpSocket peer;
        if (!listener.Accept(peer, nullptr, nullptr, &error))
            return;

        std::vector<std::uint8_t> received;
        if (!peer.ReceiveFrame(received, AndroidLan::TcpSocket::DefaultMaxFrameSize, &error))
            return;

        if (received != payload)
        {
            error = "server received a payload different from the sent payload";
            return;
        }

        if (!peer.SendFrame(received.data(), received.size(), &error))
            return;

        serverSucceeded.store(true);
    });

    AndroidLan::TcpSocket client;
    if (!client.ConnectIpv4("127.0.0.1", testPort, &error))
    {
        std::cerr << "Connect failed: " << error << '\n';
        listener.Close();
        serverThread.join();
        return 1;
    }

    std::vector<std::uint8_t> echoed;
    const bool sent = client.SendFrame(payload.data(), payload.size(), &error);
    const bool received = sent && client.ReceiveFrame(echoed, AndroidLan::TcpSocket::DefaultMaxFrameSize, &error);

    client.Close();
    serverThread.join();
    listener.Close();

    if (!received || echoed != payload || !serverSucceeded.load())
    {
        std::cerr << "TCP frame round-trip failed: " << error << '\n';
        return 1;
    }

    std::cout << "Android LAN TCP frame round-trip passed\n";
    return 0;
}
