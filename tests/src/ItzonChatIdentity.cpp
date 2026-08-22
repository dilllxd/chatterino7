// SPDX-License-Identifier: MIT

#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "providers/itzon/ItzonBadges.hpp"
#include "providers/itzon/ItzonChannel.hpp"
#include "providers/itzon/ItzonFollowers.hpp"

#include <gtest/gtest.h>
#include <QJsonArray>

#include <array>

using namespace chatterino;

TEST(ItzonChatIdentity, ParsesNamesPrefixes)
{
    auto [name, user] =
        ItzonChannel::parseNamesEntry(QStringLiteral("%@&+~*=?Alice"));

    EXPECT_EQ(name, QStringLiteral("Alice"));
    EXPECT_TRUE(user.staff);
    EXPECT_TRUE(user.owner);
    EXPECT_TRUE(user.bot);
    EXPECT_TRUE(user.moderator);
    EXPECT_TRUE(user.vip);
    EXPECT_TRUE(user.subscriber);
    EXPECT_TRUE(user.unverified);
    EXPECT_TRUE(user.guest);
}

TEST(ItzonChatIdentity, BuildsValidatedAvatarUrl)
{
    const auto url =
        ItzonChannel::avatarUrl(QStringLiteral("1017"), QStringLiteral("jpg"));
    ASSERT_TRUE(url);
    EXPECT_EQ(url->string,
              QStringLiteral("https://itzon.tv/api/live/avatar/1017.jpg"));

    EXPECT_FALSE(ItzonChannel::avatarUrl(QStringLiteral("../1017"),
                                         QStringLiteral("jpg")));
    EXPECT_FALSE(
        ItzonChannel::avatarUrl(QStringLiteral("1017"), QStringLiteral("svg")));
    EXPECT_FALSE(
        ItzonChannel::avatarUrl(QStringLiteral("1017"), QStringLiteral("JPG")));
}

TEST(ItzonChatIdentity, RetainsAndRemovesMemberMetadataCaseInsensitively)
{
    ItzonChannel channel(QStringLiteral("alice"));
    ItzonChannel::ChatUser user;
    user.moderator = true;
    user.partner = true;
    user.userID = QStringLiteral("1017");
    channel.setChatUser(QStringLiteral("SomeUser"), user);

    channel.retainChatUsers({QStringLiteral("SOMEUSER")});
    auto retained = channel.chatUser(QStringLiteral("someuser"));
    ASSERT_TRUE(retained);
    EXPECT_EQ(*retained, user);

    channel.removeChatUser(QStringLiteral("SOMEUSER"));
    EXPECT_FALSE(channel.chatUser(QStringLiteral("someuser")));
}

TEST(ItzonChatIdentity, TracksMemberMetadataChanges)
{
    ItzonChannel channel(QStringLiteral("alice"));
    int updates = 0;
    QStringList changedUsers;
    auto connection = channel.chatUserChanged.connect(
        [&updates, &changedUsers](const QString &name) {
            updates++;
            changedUsers.append(name);
        });

    ItzonChannel::ChatUser user;
    channel.setChatUser(QStringLiteral("SomeUser"), user);
    channel.setChatUser(QStringLiteral("someuser"), user);
    user.userID = QStringLiteral("1017");
    channel.setChatUser(QStringLiteral("SOMEUSER"), user);

    EXPECT_EQ(updates, 2);
    EXPECT_EQ(changedUsers, QStringList({QStringLiteral("someuser"),
                                         QStringLiteral("someuser")}));
}

TEST(ItzonChatIdentity, TracksMultiplePinnedMessages)
{
    ItzonChannel channel(QStringLiteral("alice"));
    channel.setPinnedMessage({
        .messageID = QStringLiteral("first"),
        .sender = QStringLiteral("bob"),
        .messageText = QStringLiteral("hello"),
    });
    channel.setPinnedMessage({
        .messageID = QStringLiteral("second"),
        .sender = QStringLiteral("carol"),
        .messageText = QStringLiteral("world"),
    });

    ASSERT_EQ(channel.pinnedMessageCount(), 2);
    ASSERT_NE(channel.getPinnedMessage(), nullptr);
    EXPECT_EQ(channel.getPinnedMessage()->messageID, QStringLiteral("second"));

    channel.removePinnedMessage(QStringLiteral("second"));
    ASSERT_NE(channel.getPinnedMessage(), nullptr);
    EXPECT_EQ(channel.getPinnedMessage()->messageID, QStringLiteral("first"));

    channel.clearPinnedMessages();
    EXPECT_EQ(channel.pinnedMessageCount(), 0);
    EXPECT_EQ(channel.getPinnedMessage(), nullptr);
}

