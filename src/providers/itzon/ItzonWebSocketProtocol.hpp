// SPDX-License-Identifier: MIT

#pragma once

#include <IrcProtocol>
#include <QByteArray>

namespace chatterino {

/// IRC-over-WebSocket transport for itzon.tv. Using the documented WSS
/// endpoint keeps chat bot tokens off the plaintext TCP connection on port
/// 6667.
class ItzonWebSocketProtocol final : public Communi::IrcProtocol
{
public:
    explicit ItzonWebSocketProtocol(Communi::IrcConnection *connection);

    void open() override;
    void close() override;
    void read() override;
    bool write(const QByteArray &data) override;

private:
    bool sendFrame(quint8 opcode, const QByteArray &payload);
    void processFrames();
    void failHandshake();

    QByteArray receiveBuffer_;
    QByteArray handshakeKey_;
    QByteArray fragmentedMessage_;
    quint8 fragmentedOpcode_ = 0;
    bool websocketOpen_ = false;
};

}  // namespace chatterino
