// SPDX-License-Identifier: MIT

#include "providers/itzon/ItzonFollowers.hpp"

#include <QJsonArray>

#include <algorithm>

namespace chatterino::itzon {

std::optional<FollowerPage> parseFollowerPage(const QJsonObject &json)
{
    if (!json["total"].isDouble() || !json["followers"].isArray())
    {
        return std::nullopt;
    }

    FollowerPage page;
    page.total =
        static_cast<quint64>(std::max<qint64>(0, json["total"].toInteger()));
    for (const auto value : json["followers"].toArray())
    {
        const auto item = value.toObject();
        auto username = item["username"].toString().trimmed();
        auto followedAt =
            QDateTime::fromString(item["followedAt"].toString(), Qt::ISODate);
        if (username.isEmpty() || !followedAt.isValid())
        {
            return std::nullopt;
        }
        page.followers.push_back({std::move(username), std::move(followedAt)});
    }

    if (json["nextCursor"].isString())
    {
        page.nextCursor = json["nextCursor"].toString();
    }
    else if (!json["nextCursor"].isNull() && !json["nextCursor"].isUndefined())
    {
        return std::nullopt;
    }
    return page;
}

}  // namespace chatterino::itzon
