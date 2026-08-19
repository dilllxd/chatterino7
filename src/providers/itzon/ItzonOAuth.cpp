// SPDX-License-Identifier: MIT

#include "providers/itzon/ItzonOAuth.hpp"

#include "ItzonEmbeddedOAuthClient.hpp"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrlQuery>

#include <cstring>

namespace chatterino::itzon {
namespace {

QByteArray randomBytes(qsizetype size)
{
    QByteArray bytes(size, Qt::Uninitialized);
    auto *generator = QRandomGenerator::system();
    for (qsizetype offset = 0; offset < size; offset += 4)
    {
        const auto value = generator->generate();
        const auto remaining = std::min<qsizetype>(4, size - offset);
        std::memcpy(bytes.data() + offset, &value,
                    static_cast<size_t>(remaining));
    }
    return bytes;
}

QByteArray base64Url(const QByteArray &value)
{
    return value.toBase64(QByteArray::Base64UrlEncoding |
                          QByteArray::OmitTrailingEquals);
}

}  // namespace

QString oauthClientID()
{
    const auto clientID =
        QString::fromLatin1(ITZON_EMBEDDED_OAUTH_CLIENT_ID).trimmed();
    static const QRegularExpression valid(QStringLiteral("^[0-9a-fA-F]{32}$"));
    return valid.match(clientID).hasMatch() ? clientID : QString{};
}

OAuthSession createOAuthSession()
{
    const auto verifier = base64Url(randomBytes(32));
    const auto challenge = base64Url(
        QCryptographicHash::hash(verifier, QCryptographicHash::Sha256));
    return {
        .codeVerifier = verifier,
        .codeChallenge = challenge,
        .state = base64Url(randomBytes(32)),
    };
}

QUrl authorizationUrl(const QString &clientID, const QUrl &redirectURI,
                      const OAuthSession &session)
{
    QUrl url(QStringLiteral("https://itzon.tv/oauth/authorize"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), clientID);
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectURI.toString());
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("scope"),
                       QStringLiteral("identity chat api:read"));
    query.addQueryItem(QStringLiteral("state"),
                       QString::fromLatin1(session.state));
    query.addQueryItem(QStringLiteral("code_challenge"),
                       QString::fromLatin1(session.codeChallenge));
    query.addQueryItem(QStringLiteral("code_challenge_method"),
                       QStringLiteral("S256"));
    url.setQuery(query);
    return url;
}

}  // namespace chatterino::itzon
