// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Toasts.hpp"

#include "Application.hpp"
#include "common/Common.hpp"
#include "common/Literals.hpp"
#include "common/QLogging.hpp"
#include "common/Version.hpp"
#include "controllers/notifications/NotificationController.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "singletons/StreamerMode.hpp"
#include "util/CustomPlayer.hpp"
#include "util/StreamLink.hpp"
#include "widgets/helper/CommonTexts.hpp"

#ifdef Q_OS_WIN
#    include <wintoastlib.h>
#elif defined(CHATTERINO_WITH_LIBNOTIFY)
#    include <libnotify/notify.h>
#endif

#include <QDesktopServices>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStringBuilder>
#include <QUrl>

#include <utility>

namespace {

using namespace chatterino;
using namespace literals;

QString avatarFilePath(const QString &channelName)
{
    // TODO: cleanup channel (to be used as a file) and use combinePath
    return getApp()->getPaths().twitchProfileAvatars % '/' % channelName %
           u".png";
}

bool hasAvatarForChannel(const QString &channelName)
{
    QFileInfo avatarFile(avatarFilePath(channelName));
    return avatarFile.exists() && avatarFile.isFile();
}

/// A job that downlaods a twitch avatar and saves it to a file
class AvatarDownloader : public QObject
{
    Q_OBJECT
public:
    AvatarDownloader(const QString &avatarURL, const QString &channelName);

private:
    QNetworkAccessManager manager_;
    QFile file_;
    QNetworkReply *reply_{};

Q_SIGNALS:
    void downloadComplete();
};

QString channelUrl(const ToastPlatform platform, const QString &channelName)
{
    if (platform == ToastPlatform::Itzon)
    {
        return u"https://itzon.tv/" % channelName;
    }
    return u"https://www.twitch.tv/" % channelName;
}

void performReaction(const ToastReaction &reaction, const QString &channelName,
                     const ToastPlatform platform)
{
    const auto url = channelUrl(platform, channelName);
    switch (reaction)
    {
        case ToastReaction::OpenInBrowser:
            QDesktopServices::openUrl(QUrl(url));
            break;
        case ToastReaction::OpenInPlayer:
            QDesktopServices::openUrl(
                platform == ToastPlatform::Itzon
                    ? QUrl(url)
                    : QUrl(TWITCH_PLAYER_URL.arg(channelName)));
            break;
        case ToastReaction::OpenInStreamlink: {
            openStreamlinkForChannelOrUrl(
                platform == ToastPlatform::Itzon ? url : channelName);
            break;
        }
        case ToastReaction::OpenInCustomPlayer: {
            openInCustomPlayer(channelName,
                               platform == ToastPlatform::Itzon
                                   ? QStringView{u"https://itzon.tv/"}
                                   : QStringView{u"https://www.twitch.tv/"});
            break;
        }
        case ToastReaction::DontOpen:
            // nothing should happen
            break;
    }
}

#ifdef CHATTERINO_WITH_LIBNOTIFY
struct ToastActionData {
    QString channelName;
    ToastPlatform platform;
};

void onAction(NotifyNotification *notif, const char *actionRaw, void *userData)
{
    QString action(actionRaw);
    auto *data = static_cast<ToastActionData *>(userData);

    // by default we perform the action that is specified in the settings
    auto toastReaction =
        static_cast<ToastReaction>(getSettings()->openFromToast.getValue());

    if (action == OPEN_IN_BROWSER)
    {
        toastReaction = ToastReaction::OpenInBrowser;
    }
    else if (action == OPEN_PLAYER_IN_BROWSER)
    {
        toastReaction = ToastReaction::OpenInPlayer;
    }
    else if (action == OPEN_IN_STREAMLINK)
    {
        toastReaction = ToastReaction::OpenInStreamlink;
    }
    else if (action == OPEN_IN_CUSTOM_PLAYER)
    {
        toastReaction = ToastReaction::OpenInCustomPlayer;
    }

    performReaction(toastReaction, data->channelName, data->platform);

    notify_notification_close(notif, nullptr);
}

void onActionClosed(NotifyNotification *notif, void * /*userData*/)
{
    g_object_unref(notif);
}

void onNotificationDestroyed(void *data)
{
    delete static_cast<ToastActionData *>(data);
}
#endif

}  // namespace

