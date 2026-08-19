// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/accounts/AccountModel.hpp"

#include "controllers/accounts/Account.hpp"
#include "util/StandardItemHelper.hpp"

namespace chatterino {

AccountModel::AccountModel(QObject *parent)
    : SignalVectorModel<std::shared_ptr<Account>>(1, parent)
{
}

// turn a vector item into a model row
std::shared_ptr<Account> AccountModel::getItemFromRow(
    std::vector<QStandardItem *> &, const std::shared_ptr<Account> &original)
{
    return original;
}

// turns a row in the model into a vector item
void AccountModel::getRowFromItem(const std::shared_ptr<Account> &item,
                                  std::vector<QStandardItem *> &row)
{
    setStringItem(row[0], item->toString(), false);
    row[0]->setData(QFont("Segoe UI", 10), Qt::FontRole);
}

int AccountModel::beforeInsert(const std::shared_ptr<Account> &item,
                               std::vector<QStandardItem *> &row,
                               int proposedIndex)
{
    (void)row;
    (void)proposedIndex;

    const auto category = item->getCategory();
    const bool firstInCategory = this->categoryCount_[category]++ == 0;
    if (firstInCategory)
    {
        auto newRow = this->createRow();

        setStringItem(newRow[0], category, false, false);
        newRow[0]->setData(QFont("Segoe UI Light", 16), Qt::FontRole);

        int index = 0;
        for (const auto &existingRow : this->rows())
        {
            if (existingRow.isCustomRow &&
                category < existingRow.items[0]->data(Qt::EditRole).toString())
            {
                this->insertCustomRow(std::move(newRow), index);
                return index + 1;
            }
            ++index;
        }

        this->insertCustomRow(std::move(newRow), index);
        return index + 1;
    }

    bool inCategory = false;
    int index = 0;
    for (const auto &existingRow : this->rows())
    {
        if (existingRow.isCustomRow)
        {
            if (inCategory)
            {
                return index;
            }
            inCategory =
                existingRow.items[0]->data(Qt::EditRole).toString() == category;
        }
        else if (inCategory && existingRow.original &&
                 item->operator<(*existingRow.original.value()))
        {
            return index;
        }
        ++index;
    }

    return index;
}

void AccountModel::afterRemoved(const std::shared_ptr<Account> &item,
                                std::vector<QStandardItem *> &row, int index)
{
    auto it = this->categoryCount_.find(item->getCategory());
    assert(it != this->categoryCount_.end());

    if (it->second <= 1)
    {
        this->categoryCount_.erase(it);
        this->removeCustomRow(index - 1);
    }
    else
    {
        it->second--;
    }
}

}  // namespace chatterino
