// SPDX-License-Identifier: MIT

#include "providers/itzon/ItzonAccountManager.hpp"

#include "providers/itzon/ItzonAccount.hpp"
#include "util/RapidJsonSerializeQString.hpp"  // IWYU pragma: keep
#include "util/SharedPtrElementLess.hpp"

#include <pajlada/settings/settingmanager.hpp>

#include <algorithm>

namespace chatterino {
namespace {

bool isSafeIrcAtom(const QString &value)
{
    return std::ranges::none_of(value, [](QChar character) {
        return character.isSpace() || character.unicode() < 0x20;
    });
}

}  // namespace

ItzonAccountManager::ItzonAccountManager()
    : accounts(SharedPtrElementLess<ItzonAccount>{})
{
    std::ignore = this->accounts.itemRemoved.connect([this](const auto &args) {
        this->removeAccount(args.item.get());
    });
    this->refreshTimer_.setInterval(std::chrono::minutes(2));
    QObject::connect(&this->refreshTimer_, &QTimer::timeout, [this] {
        this->refreshAccounts();
    });
    this->refreshTimer_.start();
}

void ItzonAccountManager::load()
{
    this->reloadUsers();
    this->currentUsername.connect([this](const QString &username) {
        this->currentUser_ = this->findUserByUsername(username);
        this->currentUserChanged.invoke();
    });
    this->currentUser_ = this->findUserByUsername(this->currentUsername);
    this->refreshAccounts();
}

void ItzonAccountManager::reloadUsers()
{
    bool changed = false;
    for (const auto &key :
         pajlada::Settings::SettingManager::getObjectKeys("/itzonAccounts"))
    {
        if (key == "current")
        {
            continue;
        }
        auto data = ItzonAccountData::loadRaw(key);
        if (!data || this->findUserByUsername(data->username))
        {
            continue;
        }
        this->accounts.insert(std::make_shared<ItzonAccount>(std::move(*data)));
        changed = true;
    }
    if (changed)
    {
        this->userListUpdated.invoke();
    }
}

bool ItzonAccountManager::add(const QString &username, const QString &token)
{
    auto cleanUsername = username.trimmed();
    auto cleanToken = token.trimmed();
    if (cleanUsername.isEmpty() || cleanToken.isEmpty() ||
        !isSafeIrcAtom(cleanUsername) || cleanToken.contains('\r') ||
        cleanToken.contains('\n') || cleanToken.contains(QChar::Null))
    {
        return false;
    }
    return this->addAccount(ItzonAccountData{cleanUsername, cleanToken});
}

bool ItzonAccountManager::addOAuth(ItzonAccountData data)
{
    data.username = data.username.trimmed();
    data.token = data.token.trimmed();
    data.refreshToken = data.refreshToken.trimmed();
    data.clientID = data.clientID.trimmed();
    if (data.authType != ItzonAuthType::OAuth || data.username.isEmpty() ||
        data.token.isEmpty() || data.refreshToken.isEmpty() ||
        data.clientID.isEmpty() || !data.expiresAt.isValid() ||
        !isSafeIrcAtom(data.username) ||
        !data.scope.split(' ', Qt::SkipEmptyParts)
             .contains(QStringLiteral("chat")))
    {
        return false;
    }
    return this->addAccount(std::move(data));
}

bool ItzonAccountManager::addAccount(ItzonAccountData data)
{
    if (auto existing = this->findUserByUsername(data.username))
    {
        this->accounts.removeFirstMatching(
            [&existing](const auto &account) {
                return account == existing;
            },
            this);
    }
    data.save();
    auto account = std::make_shared<ItzonAccount>(std::move(data));
    this->accounts.insert(account);
    this->userListUpdated.invoke();
    this->currentUsername = account->username();
    return true;
}

std::shared_ptr<ItzonAccount> ItzonAccountManager::current() const
{
    return this->currentUser_;
}

std::shared_ptr<ItzonAccount> ItzonAccountManager::findUserByUsername(
    const QString &username) const
{
    for (const auto &account : this->accounts.raw())
    {
        if (account->username().compare(username, Qt::CaseInsensitive) == 0)
        {
            return account;
        }
    }
    return {};
}

std::vector<QString> ItzonAccountManager::usernames() const
{
    std::vector<QString> result;
    for (const auto &account : this->accounts.raw())
    {
        result.emplace_back(account->username());
    }
    return result;
}

bool ItzonAccountManager::removeAccount(ItzonAccount *account)
{
    pajlada::Settings::SettingManager::gRemoveSetting("/itzonAccounts/" +
                                                      account->storageKey());
    if (account->username().compare(this->currentUsername,
                                    Qt::CaseInsensitive) == 0)
    {
        this->currentUsername = "";
    }
    this->userListUpdated.invoke();
    return true;
}

void ItzonAccountManager::refreshAccounts() const
{
    for (const auto &account : this->accounts.raw())
    {
        account->refreshIfNeeded();
    }
}

}  // namespace chatterino