TEST(ItzonChatIdentity, SanitizesSubscriberBadgeNames)
{
    EXPECT_EQ(ItzonBadges::sanitizeSubscriberName(QStringLiteral("ambassador")),
              QStringLiteral("ambassador"));
    EXPECT_EQ(ItzonBadges::sanitizeSubscriberName(QStringLiteral("founder_2")),
              QStringLiteral("founder_2"));
    EXPECT_EQ(ItzonBadges::sanitizeSubscriberName(QStringLiteral("../staff")),
              QStringLiteral("regular"));
    EXPECT_EQ(ItzonBadges::sanitizeSubscriberName(QString(25, 'a')),
              QStringLiteral("regular"));
}

TEST(ItzonChatIdentity, UsesOfficialBadgeAssetsAndCategories)
{
    auto [staff, staffFlag] = ItzonBadges::role(QStringLiteral("staff"));
    ASSERT_TRUE(staff);
    EXPECT_EQ(staffFlag, MessageElementFlag::BadgeGlobalAuthority);
    EXPECT_EQ(staff->images.getImage1()->url().string,
              QStringLiteral("https://itzon.tv/static/img/badge-staff.svg"));

    auto [subscriber, subscriberFlag] =
        ItzonBadges::subscriber(QStringLiteral("ambassador"));
    ASSERT_TRUE(subscriber);
    EXPECT_EQ(subscriberFlag, MessageElementFlag::BadgeSubscription);
    EXPECT_EQ(
        subscriber->images.getImage1()->url().string,
        QStringLiteral("https://itzon.tv/static/img/badge-ambassador.svg"));

    auto [unknown, unknownFlag] = ItzonBadges::role(QStringLiteral("unknown"));
    EXPECT_FALSE(unknown);
    EXPECT_EQ(unknownFlag, MessageElementFlag::None);
}

TEST(ItzonChatIdentity, UsesOfficialCosmeticBadgeNames)
{
    static constexpr std::array BADGES{
        std::pair{"regular", "Regular"},
        std::pair{"baron", "Baron"},
        std::pair{"king", "King"},
        std::pair{"founder", "Founder"},
        std::pair{"streak_30", "Every Day"},
        std::pair{"streak_365", "Full Orbit"},
        std::pair{"medal", "Medal"},
        std::pair{"twentyfour", "Twenty-Four"},
    };

    for (const auto &[assetName, title] : BADGES)
    {
        const auto [badge, flag] =
            ItzonBadges::subscriber(QString::fromUtf8(assetName));
        ASSERT_TRUE(badge);
        EXPECT_EQ(flag, MessageElementFlag::BadgeSubscription);
        EXPECT_EQ(badge->name.string, QString::fromUtf8(title));
        EXPECT_EQ(badge->images.getImage1()->url().string,
                  QStringLiteral("https://itzon.tv/static/img/badge-") +
                      QString::fromUtf8(assetName).replace('_', '-') +
                      QStringLiteral(".svg"));
    }
}

TEST(ItzonChatIdentity, ParsesFollowerPages)
{
    const auto page = itzon::parseFollowerPage(QJsonObject{
        {QStringLiteral("total"), 2},
        {QStringLiteral("followers"),
         QJsonArray{
             QJsonObject{{QStringLiteral("username"), QStringLiteral("newest")},
                         {QStringLiteral("followedAt"),
                          QStringLiteral("2026-08-05T18:24:11Z")}},
             QJsonObject{{QStringLiteral("username"), QStringLiteral("older")},
                         {QStringLiteral("followedAt"),
                          QStringLiteral("2026-08-04T09:02:58Z")}},
         }},
        {QStringLiteral("nextCursor"), QStringLiteral("opaque-cursor")},
    });

    ASSERT_TRUE(page);
    EXPECT_EQ(page->total, 2);
    ASSERT_EQ(page->followers.size(), 2);
    EXPECT_EQ(page->followers[0].username, QStringLiteral("newest"));
    EXPECT_EQ(page->followers[1].username, QStringLiteral("older"));
    EXPECT_EQ(page->nextCursor, QStringLiteral("opaque-cursor"));

    EXPECT_FALSE(itzon::parseFollowerPage(QJsonObject{
        {QStringLiteral("total"), 1},
        {QStringLiteral("followers"),
         QJsonArray{QJsonObject{
             {QStringLiteral("username"), QStringLiteral("broken")},
             {QStringLiteral("followedAt"), QStringLiteral("not-a-date")}}}},
    }));
}
