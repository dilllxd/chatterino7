// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/ChatterListWidget.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "providers/itzon/ItzonChannel.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchAccount.hpp"  // IWYU pragma: keep
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Theme.hpp"
#include "util/Helpers.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace chatterino {

namespace {

QString formatVIPListError(HelixListVIPsError error, const QString &message)
{
    using Error = HelixListVIPsError;

    QString errorMessage = QString("Failed to list VIPs - ");

    switch (error)
    {
        case Error::Forwarded: {
            errorMessage += message;
        }
        break;

        case Error::Ratelimited: {
            errorMessage += "You are being ratelimited by Twitch. Try "
                            "again in a few seconds.";
        }
        break;

        case Error::UserMissingScope: {
            // TODO(pajlada): Phrase MISSING_REQUIRED_SCOPE
            errorMessage += "Missing required scope. "
                            "Re-login with your "
                            "account and try again.";
        }
        break;

        case Error::UserNotAuthorized: {
            // TODO(pajlada): Phrase MISSING_PERMISSION
            errorMessage += "You don't have permission to "
                            "perform that action.";
        }
        break;

        case Error::UserNotBroadcaster: {
            errorMessage +=
                "Due to Twitch restrictions, "
                "this command can only be used by the broadcaster. "
                "To see the list of VIPs you must use the Twitch website.";
        }
        break;

        case Error::Unknown: {
            errorMessage += "An unknown error has occurred.";
        }
        break;
    }
    return errorMessage;
}

QString formatModsError(HelixGetModeratorsError error, const QString &message)
{
    using Error = HelixGetModeratorsError;

    QString errorMessage = QString("Failed to get moderators: ");

    switch (error)
    {
        case Error::Forwarded: {
            errorMessage += message;
        }
        break;

        case Error::UserMissingScope: {
            errorMessage += "Missing required scope. "
                            "Re-login with your "
                            "account and try again.";
        }
        break;

        case Error::UserNotAuthorized: {
            errorMessage +=
                "Due to Twitch restrictions, "
                "this command can only be used by the broadcaster. "
                "To see the list of mods you must use the Twitch website.";
        }
        break;

        case Error::Unknown: {
            errorMessage += "An unknown error has occurred.";
        }
        break;
    }
    return errorMessage;
}

QString formatChattersError(HelixGetChattersError error, const QString &message)
{
    using Error = HelixGetChattersError;

    QString errorMessage = QString("Failed to get chatters: ");

    switch (error)
    {
        case Error::Forwarded: {
            errorMessage += message;
        }
        break;

        case Error::UserMissingScope: {
            errorMessage += "Missing required scope. "
                            "Re-login with your "
                            "account and try again.";
        }
        break;

        case Error::UserNotAuthorized: {
            errorMessage +=
                "Due to Twitch restrictions, "
                "this command can only be used by moderators. "
                "To see the list of chatters you must use the Twitch website.";
        }
        break;

        case Error::Unknown: {
            errorMessage += "An unknown error has occurred.";
        }
        break;
    }
    return errorMessage;
}

}  // namespace

