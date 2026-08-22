// SPDX-License-Identifier: MIT

#include "providers/itzon/ItzonChatServer.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/irc/IrcConnection2.hpp"
#include "providers/itzon/ItzonAccount.hpp"
#include "providers/itzon/ItzonAccountManager.hpp"
#include "providers/itzon/ItzonChannel.hpp"
#include "providers/itzon/ItzonWebSocketProtocol.hpp"
#include "singletons/Settings.hpp"
#include "singletons/StreamerMode.hpp"

#include <IrcMessage>
#include <IrcNetwork>
#include <QColor>
#include <QPointer>
#include <QSslSocket>
#include <QTimer>

#include <algorithm>
#include <tuple>

namespace chatterino {
namespace {

const QString GUEST_CLIENT_KEY = QStringLiteral("\x01guest");

QString cleanChannelName(QString name)
{
    name = name.trimmed().toLower();
    while (name.startsWith('#'))
    {
        name.remove(0, 1);
    }
    if (std::ranges::any_of(name, [](QChar character) {
            return character.isSpace() || character.unicode() < 0x20;
        }))
    {
        return {};
    }
    return name;
}

QString ircCommand(QString prefix, QString message)
{
    message = message.left(400).replace('\r', ' ').replace('\n', ' ').replace(
        QChar::Null, ' ');
    const auto available = 510 - prefix.toUtf8().size();
    while (!message.isEmpty() && message.toUtf8().size() > available)
    {
        message.chop(1);
        if (!message.isEmpty() && message.back().isHighSurrogate())
        {
            message.chop(1);
        }
    }
    return prefix + message;
}

void applyChatUserTags(ItzonChannel &channel, const QString &name,
                       const Communi::IrcMessage &message)
{
    auto user = channel.chatUser(name).value_or(ItzonChannel::ChatUser{});
    const auto tags = message.tags();
    if (tags.has("sub-badge"))
    {
        user.subscriber = true;
        user.subscriberBadge = tags.getOrEmpty("sub-badge");
    }
    if (tags.has("partner"))
    {
        user.partner = tags.getOrEmpty("partner") == "1";
    }
    if (tags.has("user-id"))
    {
        user.userID = tags.getOrEmpty("user-id");
    }
    if (tags.has("avatar"))
    {
        user.avatarExtension = tags.getOrEmpty("avatar");
    }
    channel.setChatUser(name, std::move(user));

    if (tags.has("color"))
    {
        const QColor color(tags.getOrEmpty("color"));
        if (color.isValid())
        {
            channel.setUserColor(name, color);
        }
    }
}

}  // namespace

ItzonChatServer::ItzonChatServer()
    : whispersChannel_(std::make_shared<Channel>(QStringLiteral("/whispers"),
                                                 Channel::Type::ItzonWhispers))
{
}
ItzonChatServer::~ItzonChatServer() = default;

void ItzonChatServer::initialize()
{
    auto &manager = getApp()->getAccounts()->itzon;
    this->addGuestClient();
    for (const auto &account : manager.accounts.raw())
    {
        this->addClient(account);
    }
    this->signalHolder_.managedConnect(manager.accounts.itemInserted,
                                       [this](const auto &args) {
                                           this->addClient(args.item);
                                       });
    this->signalHolder_.managedConnect(
        manager.accounts.itemRemoved, [this](const auto &args) {
            this->removeClient(args.item->username());
        });
    this->signalHolder_.managedConnect(manager.currentUserChanged, [this] {
        this->syncAllClientChannels();
    });

    const auto reloadSeventvEmotes = [this] {
        for (const auto &weak : this->channels_)
        {
            if (auto channel = weak.lock())
            {
                channel->reloadSeventvEmotes(false);
            }
        }
    };
    getSettings()->enableSevenTVChannelEmotes.connect(
        reloadSeventvEmotes, this->signalHolder_, false);
    getSettings()->showUnlistedSevenTVEmotes.connect(
        reloadSeventvEmotes, this->signalHolder_, false);
}

void ItzonChatServer::addGuestClient()
{
    auto client = std::make_shared<Client>();
    client->configuredUsername = QStringLiteral("chatterino");
    client->connection = std::make_unique<IrcConnection>();
    client->outgoing =
        std::make_unique<RatelimitBucket>(1, 550, [](QString) {}, this);
    this->clients_.insert(GUEST_CLIENT_KEY, client);
    this->configureClient(client);
}

void ItzonChatServer::addClient(const std::shared_ptr<ItzonAccount> &account)
{
    auto key = account->username().toLower();
    if (this->clients_.contains(key))
    {
        return;
    }
    auto client = std::make_shared<Client>();
    client->account = account;
    client->configuredUsername = account->username();
    client->connection = std::make_unique<IrcConnection>();
    std::weak_ptr<Client> weakClient = client;
    client->outgoing = std::make_unique<RatelimitBucket>(
        1, 550,
        [weakClient](QString command) {
            if (auto client = weakClient.lock();
                client && client->authenticated &&
                client->connection->isConnected())
            {
                client->connection->sendRaw(command);
            }
        },
        this);
    this->clients_.insert(key, client);
    this->configureClient(client);

    this->signalHolder_.managedConnect(
        account->authUpdated, [this, weakClient] {
            auto client = weakClient.lock();
            if (!client)
            {
                return;
            }
            // Refresh rotation revokes the previous access token atomically. Move
            // IRC onto the new credential immediately rather than waiting for the
            // server's revalidation window to close the old session.
            client->reconnectEnabled = false;
            client->authenticated = false;
            client->connection->close();
            client->joinedChannels.clear();
            client->pendingJoinChannels.clear();
            client->pendingPartChannels.clear();
            client->waitingForEmotesChannels.clear();
            client->pendingNames.clear();
            client->connection->setPassword(client->account->token());
            client->reconnectEnabled = true;
            QTimer::singleShot(0, this, [client] {
                if (client->reconnectEnabled)
                {
                    client->connection->open();
                }
            });
        });
    this->signalHolder_.managedConnect(
        account->reauthorizationRequired, [this, weakClient] {
            auto client = weakClient.lock();
            if (!client)
            {
                return;
            }
            for (const auto &weak : this->channels_)
            {
                if (auto channel = weak.lock())
                {
                    channel->addSystemMessage(
                        QStringLiteral("itzon.tv sign-in expired for %1; sign "
                                       "in again from Settings > Accounts.")
                            .arg(client->account->username()));
                }
            }
        });
}

void ItzonChatServer::removeClient(const QString &username)
{
    auto client = this->clients_.take(username.toLower());
    if (client)
    {
        client->reconnectEnabled = false;
        client->connection->close();
    }
}

void ItzonChatServer::configureClient(const std::shared_ptr<Client> &client)
{
    auto *connection = client->connection.get();
    std::weak_ptr<Client> weakClient = client;
    connection->network()->setSkipCapabilityValidation(true);
    connection->network()->setRequestedCapabilities(
        {"message-tags", "echo-message", "draft/message-redaction",
         "server-time"});
    connection->setHost("itzon.tv");
    connection->setPort(443);
    connection->setSecure(true);
    if (auto *socket = qobject_cast<QSslSocket *>(connection->socket()))
    {
        socket->setPeerVerifyMode(QSslSocket::VerifyPeer);
    }
    connection->setProtocol(new ItzonWebSocketProtocol(connection));
    connection->setUserName(client->configuredUsername);
    connection->setNickName(client->configuredUsername);
    connection->setRealName(client->configuredUsername);
    connection->setPassword(client->account ? client->account->token()
                                            : QString{});

    QObject::connect(connection, &Communi::IrcConnection::messageReceived, this,
                     [this, weakClient](Communi::IrcMessage *message) {
                         if (auto client = weakClient.lock())
                         {
                             this->handleMessage(client, message);
                         }
                     });
    QObject::connect(connection,
                     &Communi::IrcConnection::privateMessageReceived, this,
                     [this, weakClient](Communi::IrcPrivateMessage *message) {
                         if (auto client = weakClient.lock())
                         {
                             this->handlePrivateMessage(client, message);
                         }
                     });
    QObject::connect(connection, &Communi::IrcConnection::connected, this,
                     [this, weakClient] {
                         if (auto client = weakClient.lock())
                         {
                             this->syncClientChannels(client);
                         }
                     });
    this->signalHolder_.managedConnect(
        connection->connectionLost, [this, weakClient](bool timeout) {
            auto client = weakClient.lock();
            if (!client)
            {
                return;
            }
            const auto joinedChannels = client->joinedChannels;
            const bool wasAuthenticated = client->authenticated;
            client->authenticated = false;
            client->joinedChannels.clear();
            client->pendingJoinChannels.clear();
            client->pendingPartChannels.clear();
            client->waitingForEmotesChannels.clear();
            client->pendingNames.clear();

            if (client->reconnectEnabled && wasAuthenticated &&
                this->isCurrentClient(client))
            {
                MessageBuilder builder(
                    systemMessage,
                    timeout ? "disconnected from itzon.tv (ping timeout); "
                              "reconnecting"
                            : "disconnected from itzon.tv; reconnecting");
                builder->flags.set(MessageFlag::DisconnectedMessage);
                auto disconnected = builder.release();
                for (const auto &channelName : joinedChannels)
                {
                    if (auto channel =
                            this->channels_.value(channelName).lock())
                    {
                        channel->addMessage(disconnected,
                                            MessageContext::Original);
                    }
                }
            }
            if (client->reconnectEnabled)
            {
                client->connection->smartReconnect();
            }
        });
    connection->open();
}

void ItzonChatServer::handleMessage(const std::shared_ptr<Client> &client,
                                    Communi::IrcMessage *message)
{
    const auto command = message->command();
    if (command == "001")
    {
        auto assigned = message->parameter(0);
        if (client->account && assigned.compare(client->configuredUsername,
                                                Qt::CaseInsensitive) != 0)
        {
            for (const auto &weak : this->channels_)
            {
                if (auto channel = weak.lock())
                {
                    channel->addSystemMessage(
                        QString("itzon.tv authenticated this chat bot token as "
                                "%1, not %2. Update the account label or chat "
                                "bot token.")
                            .arg(assigned, client->account->username()));
                }
            }
            client->reconnectEnabled = false;
            client->connection->close();
            return;
        }
        client->assignedUsername = assigned;
        client->authenticated = true;
        this->syncClientChannels(client);
        return;
    }

    if (command == "JOIN")
    {
        const auto channelName = cleanChannelName(message->parameter(0));
        const bool ownJoin = message->nick().compare(client->username(),
                                                     Qt::CaseInsensitive) == 0;
        const bool currentClient = this->isCurrentClient(client);
        if (ownJoin)
        {
            client->pendingJoinChannels.remove(channelName);
            const bool rejoined =
                client->everJoinedChannels.contains(channelName);
            client->joinedChannels.insert(channelName);
            client->everJoinedChannels.insert(channelName);
            qCDebug(chatterinoIrc)
                << "itzon.tv account" << client->username()
                << (rejoined ? "rejoined" : "joined") << channelName;
            if (auto channel = this->channels_.value(channelName).lock();
                currentClient && channel)
            {
                channel->clearChatUsers();
                channel->clearPinnedMessages();
                channel->setMod(false);
                channel->setVip(false);
                channel->addSystemMessage(rejoined ? "rejoined channel"
                                                   : "joined channel");
                channel->joined.invoke();
            }
            else if (!currentClient ||
                     this->channels_.value(channelName).expired())
            {
                // The selected account may have changed while JOIN was in
                // flight, or the channel may have closed. Complete cleanup
                // without surfacing a stale join.
                this->requestPart(client, channelName);
            }
        }
        else if (auto channel = this->channels_.value(channelName).lock();
                 currentClient && channel)
        {
            applyChatUserTags(*channel, message->nick(), *message);
            channel->addRecentChatter(message->nick());
            if (!this->clients_.contains(message->nick().toLower()) &&
                getSettings()->showJoins)
            {
                channel->addJoinedUser(
                    message->nick(), false,
                    message->nick().compare(channel->getName(),
                                            Qt::CaseInsensitive) == 0);
            }
        }
        return;
    }

    if (command == "PART")
    {
        const auto channelName = cleanChannelName(message->parameter(0));
        const bool ownPart = message->nick().compare(client->username(),
                                                     Qt::CaseInsensitive) == 0;
        const bool currentClient = this->isCurrentClient(client);
        if (ownPart)
        {
            client->pendingPartChannels.remove(channelName);
            client->joinedChannels.remove(channelName);
            if (currentClient && !this->channels_.value(channelName).expired())
            {
                // The account may have been selected again while PART was in
                // flight. Restore its desired subscription without showing a
                // misleading "left channel" status.
                this->requestJoin(client, channelName);
            }
        }
        else if (auto channel = this->channels_.value(channelName).lock();
                 currentClient && channel)
        {
            channel->removeChatUser(message->nick());
            if (!this->clients_.contains(message->nick().toLower()) &&
                getSettings()->showParts)
            {
                channel->addPartedUser(
                    message->nick(), false,
                    message->nick().compare(channel->getName(),
                                            Qt::CaseInsensitive) == 0);
            }
        }
        return;
    }

    if (command == "353")
    {
        const auto parameters = message->parameters();
        auto channelIt =
            std::ranges::find_if(parameters, [](const auto &value) {
                return value.startsWith('#');
            });
        if (channelIt == parameters.end() || parameters.isEmpty())
        {
            return;
        }
        const auto channelName = cleanChannelName(*channelIt);
        const bool currentClient = this->isCurrentClient(client);
        auto &names = client->pendingNames[channelName];
        for (auto entry : parameters.back().split(' ', Qt::SkipEmptyParts))
        {
            auto [name, user] = ItzonChannel::parseNamesEntry(std::move(entry));
            if (!name.isEmpty())
            {
                names.insert(name);
                if (auto channel = this->channels_.value(channelName).lock();
                    currentClient && channel)
                {
                    auto existing = channel->chatUser(name);
                    if (existing)
                    {
                        user.partner = existing->partner;
                        user.subscriberBadge = existing->subscriberBadge;
                        user.userID = existing->userID;
                        user.avatarExtension = existing->avatarExtension;
                    }
                    channel->setChatUser(name, user);
                }
                if (currentClient &&
                    name.compare(client->username(), Qt::CaseInsensitive) == 0)
                {
                    if (auto channel =
                            this->channels_.value(channelName).lock())
                    {
                        channel->setMod(user.moderator || user.staff ||
                                        user.owner);
                        channel->setVip(user.vip);
                    }
                }
            }
        }
        return;
    }

    if (command == "MODE")
    {
        const auto channelName = cleanChannelName(message->parameter(0));
        const auto mode = message->parameter(1);
        const auto target = message->parameter(2);
        if (auto channel = this->channels_.value(channelName).lock();
            this->isCurrentClient(client) && channel)
        {
            auto user =
                channel->chatUser(target).value_or(ItzonChannel::ChatUser{});
            if (mode == "+v" || mode == "-v")
            {
                user.moderator = mode == "+v";
            }
            else if (mode == "+V" || mode == "-V")
            {
                user.vip = mode == "+V";
            }
            channel->setChatUser(target, std::move(user));
        }
        if (this->isCurrentClient(client) &&
            target.compare(client->username(), Qt::CaseInsensitive) == 0)
        {
            if (auto channel = this->channels_.value(channelName).lock())
            {
                if (mode == "+v" || mode == "-v")
                {
                    channel->setMod(mode == "+v");
                }
                else if (mode == "+V" || mode == "-V")
                {
                    channel->setVip(mode == "+V");
                }
            }
        }
        return;
    }

    if (command == "366")
    {
        const auto parameters = message->parameters();
        auto channelIt =
            std::ranges::find_if(parameters, [](const auto &value) {
                return value.startsWith('#');
            });
        if (channelIt == parameters.end())
        {
            return;
        }
        const auto channelName = cleanChannelName(*channelIt);
        if (this->isCurrentClient(client))
        {
            if (auto channel = this->channels_.value(channelName).lock())
            {
                channel->updateOnlineChatters(
                    client->pendingNames.value(channelName));
                channel->retainChatUsers(
                    client->pendingNames.value(channelName));
            }
        }
        client->pendingNames.remove(channelName);
        return;
    }

    if (command == "META")
    {
        const auto channelName = cleanChannelName(message->parameter(0));
        const auto name = message->parameter(1);
        if (auto channel = this->channels_.value(channelName).lock();
            this->isCurrentClient(client) && channel && !name.isEmpty())
        {
            applyChatUserTags(*channel, name, *message);
        }
        return;
    }

    if (command == "NOTICE")
    {
        auto *notice = dynamic_cast<Communi::IrcNoticeMessage *>(message);
        if (notice)
        {
            this->addClientSystemMessage(
                client, cleanChannelName(notice->target()), notice->content());
        }
        return;
    }

    if (command == "ERROR")
    {
        const auto parameters = message->parameters();
        const auto detail = parameters.isEmpty()
                                ? QStringLiteral("connection closed")
                                : parameters.back();
        this->addClientSystemMessage(
            client, {},
            QStringLiteral("itzon.tv connection error: %1").arg(detail));
        if (detail.contains(QStringLiteral("4403")))
        {
            client->reconnectEnabled = false;
            this->addClientSystemMessage(
                client, {},
                client->account
                    ? QStringLiteral(
                          "The selected itzon.tv credential was revoked. Sign "
                          "in again or update its chat bot token.")
                    : QStringLiteral("itzon.tv revoked the guest connection."));
        }
        return;
    }

    static const QSet<QString> joinErrorNumerics = {"403", "405", "471", "473",
                                                    "474", "475", "476", "477"};
    if (joinErrorNumerics.contains(command))
    {
        const auto parameters = message->parameters();
        auto channelIt =
            std::ranges::find_if(parameters, [](const auto &value) {
                return value.startsWith('#');
            });
        const auto channelName = channelIt == parameters.end()
                                     ? QString{}
                                     : cleanChannelName(*channelIt);
        client->pendingJoinChannels.remove(channelName);
        client->pendingPartChannels.remove(channelName);
        const auto detail = parameters.isEmpty()
                                ? QStringLiteral("server rejected the join")
                                : parameters.back();
        this->addClientSystemMessage(
            client, channelName,
            QStringLiteral("Failed to join channel - %1").arg(detail));
        return;
    }

    if (command == "464")
    {
        const auto parameters = message->parameters();
        const auto detail = parameters.isEmpty()
                                ? QStringLiteral("authentication failed")
                                : parameters.back();
        this->addClientSystemMessage(
            client, {},
            QStringLiteral("itzon.tv authentication failed for %1 - %2")
                .arg(client->configuredUsername, detail));
        return;
    }

    if (command == "PIN")
    {
        const auto parameters = message->parameters();
        if (parameters.size() < 4)
        {
            return;
        }
        const auto channelName = cleanChannelName(parameters[0]);
        if (auto channel = this->channels_.value(channelName).lock();
            this->isCurrentClient(client) && channel)
        {
            const auto messageID = parameters[1];
            QDateTime sentAt;
            if (const auto original = channel->findMessageByID(messageID))
            {
                sentAt = original->serverReceivedTime;
            }
            channel->setPinnedMessage({
                .messageID = messageID,
                .sender = parameters[2],
                .pinnedBy = message->nick() == QStringLiteral("go-irc")
                                ? QString{}
                                : message->nick(),
                .messageText = parameters.mid(3).join(' '),
                .sentAt = sentAt,
            });
        }
        return;
    }

    if (command == "UNPIN")
    {
        if (message->parameters().size() < 2)
        {
            return;
        }
        const auto channelName = cleanChannelName(message->parameter(0));
        if (auto channel = this->channels_.value(channelName).lock();
            this->isCurrentClient(client) && channel)
        {
            channel->removePinnedMessage(message->parameter(1));
        }
        return;
    }

    if (command == "REDACT")
    {
        auto channelName = cleanChannelName(message->parameter(0));
        auto channel = this->channels_.value(channelName).lock();
        if (channel)
        {
            channel->disableMessage(message->parameter(1));
        }
    }
}

void ItzonChatServer::handlePrivateMessage(
    const std::shared_ptr<Client> &client, Communi::IrcPrivateMessage *message)
{
    // During an account switch the old PART and new JOIN briefly overlap.
    // Only the selected account is authoritative for received chat.
    if (!this->isCurrentClient(client))
    {
        return;
    }

    if (!message->target().startsWith('#'))
    {
        const bool sent = message->nick().compare(client->username(),
                                                  Qt::CaseInsensitive) == 0;
        auto [built, alert] = MessageBuilder::makeIrcMessage(
            this->whispersChannel_.get(), message,
            MessageParseArgs{
                .isReceivedWhisper = !sent,
                .isSentWhisper = sent,
                .isAction = message->isAction(),
            },
            message->content(), 0);
        if (!built)
        {
            return;
        }
        built->flags.set(MessageFlag::Whisper);
        MessageBuilder::triggerHighlights(this->whispersChannel_.get(), alert);
        this->whispersChannel_->addMessage(built, MessageContext::Original);

        auto repostFlags = std::optional<MessageFlags>(built->flags);
        repostFlags->set(MessageFlag::DoNotTriggerNotification);
        repostFlags->set(MessageFlag::DoNotLog);
        if (getSettings()->inlineWhispers &&
            !(getSettings()->streamerModeSuppressInlineWhispers &&
              getApp()->getStreamerMode()->isEnabled()))
        {
            for (const auto &weak : this->channels_)
            {
                if (auto channel = weak.lock())
                {
                    channel->addMessage(built, MessageContext::Repost,
                                        repostFlags);
                }
            }
        }
        return;
    }

    auto channelName = cleanChannelName(message->target());
    auto channel = this->channels_.value(channelName).lock();
    if (!channel)
    {
        return;
    }

    applyChatUserTags(*channel, message->nick(), *message);

    auto id = message->tags().getOrEmpty("msgid");
    if (!id.isEmpty() && !this->rememberMessage(channelName + ':' + id))
    {
        return;
    }

    std::shared_ptr<MessageThread> thread;
    MessagePtr parent;
    if (const auto replyID = message->tags().getOrEmpty("+reply");
        !replyID.isEmpty())
    {
        std::tie(thread, parent) = channel->getOrCreateThread(replyID);
    }

    auto [built, alert] = MessageBuilder::makeIrcMessage(
        channel.get(), message,
        MessageParseArgs{.isAction = message->isAction()}, message->content(),
        0, thread, parent);
    if (!built)
    {
        return;
    }
    built->id = id;
    channel->addRecentChatter(
        built->displayName.isEmpty() ? built->loginName : built->displayName);
    MessageBuilder::triggerHighlights(channel.get(), alert);
    channel->addMessage(built, MessageContext::Original);
}

void ItzonChatServer::addClientSystemMessage(
    const std::shared_ptr<Client> &client, const QString &channelName,
    const QString &text)
{
    if (text.trimmed().isEmpty())
    {
        return;
    }

    const bool currentClient = this->isCurrentClient(client);
    const auto formatted = currentClient
                               ? text
                               : QStringLiteral("itzon.tv account %1: %2")
                                     .arg(client->username(), text);

    if (!channelName.isEmpty())
    {
        if (auto channel = this->channels_.value(channelName).lock())
        {
            channel->addSystemMessage(formatted);
            return;
        }
    }

    for (const auto &weak : this->channels_)
    {
        if (auto channel = weak.lock())
        {
            channel->addSystemMessage(formatted);
        }
    }
}

bool ItzonChatServer::rememberMessage(const QString &id)
{
    if (this->seenMessages_.contains(id))
    {
        return false;
    }
    this->seenMessages_.insert(id);
    this->seenMessageOrder_.enqueue(id);
    while (this->seenMessageOrder_.size() > 4000)
    {
        this->seenMessages_.remove(this->seenMessageOrder_.dequeue());
    }
    return true;
}

bool ItzonChatServer::isCurrentClient(
    const std::shared_ptr<Client> &client) const
{
    auto current = getApp()->getAccounts()->itzon.current();
    if (!client)
    {
        return false;
    }
    if (!current)
    {
        return !client->account;
    }
    return client->account &&
           current->username().compare(client->configuredUsername,
                                       Qt::CaseInsensitive) == 0;
}

void ItzonChatServer::requestJoin(const std::shared_ptr<Client> &client,
                                  const QString &channelName)
{
    if (!this->isCurrentClient(client))
    {
        client->waitingForEmotesChannels.remove(channelName);
        return;
    }

    if (!client->authenticated || !client->connection->isConnected() ||
        client->joinedChannels.contains(channelName) ||
        client->pendingJoinChannels.contains(channelName) ||
        client->pendingPartChannels.contains(channelName))
    {
        return;
    }

    if (auto channel = this->channels_.value(channelName).lock();
        channel && !channel->seventvEmotesReady())
    {
        if (client->waitingForEmotesChannels.contains(channelName))
        {
            return;
        }

        client->waitingForEmotesChannels.insert(channelName);
        std::weak_ptr<Client> weakClient = client;
        QTimer::singleShot(100, this, [this, weakClient, channelName] {
            auto waitingClient = weakClient.lock();
            if (!waitingClient ||
                !waitingClient->waitingForEmotesChannels.remove(channelName))
            {
                return;
            }
            this->requestJoin(waitingClient, channelName);
        });
        return;
    }

    client->waitingForEmotesChannels.remove(channelName);
    client->pendingJoinChannels.insert(channelName);
    client->connection->sendRaw("JOIN #" + channelName);
}

void ItzonChatServer::requestPart(const std::shared_ptr<Client> &client,
                                  const QString &channelName)
{
    client->waitingForEmotesChannels.remove(channelName);
    if (!client->authenticated || !client->connection->isConnected() ||
        client->pendingPartChannels.contains(channelName) ||
        (!client->joinedChannels.contains(channelName) &&
         !client->pendingJoinChannels.contains(channelName)))
    {
        return;
    }

    client->pendingPartChannels.insert(channelName);
    client->connection->sendRaw("PART #" + channelName);
}

void ItzonChatServer::releaseChannel(const QString &channelName,
                                     const ItzonChannel *channel)
{
    auto registered = this->channels_.find(channelName);
    if (registered == this->channels_.end())
    {
        return;
    }
    if (auto replacement = registered.value().lock();
        replacement && replacement.get() != channel)
    {
        // A same-name channel was reopened before an older instance finished
        // releasing. Do not tear down the replacement's membership.
        return;
    }

    this->channels_.erase(registered);
    for (const auto &client : this->clients_)
    {
        this->requestPart(client, channelName);
        client->pendingNames.remove(channelName);
        client->waitingForEmotesChannels.remove(channelName);
    }
}

void ItzonChatServer::syncClientChannels(const std::shared_ptr<Client> &client)
{
    if (!client->authenticated)
    {
        return;
    }

    const bool selected = this->isCurrentClient(client);
    auto subscribedChannels = client->joinedChannels;
    subscribedChannels.unite(client->pendingJoinChannels);
    for (const auto &channelName : subscribedChannels)
    {
        if (!selected || this->channels_.value(channelName).expired())
        {
            this->requestPart(client, channelName);
        }
    }
    if (!selected)
    {
        return;
    }

    for (auto it = this->channels_.begin(); it != this->channels_.end(); ++it)
    {
        if (!it.value().expired())
        {
            this->requestJoin(client, it.key());
        }
    }
}

void ItzonChatServer::syncAllClientChannels()
{
    for (const auto &client : this->clients_)
    {
        this->syncClientChannels(client);
    }
}

std::shared_ptr<Channel> ItzonChatServer::getOrCreate(const QString &name)
{
    auto clean = cleanChannelName(name);
    if (clean.isEmpty())
    {
        return Channel::getEmpty();
    }
    if (clean == QStringLiteral("/whispers"))
    {
        return this->whispersChannel_;
    }
    if (auto existing = this->channels_.value(clean).lock())
    {
        return existing;
    }
    QPointer<ItzonChatServer> guard(this);
    auto channel = std::shared_ptr<ItzonChannel>(
        new ItzonChannel(clean), [guard, clean](ItzonChannel *released) {
            if (guard && !isAppAboutToQuit())
            {
                guard->releaseChannel(clean, released);
            }
            delete released;
        });
    this->channels_.insert(clean, channel);
    channel->initialize();
    if (auto client = this->currentClient(); client && client->authenticated)
    {
        this->requestJoin(client, clean);
    }
    return channel;
}

std::shared_ptr<Channel> ItzonChatServer::findChannel(const QString &name) const
{
    const auto clean = cleanChannelName(name);
    if (clean == QStringLiteral("/whispers"))
    {
        return this->whispersChannel_;
    }
    if (clean.isEmpty())
    {
        return Channel::getEmpty();
    }
    if (auto channel = this->channels_.value(clean).lock())
    {
        return channel;
    }
    return Channel::getEmpty();
}

const std::shared_ptr<Channel> &ItzonChatServer::getWhispersChannel() const
{
    return this->whispersChannel_;
}

std::shared_ptr<ItzonChatServer::Client> ItzonChatServer::currentClient() const
{
    auto account = getApp()->getAccounts()->itzon.current();
    if (!account)
    {
        return this->clients_.value(GUEST_CLIENT_KEY);
    }
    return this->clients_.value(account->username().toLower());
}

bool ItzonChatServer::canSend() const
{
    auto client = this->currentClient();
    return client && client->account && client->authenticated &&
           client->connection->isConnected();
}

void ItzonChatServer::reconnectCurrent()
{
    auto client = this->currentClient();
    if (!client)
    {
        return;
    }

    client->reconnectEnabled = false;
    client->authenticated = false;
    client->connection->close();
    client->joinedChannels.clear();
    client->pendingJoinChannels.clear();
    client->pendingPartChannels.clear();
    client->waitingForEmotesChannels.clear();
    client->pendingNames.clear();
    client->reconnectEnabled = true;
    QTimer::singleShot(0, this, [client] {
        if (client->reconnectEnabled)
        {
            client->connection->open();
        }
    });
}

void ItzonChatServer::sendMessage(const QString &channelName,
                                  const QString &message)
{
    auto client = this->currentClient();
    if (!client || !client->account || !client->authenticated)
    {
        return;
    }
    auto prefix = "PRIVMSG #" + cleanChannelName(channelName) + " :";
    client->outgoing->send(ircCommand(prefix, message));
}

void ItzonChatServer::sendReply(const QString &channelName,
                                const QString &message,
                                const QString &replyToID)
{
    auto client = this->currentClient();
    if (!client || !client->account || !client->authenticated)
    {
        return;
    }
    auto prefix = "@+reply=" + replyToID + " PRIVMSG #" +
                  cleanChannelName(channelName) + " :";
    client->outgoing->send(ircCommand(prefix, message));
}

}  // namespace chatterino
