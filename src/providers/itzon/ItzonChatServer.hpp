// SPDX-License-Identifier: MIT

#pragma once

#include "util/RatelimitBucket.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>

#include <memory>
#include <unordered_set>

namespace Communi {
class IrcMessage;
class IrcPrivateMessage;
}  // namespace Communi

namespace chatterino {

class Channel;
class IrcConnection;
class ItzonAccount;
class ItzonChannel;

class ItzonChatServer : public QObject
{
public:
    ItzonChatServer();
    ~ItzonChatServer() override;

    void initialize();
    std::shared_ptr<Channel> getOrCreate(const QString &name);
    const std::shared_ptr<Channel> &getWhispersChannel() const;
    bool canSend() const;
    void reconnectCurrent();
    void sendMessage(const QString &channelName, const QString &message);
    void sendReply(const QString &channelName, const QString &message,
                   const QString &replyToID);

private:
    struct Client {
        std::shared_ptr<ItzonAccount> account;
        std::unique_ptr<IrcConnection> connection;
        std::unique_ptr<RatelimitBucket> outgoing;
        bool authenticated = false;
        bool reconnectEnabled = true;
        QSet<QString> joinedChannels;
        QSet<QString> everJoinedChannels;
        QSet<QString> pendingJoinChannels;
        QSet<QString> pendingPartChannels;
        QSet<QString> waitingForEmotesChannels;
        QHash<QString, std::unordered_set<QString>> pendingNames;
    };

    void addClient(const std::shared_ptr<ItzonAccount> &account);
    void removeClient(const QString &username);
    void configureClient(const std::shared_ptr<Client> &client);
    void syncAllClientChannels();
    void syncClientChannels(const std::shared_ptr<Client> &client);
    void requestJoin(const std::shared_ptr<Client> &client,
                     const QString &channelName);
    void requestPart(const std::shared_ptr<Client> &client,
                     const QString &channelName);
    void releaseChannel(const QString &channelName,
                        const ItzonChannel *channel);
    bool isCurrentClient(const std::shared_ptr<Client> &client) const;
    void handleMessage(const std::shared_ptr<Client> &client,
                       Communi::IrcMessage *message);
    void handlePrivateMessage(const std::shared_ptr<Client> &client,
                              Communi::IrcPrivateMessage *message);
    void addClientSystemMessage(const std::shared_ptr<Client> &client,
                                const QString &channelName,
                                const QString &text);
    std::shared_ptr<Client> currentClient() const;
    bool rememberMessage(const QString &id);

    QHash<QString, std::shared_ptr<Client>> clients_;
    QHash<QString, std::weak_ptr<ItzonChannel>> channels_;
    std::shared_ptr<Channel> whispersChannel_;
    QSet<QString> seenMessages_;
    QQueue<QString> seenMessageOrder_;
    pajlada::Signals::SignalHolder signalHolder_;
};

}  // namespace chatterino
