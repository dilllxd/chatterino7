// SPDX-License-Identifier: MIT

#include "providers/itzon/ItzonBadges.hpp"

#include "common/Literals.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"

#include <QHash>
#include <QRegularExpression>

#include <optional>

namespace chatterino {
namespace {

using namespace literals;

struct BadgeData {
    QString assetName;
    QString title;
    MessageElementFlag flag;
};

std::optional<BadgeData> roleData(QStringView name)
{
    if (name == u"staff")
    {
        return BadgeData{u"staff"_s, u"Staff"_s,
                         MessageElementFlag::BadgeGlobalAuthority};
    }
    if (name == u"op")
    {
        return BadgeData{u"op"_s, u"Owner"_s,
                         MessageElementFlag::BadgeChannelAuthority};
    }
    if (name == u"bot")
    {
        return BadgeData{u"bot"_s, u"Bot"_s, MessageElementFlag::BadgeVanity};
    }
    if (name == u"mod")
    {
        return BadgeData{u"mod"_s, u"Mod"_s,
                         MessageElementFlag::BadgeChannelAuthority};
    }
    if (name == u"partner")
    {
        return BadgeData{u"partner"_s, u"Partner"_s,
                         MessageElementFlag::BadgeVanity};
    }
    if (name == u"vip")
    {
        return BadgeData{u"vip"_s, u"VIP"_s,
                         MessageElementFlag::BadgeChannelAuthority};
    }
    if (name == u"unverified")
    {
        return BadgeData{u"unverified"_s, u"Unverified"_s,
                         MessageElementFlag::BadgeVanity};
    }
    return std::nullopt;
}

QString subscriberTitle(const QString &name)
{
    static const QHash<QString, QString> TITLES{
        {u"regular"_s, u"Regular"_s},
        {u"ambassador"_s, u"Ambassador - one of the first"_s},
        {u"bounty"_s, u"Bug bounty - found a critical bug"_s},
        {u"invite"_s, u"Recruiter - invited a friend"_s},
        {u"lucky"_s, u"Lucky - one in a million"_s},
        {u"partner"_s, u"Partner"_s},
    };
    if (auto it = TITLES.constFind(name); it != TITLES.cend())
    {
        return *it;
    }

    auto words = name.split('_', Qt::SkipEmptyParts);
    for (auto &word : words)
    {
        word[0] = word[0].toUpper();
    }
    return words.join(' ');
}

std::pair<EmotePtr, MessageElementFlag> makeBadge(const BadgeData &data)
{
    assertInGuiThread();
    static QHash<QString, std::weak_ptr<const Emote>> cache;
    const auto pathName = QString(data.assetName).replace('_', '-');
    const auto url =
        u"https://itzon.tv/static/img/badge-"_s + pathName + u".svg"_s;
    const auto cacheKey = data.title + QChar::Null + url;
    auto emote = cache.value(cacheKey).lock();
    if (!emote)
    {
        emote = std::make_shared<const Emote>(Emote{
            .name = {data.title},
            .images = ImageSet{Image::fromAutoscaledUrl({url}, 18)},
            .tooltip = Tooltip{data.title},
        });
        cache.insert(cacheKey, emote);
    }
    return {emote, data.flag};
}

}  // namespace

std::pair<EmotePtr, MessageElementFlag> ItzonBadges::role(QStringView name)
{
    auto data = roleData(name);
    if (!data)
    {
        return {nullptr, MessageElementFlag::None};
    }
    return makeBadge(*data);
}

std::pair<EmotePtr, MessageElementFlag> ItzonBadges::subscriber(
    QStringView name)
{
    const auto sanitized = sanitizeSubscriberName(name);
    return makeBadge({sanitized, subscriberTitle(sanitized),
                      MessageElementFlag::BadgeSubscription});
}

QString ItzonBadges::sanitizeSubscriberName(QStringView name)
{
    static const QRegularExpression valid{QStringLiteral("^[a-z0-9_]{1,24}$")};
    const auto value = name.toString();
    return valid.match(value).hasMatch() ? value : u"regular"_s;
}

}  // namespace chatterino
