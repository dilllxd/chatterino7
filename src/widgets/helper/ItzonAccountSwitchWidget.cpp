// SPDX-License-Identifier: MIT

#include "widgets/helper/ItzonAccountSwitchWidget.hpp"

#include "Application.hpp"
#include "common/Common.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/itzon/ItzonAccount.hpp"
#include "singletons/Settings.hpp"

namespace chatterino {

ItzonAccountSwitchWidget::ItzonAccountSwitchWidget(QWidget *parent)
    : QListWidget(parent)
{
    this->managedConnections_.managedConnect(
        getApp()->getAccounts()->itzon.userListUpdated, [this] {
            this->refreshItems();
            this->refresh();
        });
    this->managedConnections_.managedConnect(
        getApp()->getAccounts()->itzon.currentUserChanged, [this] {
            this->refresh();
        });
    this->refreshItems();
    this->refresh();
    QObject::connect(this, &QListWidget::clicked, this, [this] {
        if (auto *item = this->currentItem())
        {
            getApp()->getAccounts()->itzon.currentUsername =
                item->text().compare(ANONYMOUS_USERNAME_LABEL,
                                     Qt::CaseInsensitive) == 0
                    ? QString{}
                    : item->text();
            std::ignore = getSettings()->requestSave();
        }
    });
}

void ItzonAccountSwitchWidget::refreshItems()
{
    QSignalBlocker blocker(this);
    this->clear();
    this->addItem(ANONYMOUS_USERNAME_LABEL);
    for (const auto &username : getApp()->getAccounts()->itzon.usernames())
    {
        this->addItem(username);
    }
}

void ItzonAccountSwitchWidget::refresh()
{
    QSignalBlocker blocker(this);
    auto current = getApp()->getAccounts()->itzon.current();
    if (!current)
    {
        this->setCurrentRow(0);
        return;
    }
    for (int i = 0; i < this->count(); ++i)
    {
        if (this->item(i)->text().compare(current->username(),
                                          Qt::CaseInsensitive) == 0)
        {
            this->setCurrentRow(i);
            return;
        }
    }
}

}  // namespace chatterino
