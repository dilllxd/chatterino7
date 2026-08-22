// SPDX-License-Identifier: MIT

#include "providers/itzon/ItzonChannel.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "messages/Emote.hpp"
#include "messages/Message.hpp"
#include "messages/MessageThread.hpp"
#include "providers/itzon/ItzonAccount.hpp"
#include "providers/itzon/ItzonApiToken.hpp"
#include "providers/itzon/ItzonChatServer.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "singletons/Settings.hpp"
#include "util/Helpers.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace chatterino {

ItzonChannel::ItzonChannel(const QString &name)
    : Channel(name.toLower(), Channel::Type::Itzon)
    , ChannelChatters(static_cast<Channel &>(*this))
    , seventvEmotes_(std::make_shared<EmoteMap>())
{
    this->platform_ = "itzon";
    this->streamDataTimer_.setInterval(60 * 1000);
    QObject::connect(&this->streamDataTimer_, &QTimer::timeout, [this] {
        this->refreshStreamData();
    });
    this->seventvStartupTimer_.setSingleShot(true);
    this->seventvStartupTimer_.setInterval(5 * 1000);
    QObject::connect(&this->seventvStartupTimer_, &QTimer::timeout, [this] {
        this->markSeventvEmotesReady();
    });
    this->seventvRefreshTimer_.setInterval(5 * 60 * 1000);
    QObject::connect(&this->seventvRefreshTimer_, &QTimer::timeout, [this] {
        this->reloadSeventvEmotes(false);
    });
}

void ItzonChannel::initialize()
{
    this->seventvStartupTimer_.start();
    this->reloadSeventvEmotes(false);
    this->seventvRefreshTimer_.start();
    this->refreshStreamData();
    this->streamDataTimer_.start();
}

std::shared_ptr<ItzonChannel> ItzonChannel::sharedFromThis()
{
    return std::static_pointer_cast<ItzonChannel>(this->shared_from_this());
}

std::weak_ptr<ItzonChannel> ItzonChannel::weakFromThis()
{
    return this->sharedFromThis();
}

void ItzonChannel::reloadSeventvEmotes(bool manualRefresh)
{
    if (!Settings::instance().enableSevenTVChannelEmotes)
    {
        this->seventvEmotes_.set(EMPTY_EMOTE_MAP);
        this->markSeventvEmotesReady();
        return;
    }

    QUrl channelInfoUrl(QStringLiteral("https://itzon.tv"));
    channelInfoUrl.setPath(QStringLiteral("/api/live/channel/") +
                           this->getName());

    auto weak = this->weakFromThis();
    NetworkRequest(channelInfoUrl)
        .timeout(10000)
        .onSuccess([weak, manualRefresh](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }

            const auto twitchID =
                result.parseJson()["emoteTwitchId"].toString().trimmed();
            static const QRegularExpression validTwitchID{
                QStringLiteral("^[0-9]{1,32}$")};
            if (!validTwitchID.match(twitchID).hasMatch())
            {
                const bool firstLoad = !self->seventvEmotesReady_;
                self->seventvTwitchID_.clear();
                self->seventvEmotes_.set(EMPTY_EMOTE_MAP);
                if (manualRefresh || firstLoad)
                {
                    self->addSystemMessage(
                        "7TV channel emotes unavailable - no Twitch account "
                        "is linked in this channel's itzon.tv settings.");
                }
                self->markSeventvEmotesReady();
                return;
            }

            self->seventvTwitchID_ = twitchID;
            bool cacheHit = readProviderEmotesCache(
                twitchID, "seventv", [weak](const auto &jsonDoc) {
                    if (auto channel = weak.lock())
                    {
                        const auto emoteSet =
                            jsonDoc.object()["emote_set"].toObject();
                        auto emotes = seventv::detail::parseEmotes(
                            emoteSet["emotes"].toArray(),
                            SeventvEmoteSetKind::Channel);
                        channel->seventvEmotes_.set(
                            std::make_shared<const EmoteMap>(
                                std::move(emotes)));
                        channel->markSeventvEmotesReady();
                    }
                });

            SeventvEmotes::loadChannelEmotes(
                weak, twitchID,
                [weak](EmoteMap &&emotes, const SeventvEmotes::ChannelInfo &) {
                    if (auto channel = weak.lock())
                    {
                        channel->seventvEmotes_.set(
                            std::make_shared<const EmoteMap>(
                                std::move(emotes)));
                        channel->markSeventvEmotesReady();
                    }
                },
                manualRefresh, cacheHit);
        })
        .onError([weak, manualRefresh](const NetworkResult &result) {
            qCWarning(chatterinoSeventv)
                << "Failed to resolve itzon.tv 7TV mapping:"
                << result.formatError();
            if (auto self = weak.lock())
            {
                const bool firstLoad = !self->seventvEmotesReady_;
                self->markSeventvEmotesReady();
                if (manualRefresh || firstLoad)
                {
                    self->addSystemMessage(
                        QStringLiteral(
                            "Failed to resolve this channel's itzon.tv 7TV "
                            "mapping. (Error: %1)")
                            .arg(result.formatError()));
                }
            }
        })
        .execute();
}

