// SPDX-License-Identifier: MIT

#ifdef _WIN32
// clang-format off
#    include <Windows.h>
#    include <dpapi.h>
// clang-format on
#endif

#include "providers/itzon/ItzonAccount.hpp"

#include "common/ChatterinoSetting.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "singletons/Settings.hpp"

#include <pajlada/settings/settingmanager.hpp>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QUrlQuery>

#include <algorithm>

namespace chatterino {
namespace {

QString protectToken(const QString &token)
{
#ifdef Q_OS_WIN
    auto input = token.toUtf8();
    DATA_BLOB inputBlob{
        static_cast<DWORD>(input.size()),
        reinterpret_cast<BYTE *>(input.data()),
    };
    DATA_BLOB outputBlob{};
    if (!CryptProtectData(&inputBlob, L"Chatterino itzon.tv credential",
                          nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          &outputBlob))
    {
        return {};
    }
    QByteArray encrypted(reinterpret_cast<const char *>(outputBlob.pbData),
                         static_cast<qsizetype>(outputBlob.cbData));
    LocalFree(outputBlob.pbData);
    return QStringLiteral("dpapi:") + QString::fromLatin1(encrypted.toBase64());
#else
    return QStringLiteral("plain:") +
           QString::fromLatin1(token.toUtf8().toBase64());
#endif
}

QString unprotectToken(const QString &stored)
{
    if (stored.startsWith(u"plain:"))
    {
        return QString::fromUtf8(
            QByteArray::fromBase64(stored.mid(6).toUtf8()));
    }
#ifdef Q_OS_WIN
    if (!stored.startsWith(u"dpapi:"))
    {
        return {};
    }
    auto input = QByteArray::fromBase64(stored.mid(6).toUtf8());
    DATA_BLOB inputBlob{
        static_cast<DWORD>(input.size()),
        reinterpret_cast<BYTE *>(input.data()),
    };
    DATA_BLOB outputBlob{};
    if (!CryptUnprotectData(&inputBlob, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &outputBlob))
    {
        return {};
    }
    QByteArray clear(reinterpret_cast<const char *>(outputBlob.pbData),
                     static_cast<qsizetype>(outputBlob.cbData));
    LocalFree(outputBlob.pbData);
    return QString::fromUtf8(clear);
#else
    return {};
#endif
}

}  // namespace

std::string ItzonAccountData::storageKeyFor(const QString &username)
{
    auto hash = QCryptographicHash::hash(username.trimmed().toLower().toUtf8(),
                                         QCryptographicHash::Sha256)
                    .toHex();
    return "user" + hash.left(24).toStdString();
}

std::optional<ItzonAccountData> ItzonAccountData::loadRaw(
    const std::string &key)
{
    auto base = "/itzonAccounts/" + key;
    auto username = QStringSetting::get(base + "/username").trimmed();
    auto token = unprotectToken(QStringSetting::get(base + "/token"));
    if (username.isEmpty() || token.isEmpty())
    {
        return std::nullopt;
    }

    const auto authTypeName = QStringSetting::get(base + "/authType");
    const auto authType = authTypeName == QStringLiteral("oauth")
                              ? ItzonAuthType::OAuth
                              : ItzonAuthType::ChatBotToken;
    if (authType == ItzonAuthType::ChatBotToken)
    {
        return ItzonAccountData{username, token};
    }

    auto refreshToken =
        unprotectToken(QStringSetting::get(base + "/refreshToken"));
    auto clientID = QStringSetting::get(base + "/clientID").trimmed();
    auto expiresAt = QDateTime::fromString(
        QStringSetting::get(base + "/expiresAt"), Qt::ISODate);
    if (refreshToken.isEmpty() || clientID.isEmpty() || !expiresAt.isValid())
    {
        return std::nullopt;
    }
    return ItzonAccountData{
        .username = username,
        .token = token,
        .authType = authType,
        .refreshToken = refreshToken,
        .clientID = clientID,
        .scope = QStringSetting::get(base + "/scope"),
        .userID = QStringSetting::get(base + "/userID"),
        .avatar = QStringSetting::get(base + "/avatar"),
        .expiresAt = expiresAt,
        .refreshExpiresAt = QDateTime::fromString(
            QStringSetting::get(base + "/refreshExpiresAt"), Qt::ISODate),
    };
}

void ItzonAccountData::save() const
{
    auto base = "/itzonAccounts/" + storageKeyFor(this->username);
    auto encrypted = protectToken(this->token);
    if (encrypted.isEmpty())
    {
        return;
    }

    QString encryptedRefresh;
    if (this->authType == ItzonAuthType::OAuth)
    {
        encryptedRefresh = protectToken(this->refreshToken);
        if (encryptedRefresh.isEmpty())
        {
            return;
        }
    }

    QStringSetting::set(base + "/username", this->username.trimmed());
    QStringSetting::set(base + "/token", encrypted);
    if (this->authType == ItzonAuthType::OAuth)
    {
        QStringSetting::set(base + "/authType", QStringLiteral("oauth"));
        QStringSetting::set(base + "/refreshToken", encryptedRefresh);
        QStringSetting::set(base + "/clientID", this->clientID);
        QStringSetting::set(base + "/scope", this->scope);
        QStringSetting::set(base + "/userID", this->userID);
        QStringSetting::set(base + "/avatar", this->avatar);
        QStringSetting::set(base + "/expiresAt",
                            this->expiresAt.toString(Qt::ISODate));
        QStringSetting::set(base + "/refreshExpiresAt",
                            this->refreshExpiresAt.toString(Qt::ISODate));
    }
    else
    {
        QStringSetting::set(base + "/authType", QStringLiteral("chat-bot"));
    }
    std::ignore = getSettings()->requestSave();
}

ItzonAccount::ItzonAccount(ItzonAccountData data)
    : Account(ProviderId::Itzon)
    , username_(std::move(data.username))
    , token_(std::move(data.token))
    , authType_(data.authType)
    , refreshToken_(std::move(data.refreshToken))
    , clientID_(std::move(data.clientID))
    , scope_(std::move(data.scope))
    , userID_(std::move(data.userID))
    , avatar_(std::move(data.avatar))
    , expiresAt_(std::move(data.expiresAt))
    , refreshExpiresAt_(std::move(data.refreshExpiresAt))
{
}

ItzonAccount::~ItzonAccount() = default;

QString ItzonAccount::toString() const
{
    return this->username_;
}

const QString &ItzonAccount::username() const
{
    return this->username_;
}

const QString &ItzonAccount::token() const
{
    return this->token_;
}

const QString &ItzonAccount::userID() const
{
    return this->userID_;
}

const QString &ItzonAccount::avatar() const
{
    return this->avatar_;
}

bool ItzonAccount::isOAuth() const
{
    return this->authType_ == ItzonAuthType::OAuth;
}

bool ItzonAccount::hasScope(const QString &scope) const
{
    return this->scope_.split(' ', Qt::SkipEmptyParts).contains(scope);
}

bool ItzonAccount::accessTokenValid() const
{
    return !this->isOAuth() ||
           QDateTime::currentDateTimeUtc().addSecs(30) < this->expiresAt_;
}

void ItzonAccount::refreshIfNeeded()
{
    if (!this->isOAuth() || this->refreshPending_ ||
        QDateTime::currentDateTimeUtc().addSecs(5 * 60) < this->expiresAt_)
    {
        return;
    }
    this->refreshOAuthToken();
}

void ItzonAccount::refreshOAuthToken()
{
    this->refreshPending_ = true;
    QUrlQuery payload{
        {QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
        {QStringLiteral("client_id"), this->clientID_},
        {QStringLiteral("refresh_token"), this->refreshToken_},
    };
    auto weak = this->weak_from_this();
    NetworkRequest(QStringLiteral("https://itzon.tv/api/oauth/token"),
                   NetworkRequestType::Post)
        .header("Content-Type", "application/x-www-form-urlencoded")
        .hideRequestBody()
        .payload(payload.toString(QUrl::FullyEncoded).toUtf8())
        .timeout(20'000)
        .onSuccess([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }
            self->refreshPending_ = false;
            const auto json = result.parseJson();
            const auto accessToken = json["access_token"].toString();
            const auto refreshToken = json["refresh_token"].toString();
            const auto expiresIn = std::clamp<qint64>(
                json["expires_in"].toInteger(), 1, 24 * 60 * 60);
            if (accessToken.isEmpty() || refreshToken.isEmpty())
            {
                qCWarning(chatterinoIrc)
                    << "itzon OAuth refresh returned an invalid response for"
                    << self->username_;
                return;
            }
            self->token_ = accessToken;
            self->refreshToken_ = refreshToken;
            self->scope_ = json["scope"].toString(self->scope_);
            self->expiresAt_ =
                QDateTime::currentDateTimeUtc().addSecs(expiresIn);
            const auto refreshExpiresIn =
                json["refresh_expires_in"].toInteger();
            if (refreshExpiresIn > 0)
            {
                self->refreshExpiresAt_ =
                    QDateTime::currentDateTimeUtc().addSecs(refreshExpiresIn);
            }
            self->save();
            self->authUpdated.invoke();
        })
        .onError([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }
            self->refreshPending_ = false;
            const auto json = result.parseJson();
            qCWarning(chatterinoIrc)
                << "Failed to refresh itzon OAuth for" << self->username_
                << result.formatError() << json["error"].toString();
            if (json["error"].toString() == QStringLiteral("invalid_grant"))
            {
                self->reauthorizationRequired.invoke();
            }
        })
        .execute();
}

std::string ItzonAccount::storageKey() const
{
    return ItzonAccountData::storageKeyFor(this->username_);
}

void ItzonAccount::save() const
{
    ItzonAccountData{
        .username = this->username_,
        .token = this->token_,
        .authType = this->authType_,
        .refreshToken = this->refreshToken_,
        .clientID = this->clientID_,
        .scope = this->scope_,
        .userID = this->userID_,
        .avatar = this->avatar_,
        .expiresAt = this->expiresAt_,
        .refreshExpiresAt = this->refreshExpiresAt_,
    }
        .save();
}

}  // namespace chatterino
