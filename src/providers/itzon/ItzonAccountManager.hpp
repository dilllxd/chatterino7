// SPDX-License-Identifier: MIT

#pragma once

#include "common/SignalVector.hpp"

#include <pajlada/settings/setting.hpp>
#include <pajlada/signals/signal.hpp>
#include <QString>
#include <QTimer>

namespace chatterino {

class ItzonAccount;
struct ItzonAccountData;

class ItzonAccountManager
{
public:
    ItzonAccountManager();

    void load();
    void reloadUsers();
    bool add(const QString &username, const QString &token);
    bool addOAuth(ItzonAccountData data);
    std::shared_ptr<ItzonAccount> current() const;
    std::shared_ptr<ItzonAccount> findUserByUsername(
        const QString &username) const;
    std::vector<QString> usernames() const;

    pajlada::Settings::Setting<QString> currentUsername{
        "/itzonAccounts/current", ""};
    pajlada::Signals::NoArgSignal currentUserChanged;
    pajlada::Signals::NoArgSignal userListUpdated;
    SignalVector<std::shared_ptr<ItzonAccount>> accounts;

private:
    bool addAccount(ItzonAccountData data);
    bool removeAccount(ItzonAccount *account);
    void refreshAccounts() const;
    std::shared_ptr<ItzonAccount> currentUser_;
    QTimer refreshTimer_;
};

}  // namespace chatterino