std::shared_ptr<const EmoteMap> ItzonChannel::seventvEmotes() const
{
    return this->seventvEmotes_.get();
}

EmotePtr ItzonChannel::seventvEmote(const EmoteName &name) const
{
    auto emotes = this->seventvEmotes_.get();
    auto it = emotes->find(name);
    return it == emotes->end() ? nullptr : it->second;
}

const QString &ItzonChannel::seventvTwitchID() const
{
    return this->seventvTwitchID_;
}

bool ItzonChannel::seventvEmotesReady() const
{
    return this->seventvEmotesReady_;
}

std::pair<QString, ItzonChannel::ChatUser> ItzonChannel::parseNamesEntry(
    QString entry)
{
    ChatUser user;
    while (!entry.isEmpty())
    {
        switch (entry.front().unicode())
        {
            case '%':
                user.staff = true;
                break;
            case '@':
                user.owner = true;
                break;
            case '&':
                user.bot = true;
                break;
            case '+':
                user.moderator = true;
                break;
            case '~':
                user.vip = true;
                break;
            case '*':
                user.subscriber = true;
                break;
            case '=':
                user.unverified = true;
                break;
            case '?':
                user.guest = true;
                break;
            default:
                return {entry, user};
        }
        entry.remove(0, 1);
    }
    return {entry, user};
}

std::optional<Url> ItzonChannel::avatarUrl(const QString &userID,
                                           const QString &avatarExtension)
{
    static const QRegularExpression validUserID{QStringLiteral("^[0-9]+$")};
    static const QRegularExpression validExtension{
        QStringLiteral("^(?:jpg|png|gif)$")};
    if (!validUserID.match(userID).hasMatch() ||
        !validExtension.match(avatarExtension).hasMatch())
    {
        return std::nullopt;
    }

    return Url{QStringLiteral("https://itzon.tv/api/live/avatar/") + userID +
               '.' + avatarExtension};
}

std::optional<ItzonChannel::ChatUser> ItzonChannel::chatUser(
    const QString &name) const
{
    auto it = this->chatUsers_.constFind(name.toLower());
    if (it == this->chatUsers_.cend())
    {
        return std::nullopt;
    }
    return *it;
}

const QHash<QString, ItzonChannel::ChatUser> &ItzonChannel::chatUsers() const
{
    return this->chatUsers_;
}

void ItzonChannel::setChatUser(const QString &name, ChatUser user)
{
    if (!name.isEmpty())
    {
        this->chatUsers_.insert(name.toLower(), std::move(user));
    }
}

void ItzonChannel::removeChatUser(const QString &name)
{
    this->chatUsers_.remove(name.toLower());
}

