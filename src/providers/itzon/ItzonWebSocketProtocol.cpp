// SPDX-License-Identifier: MIT

#include "providers/itzon/ItzonWebSocketProtocol.hpp"

#include <IrcConnection>
#include <IrcMessage>
#include <IrcNetwork>
#include <QAbstractSocket>
#include <QCryptographicHash>
#include <QRandomGenerator>

#include <limits>

namespace chatterino {
namespace {

constexpr auto WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

QString closeDescription(quint16 code)
{
    switch (code)
    {
        case 4400:
            return QStringLiteral("flood limit exceeded");
        case 4401:
            return QStringLiteral("ping timeout");
        case 4402:
            return QStringLiteral("server queue overflow");
        case 4403:
            return QStringLiteral("authentication revoked");
        default:
            return QStringLiteral("connection closed");
    }
}

quint64 readBigEndian(const QByteArray &data, qsizetype offset,
                      qsizetype length)
{
    quint64 value = 0;
    for (qsizetype i = 0; i < length; ++i)
    {
        value = (value << 8) | static_cast<unsigned char>(data.at(offset + i));
    }
    return value;
}

void appendBigEndian(QByteArray &data, quint64 value, qsizetype length)
{
    for (qsizetype i = length - 1; i >= 0; --i)
    {
        data.append(static_cast<char>((value >> (i * 8)) & 0xff));
    }
}

}  // namespace

ItzonWebSocketProtocol::ItzonWebSocketProtocol(
    Communi::IrcConnection *connection)
    : Communi::IrcProtocol(connection)
{
}

void ItzonWebSocketProtocol::open()
{
    QByteArray nonce(16, Qt::Uninitialized);
    for (auto &byte : nonce)
    {
        byte = static_cast<char>(QRandomGenerator::global()->generate() & 0xff);
    }
    this->handshakeKey_ = nonce.toBase64();
    this->receiveBuffer_.clear();
    this->fragmentedMessage_.clear();
    this->fragmentedOpcode_ = 0;
    this->websocketOpen_ = false;

    QByteArray request = "GET /ws/irc HTTP/1.1\r\n"
                         "Host: itzon.tv\r\n"
                         "Upgrade: websocket\r\n"
                         "Connection: Upgrade\r\n"
                         "Sec-WebSocket-Version: 13\r\n"
                         "Sec-WebSocket-Key: " +
                         this->handshakeKey_ + "\r\n\r\n";
    if (this->socket()->write(request) != request.size())
    {
        this->failHandshake();
    }
}

void ItzonWebSocketProtocol::close()
{
    // Communi also invokes protocol::close after QSslSocket has already
    // disconnected. Writing through Schannel at that point is invalid.
    if (this->websocketOpen_ && this->socket() &&
        this->socket()->state() == QAbstractSocket::ConnectedState)
    {
        this->sendFrame(0x8, {});
    }
    this->websocketOpen_ = false;
    Communi::IrcProtocol::close();
}

void ItzonWebSocketProtocol::read()
{
    this->receiveBuffer_.append(this->socket()->readAll());
    if (!this->websocketOpen_)
    {
        auto headerEnd = this->receiveBuffer_.indexOf("\r\n\r\n");
        if (headerEnd < 0)
        {
            return;
        }

        auto headers = this->receiveBuffer_.left(headerEnd + 4);
        this->receiveBuffer_.remove(0, headerEnd + 4);
        auto expectedAccept =
            QCryptographicHash::hash(this->handshakeKey_ + WEBSOCKET_GUID,
                                     QCryptographicHash::Sha1)
                .toBase64();
        if (!headers.startsWith("HTTP/1.1 101 ") ||
            !headers.toLower().contains("upgrade: websocket") ||
            !headers.contains("Sec-WebSocket-Accept: " + expectedAccept))
        {
            this->failHandshake();
            return;
        }

        this->websocketOpen_ = true;
        // itzon.tv requires PASS to be the first IRC command, followed by its
        // documented capability request before registration. Communi's stock
        // ordering starts CAP first, so perform this short registration
        // sequence explicitly for this transport.
        const auto password = this->connection()->password();
        if (!password.isEmpty())
        {
            this->connection()->sendRaw("PASS " + password);
        }
        const auto capabilities =
            this->connection()->network()->requestedCapabilities();
        if (!capabilities.isEmpty())
        {
            this->connection()->sendRaw("CAP REQ :" + capabilities.join(' '));
        }
        this->connection()->sendRaw("CAP END");

        auto nick = this->connection()->nickName();
        if (nick.isEmpty())
        {
            nick = this->connection()->nickNames().value(0);
        }
        this->connection()->sendRaw("NICK " + nick);
        this->connection()->sendRaw(QString("USER %1 hostname servername :%2")
                                        .arg(this->connection()->userName(),
                                             this->connection()->realName()));
    }

    this->processFrames();
}

bool ItzonWebSocketProtocol::write(const QByteArray &data)
{
    return this->websocketOpen_ && this->sendFrame(0x1, data);
}

bool ItzonWebSocketProtocol::sendFrame(quint8 opcode, const QByteArray &payload)
{
    QByteArray frame;
    frame.reserve(payload.size() + 14);
    frame.append(static_cast<char>(0x80 | opcode));

    auto size = static_cast<quint64>(payload.size());
    if (size < 126)
    {
        frame.append(static_cast<char>(0x80 | size));
    }
    else if (size <= std::numeric_limits<quint16>::max())
    {
        frame.append(static_cast<char>(0x80 | 126));
        appendBigEndian(frame, size, 2);
    }
    else
    {
        frame.append(static_cast<char>(0x80 | 127));
        appendBigEndian(frame, size, 8);
    }

    quint32 mask = QRandomGenerator::global()->generate();
    QByteArray maskBytes;
    appendBigEndian(maskBytes, mask, 4);
    frame.append(maskBytes);
    for (qsizetype i = 0; i < payload.size(); ++i)
    {
        frame.append(payload.at(i) ^ maskBytes.at(i % 4));
    }
    return this->socket()->write(frame) == frame.size();
}

void ItzonWebSocketProtocol::processFrames()
{
    while (this->receiveBuffer_.size() >= 2)
    {
        auto first = static_cast<quint8>(this->receiveBuffer_.at(0));
        auto second = static_cast<quint8>(this->receiveBuffer_.at(1));
        bool final = (first & 0x80) != 0;
        quint8 opcode = first & 0x0f;
        bool masked = (second & 0x80) != 0;
        quint64 payloadLength = second & 0x7f;
        qsizetype offset = 2;

        if (payloadLength == 126)
        {
            if (this->receiveBuffer_.size() < offset + 2)
            {
                return;
            }
            payloadLength = readBigEndian(this->receiveBuffer_, offset, 2);
            offset += 2;
        }
        else if (payloadLength == 127)
        {
            if (this->receiveBuffer_.size() < offset + 8)
            {
                return;
            }
            payloadLength = readBigEndian(this->receiveBuffer_, offset, 8);
            offset += 8;
        }

        QByteArray mask;
        if (masked)
        {
            if (this->receiveBuffer_.size() < offset + 4)
            {
                return;
            }
            mask = this->receiveBuffer_.mid(offset, 4);
            offset += 4;
        }
        if (payloadLength >
                static_cast<quint64>(std::numeric_limits<qsizetype>::max()) ||
            this->receiveBuffer_.size() <
                offset + static_cast<qsizetype>(payloadLength))
        {
            return;
        }

        auto payload = this->receiveBuffer_.mid(
            offset, static_cast<qsizetype>(payloadLength));
        this->receiveBuffer_.remove(
            0, offset + static_cast<qsizetype>(payloadLength));
        if (masked)
        {
            for (qsizetype i = 0; i < payload.size(); ++i)
            {
                payload[i] = payload.at(i) ^ mask.at(i % 4);
            }
        }

        if (opcode == 0x8)
        {
            if (payload.size() >= 2)
            {
                const auto code =
                    static_cast<quint16>(readBigEndian(payload, 0, 2));
                auto reason = QString::fromUtf8(payload.mid(2))
                                  .replace('\r', ' ')
                                  .replace('\n', ' ')
                                  .trimmed();
                auto detail =
                    QStringLiteral("itzon.tv closed WebSocket (%1: %2)")
                        .arg(code)
                        .arg(closeDescription(code));
                if (!reason.isEmpty())
                {
                    detail += QStringLiteral(" - ") + reason;
                }
                if (auto *message = Communi::IrcMessage::fromData(
                        QStringLiteral("ERROR :").append(detail).toUtf8(),
                        this->connection()))
                {
                    this->receiveMessage(message);
                }
            }
            this->sendFrame(0x8, payload);
            this->socket()->disconnectFromHost();
            return;
        }
        if (opcode == 0x9)
        {
            this->sendFrame(0xA, payload);
            continue;
        }
        if (opcode == 0xA)
        {
            continue;
        }
        if (opcode == 0x1)
        {
            this->fragmentedOpcode_ = opcode;
            this->fragmentedMessage_ = payload;
        }
        else if (opcode == 0x0 && this->fragmentedOpcode_ != 0)
        {
            this->fragmentedMessage_.append(payload);
        }
        else
        {
            continue;
        }

        if (final && this->fragmentedOpcode_ == 0x1)
        {
            auto *message = Communi::IrcMessage::fromData(
                this->fragmentedMessage_, this->connection());
            this->fragmentedMessage_.clear();
            this->fragmentedOpcode_ = 0;
            if (message)
            {
                if (message->type() == Communi::IrcMessage::Ping)
                {
                    auto *ping =
                        static_cast<Communi::IrcPingMessage *>(message);
                    this->connection()->sendRaw("PONG " + ping->argument());
                }
                else if (message->command() == "001")
                {
                    this->setNickName(message->parameter(0));
                    this->setStatus(Communi::IrcConnection::Connected);
                }
                this->receiveMessage(message);
                if (!this->websocketOpen_)
                {
                    return;
                }
            }
        }
    }
}

void ItzonWebSocketProtocol::failHandshake()
{
    this->websocketOpen_ = false;
    this->socket()->abort();
}

}  // namespace chatterino
