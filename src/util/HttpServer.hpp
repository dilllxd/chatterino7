#pragma once

#include <QObject>

#include <functional>

class QTcpServer;

namespace chatterino {

/// A very minimal local-only HTTP server.
///
/// It's intended to be used for short-lived authentication procedures. Hence,
/// it only exposes the requested URL in the callback. The callback can then
/// return the status code as well as the response.
class HttpServer : public QObject
{
public:
    HttpServer(uint16_t port, QObject *parent = nullptr);

    using HandlerCb =
        std::function<std::pair<unsigned, QByteArray>(const QString &)>;

    void setHandler(HandlerCb handler);
    const HandlerCb &handler() const;
    bool isListening() const;
    uint16_t port() const;

private:
    HandlerCb handler_;
    QTcpServer *server_ = nullptr;
};

}  // namespace chatterino