void ItzonChannel::retainChatUsers(const std::unordered_set<QString> &names)
{
    QSet<QString> normalizedNames;
    normalizedNames.reserve(static_cast<qsizetype>(names.size()));
    for (const auto &name : names)
    {
        normalizedNames.insert(name.toLower());
    }
    for (auto it = this->chatUsers_.begin(); it != this->chatUsers_.end();)
    {
        if (!normalizedNames.contains(it.key()))
        {
            it = this->chatUsers_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void ItzonChannel::clearChatUsers()
{
    this->chatUsers_.clear();
}

void ItzonChannel::markSeventvEmotesReady()
{
    if (this->seventvEmotesReady_)
    {
        return;
    }
    this->seventvEmotesReady_ = true;
    this->seventvStartupTimer_.stop();
}

const ItzonChannel::StreamData &ItzonChannel::streamData() const
{
    return this->streamData_;
}

std::pair<std::shared_ptr<MessageThread>, MessagePtr>
    ItzonChannel::getOrCreateThread(const QString &messageID)
{
    auto existingIt = this->threads_.find(messageID);
    if (existingIt != this->threads_.end())
    {
        if (auto existing = existingIt->second.lock())
        {
            return {existing, existing->root()};
        }
    }

    auto message = this->findMessageByID(messageID);
    if (!message)
    {
        return {nullptr, nullptr};
    }
    if (message->replyThread)
    {
        return {message->replyThread, message};
    }

    auto thread = std::make_shared<MessageThread>(message);
    this->threads_[messageID] = thread;
    return {thread, message};
}

void ItzonChannel::refreshStreamData()
{
    if (this->streamDataRequestPending_)
    {
        return;
    }
    this->streamDataRequestPending_ = true;

    const auto token = itzon::apiToken();
    if (token.isEmpty())
    {
        this->fetchPublicChannelInfo(false);
        return;
    }

    QUrl url(QStringLiteral("https://itzon.tv"));
    url.setPath(QStringLiteral("/api/public/v1/channel/") + this->getName());

    auto weak = this->weakFromThis();
    NetworkRequest(url)
        .header("Authorization", QByteArrayLiteral("Bearer ") + token)
        .timeout(10000)
        .onSuccess([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }

            const auto json = result.parseJson();
            if (!json["live"].isBool())
            {
                self->fetchPublicChannelInfo(true);
                return;
            }

            auto data = self->streamData_;
            data.live = json["live"].toBool();
            data.title = json["title"].toString().trimmed();
            data.category = json["category"].toString().trimmed();
            if (json["categoryId"].isDouble())
            {
                data.categoryID = json["categoryId"].toInteger();
            }
            else if (json["categoryId"].isNull())
            {
                data.categoryID.reset();
            }
            data.language = json["language"].toString().trimmed();
            if (json["followers"].isDouble())
            {
                data.followers = static_cast<quint64>(
                    std::max<qint64>(0, json["followers"].toInteger()));
            }
            if (json["viewers"].isDouble())
            {
                data.viewers = static_cast<quint64>(
                    std::max<qint64>(0, json["viewers"].toInteger()));
            }

            self->publicApiMetadataReady_ = true;
            self->updateStreamData(std::move(data));
            self->fetchPublicChannelInfo(true);
        })
        .onError([weak](const NetworkResult &) {
            if (auto self = weak.lock())
            {
                self->fetchPublicChannelInfo(true);
            }
        })
        .execute();
}

void ItzonChannel::fetchPublicChannelInfo(bool preserveKnownLive)
{
    QUrl url(QStringLiteral("https://itzon.tv"));
    url.setPath(QStringLiteral("/api/live/channel/") + this->getName());

    auto weak = this->weakFromThis();
    NetworkRequest(url)
        .timeout(10000)
        .onSuccess([weak, preserveKnownLive](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }

            const auto json = result.parseJson();
            if (!json["live"].isBool())
            {
                self->fetchPublicBadge(preserveKnownLive);
                return;
            }

            const bool live = json["live"].toBool();
            if (preserveKnownLive && self->streamData_.live && !live)
            {
                self->streamDataRequestPending_ = false;
                return;
            }

            auto data = self->streamData_;
            data.live = live;
            if (!json["title"].toString().trimmed().isEmpty())
            {
                data.title = json["title"].toString().trimmed();
            }
            if (!json["category"].toString().trimmed().isEmpty())
            {
                data.category = json["category"].toString().trimmed();
            }
            if (!json["language"].toString().trimmed().isEmpty())
            {
                data.language = json["language"].toString().trimmed();
            }

            if (live && json["startedAt"].isDouble())
            {
                data.startedAt =
                    std::max<qint64>(0, json["startedAt"].toInteger());
                const auto started =
                    QDateTime::fromMSecsSinceEpoch(data.startedAt, Qt::UTC);
                const auto elapsed = std::max<qint64>(
                    0, started.secsTo(QDateTime::currentDateTimeUtc()));
                data.uptime = QStringLiteral("%1h %2m")
                                  .arg(elapsed / 3600)
                                  .arg(elapsed % 3600 / 60);

                QUrl preview(QStringLiteral("https://itzon.tv"));
                preview.setPath(QStringLiteral("/api/live/channel/") +
                                self->getName() +
                                QStringLiteral("/preview.jpg"));
                QUrlQuery query;
                query.addQueryItem(QStringLiteral("v"),
                                   QString::number(data.startedAt / 1000));
                preview.setQuery(query);
                data.thumbnailUrl = preview.toString();
            }
            else if (!live)
            {
                data.startedAt = 0;
                data.uptime.clear();
                data.thumbnailUrl.clear();
                data.viewers.reset();
            }

            self->streamDataRequestPending_ = false;
            self->updateStreamData(std::move(data));
        })
        .onError([weak, preserveKnownLive](const NetworkResult &) {
            if (auto self = weak.lock())
            {
                self->fetchPublicBadge(preserveKnownLive);
            }
        })
        .execute();
}

void ItzonChannel::fetchPublicBadge(bool preserveKnownLive)
{
    QUrl url(QStringLiteral("https://itzon.tv"));
    url.setPath(QStringLiteral("/api/public/v1/badge/") + this->getName() +
                QStringLiteral(".json"));

    auto weak = this->weakFromThis();
    NetworkRequest(url)
        .timeout(10000)
        .onSuccess([weak, preserveKnownLive](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }

            const auto json = result.parseJson();
            self->streamDataRequestPending_ = false;
            if (!json["live"].isBool())
            {
                return;
            }

            const bool live = json["live"].toBool();
            if (preserveKnownLive && self->streamData_.live && !live)
            {
                return;
            }

            auto data = self->streamData_;
            data.live = live;
            if (!live)
            {
                data.viewers.reset();
            }
            self->updateStreamData(std::move(data));
        })
        .onError([weak](const NetworkResult &) {
            if (auto self = weak.lock())
            {
                self->streamDataRequestPending_ = false;
            }
        })
        .execute();
}

