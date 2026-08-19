// SPDX-License-Identifier: MIT

#include "controllers/accounts/AccountModel.hpp"

#include "controllers/accounts/Account.hpp"
#include "util/SharedPtrElementLess.hpp"

#include <gtest/gtest.h>
#include <QStringList>

#include <memory>
#include <utility>

namespace chatterino {
namespace {

class TestAccount : public Account
{
public:
    TestAccount(ProviderId provider, QString name)
        : Account(provider)
        , name_(std::move(name))
    {
    }

    QString toString() const override
    {
        return this->name_;
    }

private:
    QString name_;
};

QStringList rows(const AccountModel &model)
{
    QStringList result;
    for (int row = 0; row < model.rowCount({}); ++row)
    {
        result.emplace_back(
            model.data(model.index(row, 0), Qt::DisplayRole).toString());
    }
    return result;
}

TEST(AccountModel, KeepsAccountsUnderTheirProviderHeader)
{
    SignalVector<std::shared_ptr<Account>> accounts(
        SharedPtrElementLess<Account>{});
    AccountModel model(nullptr);
    model.initialize(&accounts);

    accounts.insert(std::make_shared<TestAccount>(ProviderId::Itzon, "axel"));
    accounts.insert(std::make_shared<TestAccount>(ProviderId::Kick, "kick"));
    accounts.insert(
        std::make_shared<TestAccount>(ProviderId::Twitch, "dilllxd"));
    accounts.insert(std::make_shared<TestAccount>(ProviderId::Kick, "alt"));

    EXPECT_EQ(rows(model), QStringList({"Kick", "alt", "kick", "Twitch",
                                        "dilllxd", "itzon.tv", "axel"}));
}

}  // namespace
}  // namespace chatterino
