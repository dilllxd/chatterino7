// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/notifications/NotificationController.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "controllers/notifications/NotificationModel.hpp"
#include "controllers/sound/ISoundController.hpp"
#include "messages/Link.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/itzon/ItzonApiToken.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"
#include "singletons/StreamerMode.hpp"
#include "singletons/Toasts.hpp"
#include "singletons/WindowManager.hpp"
#include "util/Helpers.hpp"

#include <QSet>
#include <QUrl>

#include <ranges>

namespace ranges = std::ranges;

namespace chatterino {

NotificationController::NotificationController()
{
    for (const QString &channelName : this->twitchSetting_.getValue())
    {
        this->channelMap[Platform::Twitch].append(channelName);
    }
    for (const QString &channelName : this->itzonSetting_.getValue())
    {
        this->channelMap[Platform::Itzon].append(channelName);
    }

    // We can safely ignore this signal connection since channelMap will always be destroyed
    // before the NotificationController
    std::ignore =
        this->channelMap[Platform::Twitch].delayedItemsChanged.connect([this] {
            this->twitchSetting_.setValue(
                this->channelMap[Platform::Twitch].raw());
        });
    std::ignore =
        this->channelMap[Platform::Itzon].delayedItemsChanged.connect([this] {
            this->itzonSetting_.setValue(
                this->channelMap[Platform::Itzon].raw());
            this->fetchItzonChannels();
        });

    QObject::connect(&this->liveStatusTimer_, &QTimer::timeout, [this] {
        this->fetchFakeChannels();
        this->fetchItzonChannels();
    });
    this->liveStatusTimer_.start(60 * 1000);
}

void NotificationController::initialize()
{
    this->fetchFakeChannels();
    this->fetchItzonChannels();
}

void NotificationController::updateChannelNotification(
    const QString &channelName, Platform p)
{
    if (this->isChannelNotified(channelName, p))
    {
        this->removeChannelNotification(channelName, p);
    }
    else
    {
        this->addChannelNotification(channelName, p);
    }
}

bool NotificationController::isChannelNotified(const QString &channelName,
                                               Platform p) const
{
    return ranges::any_of(this->channelMap.at(p).raw(), [&](const auto &name) {
        return name.compare(channelName, Qt::CaseInsensitive) == 0;
    });
}

void NotificationController::addChannelNotification(const QString &channelName,
                                                    Platform p)
{
    this->channelMap[p].append(channelName);
}

void NotificationController::removeChannelNotification(
    const QString &channelName, Platform p)
{
    for (std::vector<int>::size_type i = 0;
         i != this->channelMap[p].raw().size(); i++)
    {
        if (this->channelMap[p].raw()[i].compare(channelName,
                                                 Qt::CaseInsensitive) == 0)
        {
            this->channelMap[p].removeAt(static_cast<int>(i));
            i--;
        }
    }
}

void NotificationController::playSound() const
{
    QUrl highlightSoundUrl =
        getSettings()->notificationCustomSound
            ? QUrl::fromLocalFile(
                  getSettings()->notificationPathSound.getValue())
            : QUrl("qrc:/sounds/ping2.wav");

    getApp()->getSound()->play(highlightSoundUrl);
}

NotificationModel *NotificationController::createModel(QObject *parent,
                                                       Platform p)
{
    auto *model = new NotificationModel(parent);
    model->initialize(&this->channelMap[p]);
    return model;
}

void NotificationController::notifyTwitchChannelLive(
    const NotificationPayload &payload) const
{
    bool showNotification =
        !(getSettings()->suppressInitialLiveNotification &&
          payload.isInitialUpdate) &&
        !(getApp()->getStreamerMode()->isEnabled() &&
          getSettings()->streamerModeSuppressLiveNotifications);
    bool playedSound = false;

    if (showNotification &&
        this->isChannelNotified(payload.channelName, Platform::Twitch))
    {
        if (Toasts::isEnabled())
        {
            getApp()->getToasts()->sendChannelNotification(payload.channelName,
                                                           payload.title);
        }
        if (getSettings()->notificationPlaySound)
        {
            this->playSound();
            playedSound = true;
        }
        if (getSettings()->notificationFlashTaskbar)
        {
            getApp()->getWindows()->sendAlert();
        }
    }

    // Message in /live channel
    getApp()->getTwitch()->getLiveChannel()->addMessage(
        MessageBuilder::makeLiveMessage(payload.displayName, payload.channelId,
                                        payload.title),
        MessageContext::Original);

    // Notify on all channels with a ping sound
    if (showNotification && !playedSound &&
        getSettings()->notificationOnAnyChannel)
    {
        this->playSound();
    }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void NotificationController::notifyTwitchChannelOffline(const QString &id) const
{
    // "delete" old 'CHANNEL is live' message
    auto snapshot =
        getApp()->getTwitch()->getLiveChannel()->getMessageSnapshot(200);
    for (const auto &s : snapshot | std::views::reverse)
    {
        if (s->id == id)
        {
            s->flags.set(MessageFlag::Disabled);
            break;
        }
    }
}

void NotificationController::notifyItzonChannelLive(
    const NotificationPayload &payload) const
{
    bool showNotification =
        !(getSettings()->suppressInitialLiveNotification &&
          payload.isInitialUpdate) &&
        !(getApp()->getStreamerMode()->isEnabled() &&
          getSettings()->streamerModeSuppressLiveNotifications);
    bool playedSound = false;

    if (showNotification &&
        this->isChannelNotified(payload.channelName, Platform::Itzon))
    {
        if (Toasts::isEnabled())
        {
            getApp()->getToasts()->sendChannelNotification(
                payload.channelName, payload.title, ToastPlatform::Itzon);
        }
        if (getSettings()->notificationPlaySound)
        {
            this->playSound();
            playedSound = true;
        }
        if (getSettings()->notificationFlashTaskbar)
        {
            getApp()->getWindows()->sendAlert();
        }
    }

    getApp()->getTwitch()->getLiveChannel()->addMessage(
        MessageBuilder::makeLiveMessage(
            payload.displayName, payload.channelId, payload.title, {},
            {Link::Url,
             QStringLiteral("https://itzon.tv/") + payload.channelName}),
        MessageContext::Original);

    if (showNotification && !playedSound &&
        getSettings()->notificationOnAnyChannel)
    {
        this->playSound();
    }
}

void NotificationController::fetchItzonChannels()
{
    const auto &notified = this->channelMap[Platform::Itzon].raw();
    for (auto it = this->itzonChannels_.begin();
         it != this->itzonChannels_.end();)
    {
        const bool stillNotified = ranges::any_of(notified, [&](const auto &n) {
            return n.compare(it->first, Qt::CaseInsensitive) == 0;
        });
        if (!stillNotified)
        {
            it = this->itzonChannels_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    const auto apiToken = itzon::apiToken();
    QSet<QString> requested;
    for (const auto &channelName : notified)
    {
        const auto key = channelName.toLower();
        if (requested.contains(key))
        {
            continue;
        }
        requested.insert(key);

        if (apiToken.isEmpty())
        {
            this->fetchItzonBadge(channelName);
        }
        else
        {
            this->fetchItzonChannel(channelName, apiToken);
        }
    }
}

void NotificationController::fetchItzonChannel(const QString &channelName,
                                               const QByteArray &apiToken)
{
    QUrl url(QStringLiteral("https://itzon.tv"));
    url.setPath(QStringLiteral("/api/public/v1/channel/") + channelName);

    NetworkRequest(url)
        .header("Authorization", QByteArrayLiteral("Bearer ") + apiToken)
        .timeout(10000)
        .onSuccess([this, channelName](const NetworkResult &result) {
            const auto json = result.parseJson();
            if (!json["live"].isBool())
            {
                qCWarning(chatterinoNotification)
                    << "Invalid itzon.tv Public API response for" << channelName
                    << "- falling back to the public badge";
                this->fetchItzonBadge(channelName, true);
                return;
            }

            this->updateItzonChannel(
                channelName, json["username"].toString(channelName),
                json["title"].toString(), json["live"].toBool());
        })
        .onError([this, channelName](const NetworkResult &result) {
            qCWarning(chatterinoNotification)
                << "Failed to fetch itzon.tv Public API live status for"
                << channelName << result.formatError()
                << "- falling back to the public badge";
            this->fetchItzonBadge(channelName, true);
        })
        .execute();
}

void NotificationController::fetchItzonBadge(const QString &channelName,
                                             bool preserveKnownLive)
{
    QUrl url(QStringLiteral("https://itzon.tv"));
    url.setPath(QStringLiteral("/api/public/v1/badge/") + channelName +
                QStringLiteral(".json"));

    NetworkRequest(url)
        .timeout(10000)
        .onSuccess([this, channelName,
                    preserveKnownLive](const NetworkResult &result) {
            const auto json = result.parseJson();
            if (!json["live"].isBool())
            {
                qCWarning(chatterinoNotification)
                    << "Invalid itzon.tv live-status response for"
                    << channelName;
                return;
            }
            const bool live = json["live"].toBool();
            const auto previous = this->itzonChannels_.find(channelName);
            if (preserveKnownLive && !live &&
                previous != this->itzonChannels_.end() && previous->second)
            {
                return;
            }
            this->updateItzonChannel(
                channelName, json["username"].toString(channelName), {}, live);
        })
        .onError([channelName](const NetworkResult &result) {
            qCWarning(chatterinoNotification)
                << "Failed to fetch itzon.tv live status for" << channelName
                << result.formatError();
        })
        .execute();
}

void NotificationController::updateItzonChannel(const QString &channelName,
                                                const QString &displayName,
                                                const QString &title, bool live)
{
    auto [it, inserted] = this->itzonChannels_.emplace(channelName, live);
    if (!inserted && it->second == live)
    {
        return;
    }
    it->second = live;

    const auto messageID = QStringLiteral("itzon:") + channelName.toLower();
    if (!live)
    {
        if (!inserted)
        {
            this->notifyTwitchChannelOffline(messageID);
        }
        return;
    }

    this->notifyItzonChannelLive({
        .channelId = messageID,
        .channelName = channelName,
        .displayName = displayName,
        .title = title.trimmed().isEmpty() ? QStringLiteral("Live on itzon.tv")
                                           : title.trimmed(),
        .isInitialUpdate = inserted,
    });
}

void NotificationController::fetchFakeChannels()
{
    qCDebug(chatterinoNotification) << "fetching fake channels";

    QStringList channels;
    for (size_t i = 0; i < this->channelMap[Platform::Twitch].raw().size(); i++)
    {
        const auto &name = this->channelMap[Platform::Twitch].raw()[i];
        auto chan = getApp()->getTwitch()->getChannelOrEmpty(name);
        if (chan->isEmpty())
        {
            channels.push_back(name);
        }
        else
        {
            this->fakeChannels_.erase(name);
        }
    }

    for (const auto &batch : splitListIntoBatches(channels))
    {
        getHelix()->fetchStreams(
            {}, batch,
            [batch, this](const auto &streams) {
                std::map<QString, std::optional<HelixStream>,
                         QCompareCaseInsensitive>
                    liveStreams;
                for (const auto &stream : streams)
                {
                    liveStreams.emplace(stream.userLogin, stream);
                }

                for (const auto &name : batch)
                {
                    auto it = liveStreams.find(name);
                    if (it == liveStreams.end())
                    {
                        this->updateFakeChannel(name, std::nullopt);
                    }
                    else
                    {
                        this->updateFakeChannel(name, it->second);
                    }
                }
            },
            [batch]() {
                // we done fucked up.
                qCWarning(chatterinoNotification)
                    << "Failed to fetch live status for " << batch;
            },
            []() {
                // finally
            });
    }
}
void NotificationController::updateFakeChannel(
    const QString &channelName, const std::optional<HelixStream> &stream)
{
    bool live = stream.has_value();
    qCDebug(chatterinoNotification).nospace().noquote()
        << "[FakeTwitchChannel " << channelName
        << "] New live status: " << stream.has_value();

    auto channelIt = this->fakeChannels_.find(channelName);
    bool isInitialUpdate = false;
    if (channelIt == this->fakeChannels_.end())
    {
        channelIt = this->fakeChannels_
                        .emplace(channelName,
                                 FakeChannel{
                                     .id = {},
                                     .isLive = live,
                                 })
                        .first;
        isInitialUpdate = true;
    }
    if (channelIt->second.isLive == live && !isInitialUpdate)
    {
        return;  // nothing changed
    }

    if (live && channelIt->second.id.isNull())
    {
        channelIt->second.id = stream->userId;
    }

    channelIt->second.isLive = live;

    // Similar code can be found in TwitchChannel::onLiveStatusChange.
    // Since this is a fake channel, we don't send a live message in the
    // TwitchChannel.
    if (!live)
    {
        // Stream is offline
        this->notifyTwitchChannelOffline(channelIt->second.id);
        return;
    }

    this->notifyTwitchChannelLive({
        .channelId = stream->userId,
        .channelName = channelName,
        .displayName = stream->userName,
        .title = stream->title,
        .isInitialUpdate = isInitialUpdate,
    });
}

}  // namespace chatterino
