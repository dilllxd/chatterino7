// SPDX-License-Identifier: MIT

#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "providers/itzon/ItzonBadges.hpp"
#include "providers/itzon/ItzonChannel.hpp"

#include <gtest/gtest.h>

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