void ItzonChannel::updateStreamData(StreamData data)
{
    if (this->streamData_ == data)
    {
        return;
    }

    const bool liveChanged = this->streamData_.live != data.live;
    this->streamData_ = std::move(data);
    this->streamDataChanged.invoke();
    if (liveChanged)
    {
        this->liveStatusChanged.invoke();
    }
}

bool ItzonChannel::canSendMessage() const
{
    return getApp()->getItzonChatServer()->canSend();
}

bool ItzonChannel::canReconnect() const
{
    return true;
}

void ItzonChannel::reconnect()
{
    getApp()->getItzonChatServer()->reconnectCurrent();
}

bool ItzonChannel::isMod() const
{
    return this->isMod_;
}

bool ItzonChannel::isBroadcaster() const
{
    const auto current = getApp()->getAccounts()->itzon.current();
    return current && current->username().compare(this->getName(),
                                                  Qt::CaseInsensitive) == 0;
}

bool ItzonChannel::hasHighRateLimit() const
{
    return this->hasModRights() || this->isVip_;
}

void ItzonChannel::setMod(bool mod)
{
    if (this->isMod_ == mod)
    {
        return;
    }
    this->isMod_ = mod;
    this->userStateChanged.invoke();
}

void ItzonChannel::setVip(bool vip)
{
    if (this->isVip_ == vip)
    {
        return;
    }
    this->isVip_ = vip;
    this->userStateChanged.invoke();
}

bool ItzonChannel::isLive() const
{
    return this->streamData_.live;
}

void ItzonChannel::sendMessage(const QString &message)
{
    getApp()->getItzonChatServer()->sendMessage(this->getName(), message);
}

void ItzonChannel::sendReply(const QString &message, const QString &replyToID)
{
    getApp()->getItzonChatServer()->sendReply(this->getName(), message,
                                              replyToID);
}

