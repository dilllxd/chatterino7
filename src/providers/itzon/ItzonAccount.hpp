// SPDX-License-Identifier: MIT

#pragma once

#include "controllers/accounts/Account.hpp"

#include <pajlada/signals/signal.hpp>
#include <QDateTime>
#include <QString>

#include <memory>
#include <optional>
#include <string>

namespace chatterino {

enum class ItzonAuthType {
    ChatBotToken,
    OAuth,
};

struct ItzonAccountData {
    QString username;
    QString token;
    ItzonAuthType authType = ItzonAuthType::ChatBotToken;
    QString refreshToken;
    QString clientID;
    QString scope;
    QString userID;
    QString avatar;
    QDateTime expiresAt;
    QDateTime refreshExpiresAt;

    void save() const;
    static std::optional<ItzonAccountData> loadRaw(const std::string &key);
    static std::string storageKeyFor(const QString &username);
};

class ItzonAccount : public Account,
                     public std::enable_shared_from_this<ItzonAccount>
{
public:
    explicit ItzonAccount(ItzonAccountData data);
    ~ItzonAccount() override;

    QString toString() const override;
    const QString &username() const;
    const QString &token() const;
    const QString &userID() const;
    const QString &avatar() const;
    bool isOAuth() const;
    bool hasScope(const QString &scope) const;
    bool accessTokenValid() const;
    void refreshIfNeeded();
    std::string storageKey() const;
    void save() const;

    pajlada::Signals::NoArgSignal authUpdated;
    pajlada::Signals::NoArgSignal reauthorizationRequired;

private:
    void refreshOAuthToken();

    QString username_;
    QString token_;
    ItzonAuthType authType_ = ItzonAuthType::ChatBotToken;
    QString refreshToken_;
    QString clientID_;
    QString scope_;
    QString userID_;
    QString avatar_;
    QDateTime expiresAt_;
    QDateTime refreshExpiresAt_;
    bool refreshPending_ = false;
};

}  // namespace chatterino
