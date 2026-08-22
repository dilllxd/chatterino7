// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace chatterino::itzon {

struct OAuthSession {
    QByteArray codeVerifier;
    QByteArray codeChallenge;
    QByteArray state;
};

QString oauthClientID();

OAuthSession createOAuthSession();

QUrl authorizationUrl(const QString &clientID, const QUrl &redirectURI,
                      const OAuthSession &session,
                      bool requestApiWrite = false);

}  // namespace chatterino::itzon
