// SPDX-License-Identifier: MIT

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <vector>

namespace chatterino::itzon {

struct Follower {
    QString username;
    QDateTime followedAt;
};

struct FollowerPage {
    quint64 total = 0;
    std::vector<Follower> followers;
    QString nextCursor;
};

std::optional<FollowerPage> parseFollowerPage(const QJsonObject &json);

}  // namespace chatterino::itzon
