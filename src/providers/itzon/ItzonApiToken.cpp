// SPDX-License-Identifier: MIT

#include "providers/itzon/ItzonApiToken.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "ItzonEmbeddedApiToken.hpp"
#include "providers/itzon/ItzonAccount.hpp"

namespace chatterino::itzon {

QByteArray apiToken()
{
    if (const auto account = getApp()->getAccounts()->itzon.current();
        account && account->isOAuth() && account->hasScope("api:read") &&
        account->accessTokenValid())
    {
        return account->token().toUtf8();
    }

    auto token = QByteArray(ITZON_EMBEDDED_API_TOKEN).trimmed();
    for (const auto byte : token)
    {
        const auto value = static_cast<unsigned char>(byte);
        if (value < 0x21 || value > 0x7E)
        {
            return {};
        }
    }
    return token;
}

}  // namespace chatterino::itzon
