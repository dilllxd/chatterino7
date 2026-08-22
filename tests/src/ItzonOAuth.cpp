// SPDX-License-Identifier: MIT

#include "providers/itzon/ItzonOAuth.hpp"

#include <gtest/gtest.h>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QUrlQuery>

using namespace chatterino;

TEST(ItzonOAuth, CreatesValidS256Session)
{
    const auto session = itzon::createOAuthSession();
    const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9._~-]+$"));

    EXPECT_EQ(session.codeVerifier.size(), 43);
    EXPECT_TRUE(
        allowed.match(QString::fromLatin1(session.codeVerifier)).hasMatch());
    EXPECT_EQ(session.codeChallenge.size(), 43);
    EXPECT_EQ(session.state.size(), 43);

    const auto expected = QCryptographicHash::hash(session.codeVerifier,
                                                   QCryptographicHash::Sha256)
                              .toBase64(QByteArray::Base64UrlEncoding |
                                        QByteArray::OmitTrailingEquals);
    EXPECT_EQ(session.codeChallenge, expected);
}

TEST(ItzonOAuth, BuildsDocumentedPublicClientRequest)
{
    const auto session = itzon::createOAuthSession();
    const QUrl redirect(
        QStringLiteral("http://127.0.0.1:49152/oauth/itzon/callback"));
    const QString clientID = QStringLiteral("0123456789abcdef0123456789abcdef");
    const auto url = itzon::authorizationUrl(clientID, redirect, session);
    const QUrlQuery query(url);

    EXPECT_EQ(url.scheme(), QStringLiteral("https"));
    EXPECT_EQ(url.host(), QStringLiteral("itzon.tv"));
    EXPECT_EQ(url.path(), QStringLiteral("/oauth/authorize"));
    EXPECT_EQ(query.queryItemValue(QStringLiteral("client_id")), clientID);
    EXPECT_EQ(query.queryItemValue(QStringLiteral("redirect_uri")),
              redirect.toString());
    EXPECT_EQ(query.queryItemValue(QStringLiteral("response_type")),
              QStringLiteral("code"));
    EXPECT_EQ(query.queryItemValue(QStringLiteral("scope")),
              QStringLiteral("identity chat api:read"));
    EXPECT_EQ(query.queryItemValue(QStringLiteral("state")),
              QString::fromLatin1(session.state));
    EXPECT_EQ(query.queryItemValue(QStringLiteral("code_challenge")),
              QString::fromLatin1(session.codeChallenge));
    EXPECT_EQ(query.queryItemValue(QStringLiteral("code_challenge_method")),
              QStringLiteral("S256"));
    EXPECT_FALSE(query.hasQueryItem(QStringLiteral("client_secret")));
}

TEST(ItzonOAuth, RequestsWriteScopeOnlyWhenEnabled)
{
    const auto session = itzon::createOAuthSession();
    const QUrl redirect(
        QStringLiteral("http://127.0.0.1:49152/oauth/itzon/callback"));
    const auto url = itzon::authorizationUrl(
        QStringLiteral("0123456789abcdef0123456789abcdef"), redirect, session,
        true);

    EXPECT_EQ(QUrlQuery(url).queryItemValue(QStringLiteral("scope")),
              QStringLiteral("identity chat api:read api:write"));
}