ChatterListWidget::ChatterListWidget(const TwitchChannel *twitchChannel,
                                     QWidget *parent)
    : BaseWindow({}, parent)
{
    assert(twitchChannel != nullptr);
    this->initialize(twitchChannel->getName());

    auto *chattersList = this->chattersList_;
    auto *loadingLabel = this->loadingLabel_;

    auto loadChatters = [=, this](auto modList, auto vipList,
                                  bool isBroadcaster) {
        getHelix()->getChatters(
            twitchChannel->roomId(),
            getApp()->getAccounts()->twitch.getCurrent()->getUserId(), 50000,
            [=, this](const auto &chatters) {
                auto broadcaster = twitchChannel->getName().toLower();
                QStringList chatterList;
                QStringList modChatters;
                QStringList vipChatters;

                bool addedBroadcaster = false;
                for (auto chatter : chatters.chatters)
                {
                    chatter = chatter.toLower();

                    if (!addedBroadcaster && chatter == broadcaster)
                    {
                        addedBroadcaster = true;
                        this->addLabel("Broadcaster");
                        chattersList->addItem(broadcaster);
                        chattersList->addItem(new QListWidgetItem());
                        continue;
                    }

                    if (modList.contains(chatter))
                    {
                        modChatters.append(chatter);
                        continue;
                    }

                    if (vipList.contains(chatter))
                    {
                        vipChatters.append(chatter);
                        continue;
                    }

                    chatterList.append(chatter);
                }

                if (isBroadcaster)
                {
                    this->addUserList(std::move(modChatters), "Moderators");
                    this->addUserList(std::move(vipChatters), "VIPs");
                }
                else
                {
                    this->addLabel("Moderators");
                    chattersList->addItem(
                        "Moderators cannot check who is a moderator");
                    chattersList->addItem(new QListWidgetItem());

                    this->addLabel("VIPs");
                    chattersList->addItem(
                        "Moderators cannot check who is a VIP");
                    chattersList->addItem(new QListWidgetItem());
                }

                this->addUserList(std::move(chatterList), "Chatters");

                loadingLabel->hide();
                this->performListSearch();
            },
            [this](auto error, const auto &message) {
                auto errorMessage = formatChattersError(error, message);
                this->chattersList_->addItem(
                    this->formatListItem(errorMessage));
            });
    };

    // Only broadcaster can get vips, mods can get chatters
    if (twitchChannel->isBroadcaster())
    {
        // Add moderators
        getHelix()->getModerators(
            twitchChannel->roomId(), 1000,
            [=, this](const auto &mods) {
                QSet<QString> modList;
                for (const auto &mod : mods)
                {
                    modList.insert(mod.userName.toLower());
                }

                // Add vips
                getHelix()->getChannelVIPs(
                    twitchChannel->roomId(),
                    [=, this](const auto &vips) {
                        QSet<QString> vipList;
                        for (const auto &vip : vips)
                        {
                            vipList.insert(vip.userName.toLower());
                        }

                        // Add chatters
                        loadChatters(modList, vipList, true);
                    },
                    [this](auto error, const auto &message) {
                        auto errorMessage = formatVIPListError(error, message);
                        this->chattersList_->addItem(
                            this->formatListItem(errorMessage));
                    });
            },
            [this](auto error, const auto &message) {
                auto errorMessage = formatModsError(error, message);
                this->chattersList_->addItem(
                    this->formatListItem(errorMessage));
            });
    }
    else if (twitchChannel->hasModRights())
    {
        QSet<QString> modList;
        QSet<QString> vipList;
        loadChatters(modList, vipList, false);
    }
    else
    {
        chattersList->addItem(
            this->formatListItem("Due to Twitch restrictions, this feature is "
                                 "only \navailable for moderators."));
        chattersList->addItem(this->formatListItem(
            "If you would like to see the Chatter list, you "
            "must \nuse the Twitch website."));
        loadingLabel->hide();
    }
}

ChatterListWidget::ChatterListWidget(const ItzonChannel *itzonChannel,
                                     QWidget *parent)
    : BaseWindow({}, parent)
{
    assert(itzonChannel != nullptr);
    this->initialize(itzonChannel->getName());

    QStringList broadcaster;
    QStringList staff;
    QStringList bots;
    QStringList moderators;
    QStringList vips;
    QStringList chatters;
    const auto &metadata = itzonChannel->chatUsers();
    for (const auto &[lowerName, displayName] :
         itzonChannel->accessChatters()->all())
    {
        const auto name = displayName.isEmpty() ? lowerName : displayName;
        const auto user = metadata.value(lowerName);
        if (user.owner || lowerName.compare(itzonChannel->getName(),
                                            Qt::CaseInsensitive) == 0)
        {
            broadcaster.append(name);
        }
        else if (user.staff)
        {
            staff.append(name);
        }
        else if (user.bot)
        {
            bots.append(name);
        }
        else if (user.moderator)
        {
            moderators.append(name);
        }
        else if (user.vip)
        {
            vips.append(name);
        }
        else
        {
            chatters.append(name);
        }
    }

    this->addUserList(std::move(broadcaster), "Broadcaster");
    this->addUserList(std::move(staff), "Staff");
    this->addUserList(std::move(bots), "Bots");
    this->addUserList(std::move(moderators), "Moderators");
    this->addUserList(std::move(vips), "VIPs");
    this->addUserList(std::move(chatters), "Chatters");
    this->loadingLabel_->hide();
    this->performListSearch();
}

