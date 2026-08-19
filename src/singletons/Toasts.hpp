// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <pajlada/settings/setting.hpp>
#include <QString>

#include <cstdint>

namespace chatterino {

enum class ToastReaction {
    OpenInBrowser = 0,
    OpenInPlayer = 1,
    OpenInStreamlink = 2,
    DontOpen = 3,
    OpenInCustomPlayer = 4,
};

enum class ToastPlatform : std::uint8_t {
    Twitch,
    Itzon,
};

class Toasts final
{
public:
    ~Toasts();

    void sendChannelNotification(
        const QString &channelName, const QString &channelTitle,
        ToastPlatform platform = ToastPlatform::Twitch);
    static QString findStringFromReaction(const ToastReaction &reaction);
    static QString findStringFromReaction(
        const pajlada::Settings::Setting<int> &reaction);

    static bool isEnabled();

private:
#ifdef Q_OS_WIN
    void ensureInitialized();
    void sendWindowsNotification(const QString &channelName,
                                 const QString &channelTitle,
                                 ToastPlatform platform);

    bool initialized_ = false;
#elif defined(CHATTERINO_WITH_LIBNOTIFY)
    void ensureInitialized();
    void sendLibnotify(const QString &channelName, const QString &channelTitle,
                       ToastPlatform platform);

    bool initialized_ = false;
#endif
};
}  // namespace chatterino