namespace chatterino {

#ifdef Q_OS_WIN
using WinToastLib::WinToast;
using WinToastLib::WinToastTemplate;
#endif

Toasts::~Toasts()
{
#ifdef Q_OS_WIN
    if (this->initialized_)
    {
        WinToast::instance()->clear();
    }
#elif defined(CHATTERINO_WITH_LIBNOTIFY)
    if (this->initialized_)
    {
        notify_uninit();
    }
#endif
}

bool Toasts::isEnabled()
{
    auto enabled = getSettings()->notificationToast &&
                   !(getApp()->getStreamerMode()->isEnabled() &&
                     getSettings()->streamerModeSuppressLiveNotifications);

#ifdef Q_OS_WIN
    enabled = enabled && WinToast::isCompatible();
#endif

    return enabled;
}

QString Toasts::findStringFromReaction(const ToastReaction &reaction)
{
    switch (reaction)
    {
        case ToastReaction::OpenInBrowser:
            return OPEN_IN_BROWSER;
        case ToastReaction::OpenInPlayer:
            return OPEN_PLAYER_IN_BROWSER;
        case ToastReaction::OpenInStreamlink:
            return OPEN_IN_STREAMLINK;
        case ToastReaction::DontOpen:
            return DONT_OPEN;
        case ToastReaction::OpenInCustomPlayer:
            return OPEN_IN_CUSTOM_PLAYER;
        default:
            return DONT_OPEN;
    }
}

QString Toasts::findStringFromReaction(
    const pajlada::Settings::Setting<int> &reaction)
{
    static_assert(std::is_same_v<std::underlying_type_t<ToastReaction>, int>);
    int value = reaction;
    return Toasts::findStringFromReaction(static_cast<ToastReaction>(value));
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Toasts::sendChannelNotification(const QString &channelName,
                                     const QString &channelTitle,
                                     ToastPlatform platform)
{
#ifdef Q_OS_WIN
    auto sendChannelNotification = [this, channelName, channelTitle, platform] {
        this->sendWindowsNotification(channelName, channelTitle, platform);
    };
#elif defined(CHATTERINO_WITH_LIBNOTIFY)
    auto sendChannelNotification = [this, channelName, channelTitle, platform] {
        this->sendLibnotify(channelName, channelTitle, platform);
    };
#else
    (void)channelTitle;
    auto sendChannelNotification = [] {
        // Unimplemented for macOS
    };
#endif
    // Fetch user profile avatar
    if (platform == ToastPlatform::Itzon || hasAvatarForChannel(channelName))
    {
        sendChannelNotification();
    }
    else
    {
        getHelix()->getUserByName(
            channelName,
            [channelName, sendChannelNotification](const auto &user) {
                // gets deleted when finished
                auto *downloader =
                    new AvatarDownloader(user.profileImageUrl, channelName);
                QObject::connect(downloader,
                                 &AvatarDownloader::downloadComplete,
                                 sendChannelNotification);
            },
            [] {
                // on failure
            });
    }
}

#ifdef Q_OS_WIN

class CustomHandler : public WinToastLib::IWinToastHandler
{
private:
    QString channelName_;
    ToastPlatform platform_;

public:
    CustomHandler(QString channelName, ToastPlatform platform)
        : channelName_(std::move(channelName))
        , platform_(platform)
    {
    }
    void toastActivated() const override
    {
        auto toastReaction =
            static_cast<ToastReaction>(getSettings()->openFromToast.getValue());

        performReaction(toastReaction, channelName_, platform_);
    }

    void toastActivated(int actionIndex) const override
    {
    }

    void toastActivated(std::wstring response) const override
    {
    }

    void toastFailed() const override
    {
    }

    void toastDismissed(WinToastDismissalReason state) const override
    {
    }
};

void Toasts::ensureInitialized()
{
    if (this->initialized_)
    {
        return;
    }
    this->initialized_ = true;

    auto *instance = WinToast::instance();
    instance->setAppName(L"Chatterino7TV");
    instance->setAppUserModelId(Version::instance().appUserModelID());
    if (!getSettings()->createShortcutForToasts)
    {
        instance->setShortcutPolicy(WinToast::SHORTCUT_POLICY_IGNORE);
    }
    WinToast::WinToastError error{};
    instance->initialize(&error);

    if (error != WinToast::NoError)
    {
        qCDebug(chatterinoNotification)
            << "Failed to initialize WinToast - error:" << error;
    }
}

void Toasts::sendWindowsNotification(const QString &channelName,
                                     const QString &channelTitle,
                                     ToastPlatform platform)
{
    this->ensureInitialized();

    WinToastTemplate templ(WinToastTemplate::ImageAndText03);
    QString str = channelName % u" is live!";

    templ.setTextField(str.toStdWString(), WinToastTemplate::FirstLine);
    if (static_cast<ToastReaction>(getSettings()->openFromToast.getValue()) !=
        ToastReaction::DontOpen)
    {
        QString mode =
            Toasts::findStringFromReaction(getSettings()->openFromToast);
        mode = mode.toLower();

        templ.setTextField(
            u"%1 \nClick to %2"_s.arg(channelTitle).arg(mode).toStdWString(),
            WinToastTemplate::SecondLine);
    }

    const auto avatarPath = avatarFilePath(channelName);
    if (QFileInfo::exists(avatarPath))
    {
        templ.setImagePath(avatarPath.toStdWString());
    }
    if (getSettings()->notificationPlaySound)
    {
        templ.setAudioOption(WinToastTemplate::AudioOption::Silent);
    }

    WinToast::WinToastError error = WinToast::NoError;
    WinToast::instance()->showToast(
        templ, new CustomHandler(channelName, platform), &error);
    if (error != WinToast::NoError)
    {
        qCWarning(chatterinoNotification) << "Failed to show toast:" << error;
    }
}

#elif defined(CHATTERINO_WITH_LIBNOTIFY)

void Toasts::ensureInitialized()
{
    if (this->initialized_)
    {
        return;
    }
    auto result = notify_init("Chatterino 7TV");

    if (result == 0)
    {
        qCWarning(chatterinoNotification) << "Failed to initialize libnotify";
    }
    this->initialized_ = true;
}

void Toasts::sendLibnotify(const QString &channelName,
                           const QString &channelTitle, ToastPlatform platform)
{
    this->ensureInitialized();

    qCDebug(chatterinoNotification) << "sending to libnotify";

    QString str = channelName % u" is live!";

    NotifyNotification *notif = notify_notification_new(
        str.toUtf8().constData(), channelTitle.toUtf8().constData(), nullptr);

    notify_notification_set_hint(
        notif, "desktop-entry",
        g_variant_new_string("com.chatterino.chatterino"));

    // this will be freed in onNotificationDestroyed
    auto *actionData = new ToastActionData{channelName, platform};

    // we only set onNotificationDestroyed as free_func in the first action
    // because all free_funcs will be called once the notification is destroyed
    // which would cause a double-free otherwise
    notify_notification_add_action(notif, OPEN_IN_BROWSER.toUtf8().constData(),
                                   OPEN_IN_BROWSER.toUtf8().constData(),
                                   (NotifyActionCallback)onAction, actionData,
                                   onNotificationDestroyed);
    notify_notification_add_action(
        notif, OPEN_PLAYER_IN_BROWSER.toUtf8().constData(),
        OPEN_PLAYER_IN_BROWSER.toUtf8().constData(),
        (NotifyActionCallback)onAction, actionData, nullptr);
    notify_notification_add_action(
        notif, OPEN_IN_STREAMLINK.toUtf8().constData(),
        OPEN_IN_STREAMLINK.toUtf8().constData(), (NotifyActionCallback)onAction,
        actionData, nullptr);
    if (!getSettings()->customURIScheme.getValue().isEmpty())
    {
        notify_notification_add_action(
            notif, OPEN_IN_CUSTOM_PLAYER.toUtf8().constData(),
            OPEN_IN_CUSTOM_PLAYER.toUtf8().constData(),
            (NotifyActionCallback)onAction, actionData, nullptr);
    }

    auto defaultToastReaction =
        static_cast<ToastReaction>(getSettings()->openFromToast.getValue());

    if (defaultToastReaction != ToastReaction::DontOpen)
    {
        notify_notification_add_action(
            notif, "default",
            Toasts::findStringFromReaction(defaultToastReaction)
                .toUtf8()
                .constData(),
            (NotifyActionCallback)onAction, actionData, nullptr);
    }

    GdkPixbuf *img =
        platform == ToastPlatform::Itzon
            ? nullptr
            : gdk_pixbuf_new_from_file(
                  avatarFilePath(channelName).toUtf8().constData(), nullptr);
    if (img == nullptr && platform != ToastPlatform::Itzon)
    {
        qWarning(chatterinoNotification) << "Failed to load user avatar image";
    }
    else
    {
        notify_notification_set_image_from_pixbuf(notif, img);
        g_object_unref(img);
    }

    g_signal_connect(notif, "closed", (GCallback)onActionClosed, nullptr);

    gboolean success = notify_notification_show(notif, nullptr);
    if (success == 0)
    {
        g_object_unref(notif);
    }
}
#endif

}  // namespace chatterino

namespace {

AvatarDownloader::AvatarDownloader(const QString &avatarURL,
                                   const QString &channelName)
    : file_(avatarFilePath(channelName))
{
    if (!this->file_.open(QFile::WriteOnly | QFile::Truncate))
    {
        qCWarning(chatterinoNotification)
            << "Failed to open avatar file" << this->file_.errorString();
    }

    this->reply_ = this->manager_.get(QNetworkRequest(avatarURL));

    connect(this->reply_, &QNetworkReply::readyRead, this, [this] {
        this->file_.write(this->reply_->readAll());
    });
    connect(this->reply_, &QNetworkReply::finished, this, [this] {
        if (this->reply_->error() != QNetworkReply::NoError)
        {
            qCWarning(chatterinoNotification)
                << "Failed to download avatar" << this->reply_->errorString();
        }

        if (this->file_.isOpen())
        {
            this->file_.close();
        }
        this->downloadComplete();
        this->deleteLater();
    });
}

#include "Toasts.moc"

}  // namespace