void ChatterListWidget::initialize(const QString &channelName)
{
    this->setWindowTitle("Chatter List - " + channelName);
    this->setAttribute(Qt::WA_DeleteOnClose);

    auto *dockVbox = new QVBoxLayout();
    this->searchBar_ = new QLineEdit(this);
    this->chattersList_ = new QListWidget();
    this->resultList_ = new QListWidget();
    this->loadingLabel_ = new QLabel("Loading...");
    this->searchBar_->setPlaceholderText("Search User...");

    QObject::connect(this->searchBar_, &QLineEdit::textEdited, this, [this] {
        this->performListSearch();
    });

    this->setMinimumWidth(300);

    auto listDoubleClick = [this](const QModelIndex &index) {
        const auto itemText = index.data().toString();

        if (!itemText.isEmpty())
        {
            this->userClicked(itemText);
        }
    };

    QObject::connect(this->chattersList_, &QListWidget::doubleClicked, this,
                     listDoubleClick);

    QObject::connect(this->resultList_, &QListWidget::doubleClicked, this,
                     listDoubleClick);

    HotkeyController::HotkeyMap actions{
        {"delete",
         [this](const std::vector<QString> &) -> QString {
             this->close();
             return "";
         }},
        {"accept", nullptr},
        {"reject", nullptr},
        {"scrollPage", nullptr},
        {"openTab", nullptr},
        {"search",
         [this](const std::vector<QString> &) -> QString {
             this->searchBar_->setFocus();
             this->searchBar_->selectAll();
             return "";
         }},
    };

    getApp()->getHotkeys()->shortcutsForCategory(HotkeyCategory::PopupWindow,
                                                 actions, this);

    dockVbox->addWidget(this->searchBar_);
    dockVbox->addWidget(this->loadingLabel_);
    dockVbox->addWidget(this->chattersList_);
    dockVbox->addWidget(this->resultList_);
    this->resultList_->hide();

    this->setStyleSheet(this->theme->splits.input.styleSheet);
    this->setLayout(dockVbox);
}

QListWidgetItem *ChatterListWidget::formatListItem(const QString &text) const
{
    auto *item = new QListWidgetItem(text);
    item->setFont(getApp()->getFonts()->getFont(FontStyle::ChatMedium, 1.0));
    return item;
}

void ChatterListWidget::addLabel(const QString &label)
{
    auto *item = this->formatListItem(label);
    item->setFlags(Qt::NoItemFlags);
    item->setForeground(this->theme->accent);
    this->chattersList_->addItem(item);
}

void ChatterListWidget::addUserList(QStringList users, const QString &label)
{
    if (users.isEmpty())
    {
        return;
    }
    users.sort(Qt::CaseInsensitive);
    this->addLabel(
        QString("%1 (%2)").arg(label, localizeNumbers(users.size())));
    for (const auto &user : users)
    {
        this->chattersList_->addItem(this->formatListItem(user));
    }
    this->chattersList_->addItem(new QListWidgetItem());
}

void ChatterListWidget::performListSearch()
{
    const auto query = this->searchBar_->text();
    if (query.isEmpty())
    {
        this->resultList_->hide();
        this->chattersList_->show();
        return;
    }

    const auto results =
        this->chattersList_->findItems(query, Qt::MatchContains);
    this->chattersList_->hide();
    this->resultList_->clear();
    for (const auto *item : results)
    {
        if (item->flags() != Qt::NoItemFlags && !item->text().isEmpty())
        {
            this->resultList_->addItem(this->formatListItem(item->text()));
        }
    }
    this->resultList_->show();
}

}  // namespace chatterino
