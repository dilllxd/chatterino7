// SPDX-License-Identifier: MIT

#pragma once

#include "common/Atomic.hpp"
#include "common/Channel.hpp"
#include "common/ChannelChatters.hpp"

#include <pajlada/signals/signal.hpp>
#include <QTimer>

#include <optional>
#include <unordered_map>

namespace chatterino {

class EmoteMap;
class MessageThread;
struct Emote;
using EmotePtr = std::shared_ptr<const Emote>;
struct EmoteName;

class ItzonChannel : public Channel, public ChannelChatters
{
public:
    struct StreamData {
        bool live = false;
        QString title;
        QString category;
        QString language;
        QString thumbnailUrl;
        QString uptime;
        qint64 startedAt = 0;
        std::optional<quint64> followers;
        std::optional<quint64> viewers;

        bool operator==(const StreamData &other) const = default;
    };

    explicit ItzonChannel(const QString &name);

    pajlada::Signals::NoArgSignal joined;
    pajlada::Signals::NoArgSignal streamDataChanged;
    pajlada::Signals::NoArgSignal liveStatusChanged;
    pajlada::Signals::NoArgSignal userStateChanged;

    void initialize();
    void reloadSeventvEmotes(bool manualRefresh);
    std::shared_ptr<const EmoteMap> seventvEmotes() const;
    EmotePtr seventvEmote(const EmoteName &name) const;
    const QString &seventvTwitchID() const;
    bool seventvEmotesReady() const;
    const StreamData &streamData() const;
    std::pair<std::shared_ptr<MessageThread>, MessagePtr> getOrCreateThread(
        const QString &messageID);

    bool canSendMessage() const override;
    bool isMod() const override;
    bool isBroadcaster() const override;
    bool hasHighRateLimit() const override;
    bool isLive() const override;
    void setMod(bool mod);
    void setVip(bool vip);
    void sendMessage(const QString &message) override;
    void sendReply(const QString &message, const QString &replyToID);

private:
    std::shared_ptr<ItzonChannel> sharedFromThis();
    std::weak_ptr<ItzonChannel> weakFromThis();
    void markSeventvEmotesReady();
    void refreshStreamData();
    void fetchPublicChannelInfo(bool preserveKnownLive);
    void fetchPublicBadge(bool preserveKnownLive);
    void updateStreamData(StreamData data);

    Atomic<std::shared_ptr<const EmoteMap>> seventvEmotes_;
    QString seventvTwitchID_;
    QTimer seventvStartupTimer_;
    bool seventvEmotesReady_ = false;
    std::unordered_map<QString, std::weak_ptr<MessageThread>> threads_;
    bool isMod_ = false;
    bool isVip_ = false;
    StreamData streamData_;
    QTimer streamDataTimer_;
    bool streamDataRequestPending_ = false;
};

}  // namespace chatterino