std::shared_ptr<ItzonAccount> ItzonChannel::writableAccount()
{
    const auto account = getApp()->getAccounts()->itzon.current();
    if (!this->isBroadcaster())
    {
        this->addSystemMessage(
            "Stream information can only be changed from your own channel.");
        return {};
    }
    if (!account || !account->isOAuth() ||
        !account->hasScope(QStringLiteral("api:write")))
    {
        this->addSystemMessage(
            "Changing stream information requires signing in again with the "
            "stream-info permission enabled.");
        return {};
    }
    if (!account->accessTokenValid())
    {
        account->refreshIfNeeded();
        this->addSystemMessage(
            "Your itzon.tv access token is refreshing. Try again shortly.");
        return {};
    }
    if (!this->publicApiMetadataReady_)
    {
        this->addSystemMessage(
            "Channel metadata is still loading. Try again shortly.");
        return {};
    }
    return account;
}

void ItzonChannel::updateStreamInfo(const QString &title,
                                    std::optional<qint64> categoryID,
                                    const QString &language)
{
    const auto account = this->writableAccount();
    if (!account)
    {
        return;
    }

    const auto normalizedLanguage = language.trimmed().isEmpty()
                                        ? QStringLiteral("und")
                                        : language.trimmed();
    QJsonObject payload{
        {QStringLiteral("title"), title},
        {QStringLiteral("categoryId"),
         categoryID ? QJsonValue(*categoryID) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("language"), normalizedLanguage},
    };
    auto weak = this->weakFromThis();
    NetworkRequest(
        QStringLiteral("https://itzon.tv/api/public/v1/me/stream-info"),
        NetworkRequestType::Put)
        .header("Authorization",
                QByteArrayLiteral("Bearer ") + account->token().toUtf8())
        .header("Content-Type", "application/json")
        .payload(QJsonDocument(payload).toJson(QJsonDocument::Compact))
        .timeout(10000)
        .onSuccess([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }
            const auto json = result.parseJson();
            auto data = self->streamData_;
            data.title = json["title"].toString().trimmed();
            data.category = json["category"].toString().trimmed();
            data.language = json["language"].toString().trimmed();
            if (json["categoryId"].isDouble())
            {
                data.categoryID = json["categoryId"].toInteger();
            }
            else
            {
                data.categoryID.reset();
            }
            self->updateStreamData(std::move(data));
            self->addSystemMessage("Stream information updated.");
        })
        .onError([weak](const NetworkResult &result) {
            if (auto self = weak.lock())
            {
                const auto error = result.parseJson()["error"].toString();
                self->addSystemMessage(
                    QStringLiteral("Failed to update stream information: %1")
                        .arg(error.isEmpty() ? result.formatError() : error));
            }
        })
        .execute();
}

void ItzonChannel::setStreamTitle(const QString &title)
{
    this->updateStreamInfo(title.left(200), this->streamData_.categoryID,
                           this->streamData_.language);
}

void ItzonChannel::setStreamCategory(const QString &category)
{
    const auto account = this->writableAccount();
    if (!account)
    {
        return;
    }
    if (category.trimmed().isEmpty())
    {
        this->updateStreamInfo(this->streamData_.title, std::nullopt,
                               this->streamData_.language);
        return;
    }

    auto weak = this->weakFromThis();
    NetworkRequest(QStringLiteral("https://itzon.tv/api/public/v1/categories"))
        .header("Authorization",
                QByteArrayLiteral("Bearer ") + account->token().toUtf8())
        .timeout(10000)
        .onSuccess([weak, category](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }
            for (const auto value : result.parseJson()["categories"].toArray())
            {
                const auto item = value.toObject();
                if (item["name"].toString().compare(category.trimmed(),
                                                    Qt::CaseInsensitive) == 0)
                {
                    self->updateStreamInfo(self->streamData_.title,
                                           item["id"].toInteger(),
                                           self->streamData_.language);
                    return;
                }
            }
            self->addSystemMessage(
                QStringLiteral("No itzon.tv category named ‘%1’ was found.")
                    .arg(category.trimmed()));
        })
        .onError([weak](const NetworkResult &result) {
            if (auto self = weak.lock())
            {
                self->addSystemMessage(
                    QStringLiteral("Failed to load itzon.tv categories: %1")
                        .arg(result.formatError()));
            }
        })
        .execute();
}

void ItzonChannel::setStreamLanguage(const QString &language)
{
    this->updateStreamInfo(this->streamData_.title,
                           this->streamData_.categoryID,
                           language.trimmed().isEmpty() ? QStringLiteral("und")
                                                        : language.trimmed());
}

}  // namespace chatterino
