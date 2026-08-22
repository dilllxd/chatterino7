// SPDX-License-Identifier: MIT

#pragma once

#include "common/Atomic.hpp"
#include "common/Channel.hpp"
#include "common/ChannelChatters.hpp"

#include <pajlada/signals/signal.hpp>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QTimer>

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace chatterino {

class EmoteMap;
class ItzonAccount;
class MessageThread;
struct Emote;
using EmotePtr = std::shared_ptr<const Emote>;
struct EmoteName;

class ItzonChannel : public Channel, public ChannelChatters
{
public:
    struct ChatUser {
        bool staff = false;
        bool owner = false;
        bool bot = false;
        bool moderator = false;
        bool vip = false;
        bool subscriber = false;
        bool unverified = false;
        bool guest = false;
        bool partner = false;
        QString subscriberBadge;
        QString userID;
        QString avatarExtension;

        bool operator==(const ChatUser &other) const = default;
    };

    struct StreamData {
        bool live = false;
        QString title;
        QString category;
        std::optional<qint64> categoryID;
        QString language;
        QString thumbnailUrl;
        QString uptime;
        qint64 startedAt = 0;
        std::optional<quint64> followers;
        std::optional<quint64> viewers;

        bool operator==(const StreamData &other) const = default;
    };

    struct PinnedMessage {
        QString messageID;
        QString sender;
        QString pinnedBy;
        QString messageText;
        QDateTime sentAt;

        bool operator==(const PinnedMessage &other) const = default;
    };

    explicit ItzonChannel(const QString &name);

    pajlada::Signals::NoArgSignal joined;
    pajlada::Signals::NoArgSignal streamDataChanged;
    pajlada::Signals::NoArgSignal liveStatusChanged;
    pajlada::Signals::NoArgSignal userStateChanged;
    pajlada::Signals::Signal<const QString &> chatUserChanged;
    pajlada::Signals::NoArgSignal pinnedMessageChanged;

    void initialize();
    void reloadSeventvEmotes(bool manualRefresh);
    std::shared_ptr<const EmoteMap> seventvEmotes() const;
    EmotePtr seventvEmote(const EmoteName &name) const;
    const QString &seventvTwitchID() const;
    bool seventvEmotesReady() const;
    const StreamData &streamData() const;
    static std::pair<QString, ChatUser> parseNamesEntry(QString entry);
    static std::optional<Url> avatarUrl(const QString &userID,
                                        const QString &avatarExtension);
    std::optional<ChatUser> chatUser(const QString &name) const;
    const QHash<QString, ChatUser> &chatUsers() const;
    void setChatUser(const QString &name, ChatUser user);
    void removeChatUser(const QString &name);
    void retainChatUsers(const std::unordered_set<QString> &names);
    void clearChatUsers();
    const PinnedMessage *getPinnedMessage() const;
    qsizetype pinnedMessageCount() const;
    void setPinnedMessage(PinnedMessage pin);
    void removePinnedMessage(const QString &messageID);
    void clearPinnedMessages();
    void unpinCurrentMessage();
    std::pair<std::shared_ptr<MessageThread>, MessagePtr> getOrCreateThread(
        const QString &messageID);

    bool canSendMessage() const override;
    bool canReconnect() const override;
    void reconnect() override;
    bool isMod() const override;
    bool isBroadcaster() const override;
    bool hasHighRateLimit() const override;
    bool isLive() const override;
    void setMod(bool mod);
    void setVip(bool vip);
    void sendMessage(const QString &message) override;
    void sendReply(const QString &message, const QString &replyToID);
    void setStreamTitle(const QString &title);
    void setStreamCategory(const QString &category);
    void setStreamLanguage(const QString &language);

private:
    std::shared_ptr<ItzonChannel> sharedFromThis();
    std::weak_ptr<ItzonChannel> weakFromThis();
    void markSeventvEmotesReady();
    void refreshStreamData();
    void fetchPublicChannelInfo(bool preserveKnownLive);
    void fetchPublicBadge(bool preserveKnownLive);
    void updateStreamData(StreamData data);
    std::shared_ptr<ItzonAccount> writableAccount();
    void updateStreamInfo(const QString &title,
                          std::optional<qint64> categoryID,
                          const QString &language);

    Atomic<std::shared_ptr<const EmoteMap>> seventvEmotes_;
    QString seventvTwitchID_;
    QTimer seventvStartupTimer_;
    QTimer seventvRefreshTimer_;
    bool seventvEmotesReady_ = false;
    QHash<QString, ChatUser> chatUsers_;
    QList<PinnedMessage> pinnedMessages_;
    std::unordered_map<QString, std::weak_ptr<MessageThread>> threads_;
    bool isMod_ = false;
    bool isVip_ = false;
    StreamData streamData_;
    QTimer streamDataTimer_;
    bool streamDataRequestPending_ = false;
    bool publicApiMetadataReady_ = false;
};

}  // namespace chatterino
