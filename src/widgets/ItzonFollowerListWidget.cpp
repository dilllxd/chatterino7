// SPDX-License-Identifier: MIT

#include "widgets/ItzonFollowerListWidget.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/itzon/ItzonAccount.hpp"
#include "providers/itzon/ItzonApiToken.hpp"
#include "providers/itzon/ItzonChannel.hpp"
#include "providers/itzon/ItzonFollowers.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Theme.hpp"
#include "util/Helpers.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace chatterino {

ItzonFollowerListWidget::ItzonFollowerListWidget(const ItzonChannel *channel,
                                                 QWidget *parent)
    : BaseWindow({}, parent)
    , channelName_(channel->getName())
{
    this->setAttribute(Qt::WA_DeleteOnClose);
    this->setWindowTitle(
        QStringLiteral("Follower List - %1").arg(this->channelName_));
    this->setMinimumSize(360, 360);

    auto *layout = new QVBoxLayout(this);
    this->searchBar_ = new QLineEdit(this);
    this->searchBar_->setPlaceholderText(QStringLiteral("Search loaded users"));
    layout->addWidget(this->searchBar_);

    this->summaryLabel_ = new QLabel(QStringLiteral("Loading..."), this);
    layout->addWidget(this->summaryLabel_);

    this->followersList_ = new QListWidget(this);
    layout->addWidget(this->followersList_, 1);

    this->loadMore_ = new QPushButton(QStringLiteral("Load more"), this);
    this->loadMore_->setEnabled(false);
    layout->addWidget(this->loadMore_);

    QObject::connect(this->searchBar_, &QLineEdit::textChanged, this,
                     &ItzonFollowerListWidget::updateFilter);
    QObject::connect(this->loadMore_, &QPushButton::clicked, this,
                     &ItzonFollowerListWidget::loadNextPage);
    QObject::connect(this->followersList_, &QListWidget::itemDoubleClicked,
                     this, [this](const QListWidgetItem *item) {
                         const auto username =
                             item->data(Qt::UserRole).toString();
                         if (!username.isEmpty())
                         {
                             this->userClicked(username);
                         }
                     });

    const auto current = getApp()->getAccounts()->itzon.current();
    if (current && current->isOAuth() && current->hasScope("api:read") &&
        current->accessTokenValid() &&
        current->username().compare(this->channelName_, Qt::CaseInsensitive) ==
            0)
    {
        this->token_ = current->token().toUtf8();
        this->endpointPath_ = QStringLiteral("/api/public/v1/me/followers");
    }
    else
    {
        this->token_ = itzon::apiToken();
        this->endpointPath_ = QStringLiteral("/api/public/v1/channel/") +
                              this->channelName_ + QStringLiteral("/followers");
    }

    this->setStyleSheet(this->theme->splits.input.styleSheet);
    if (this->token_.isEmpty())
    {
        this->showError(
            QStringLiteral("An itzon.tv API read token is required."));
        return;
    }
    this->loadNextPage();
}

void ItzonFollowerListWidget::loadNextPage()
{
    if (this->loading_ || (this->loadedPage_ && this->nextCursor_.isEmpty()))
    {
        return;
    }
    this->loading_ = true;
    this->loadMore_->setEnabled(false);
    this->summaryLabel_->setText(QStringLiteral("Loading..."));

    QUrl url(QStringLiteral("https://itzon.tv"));
    url.setPath(this->endpointPath_);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("100"));
    if (!this->nextCursor_.isEmpty())
    {
        query.addQueryItem(QStringLiteral("cursor"), this->nextCursor_);
    }
    url.setQuery(query);

    NetworkRequest(url)
        .header("Authorization", QByteArrayLiteral("Bearer ") + this->token_)
        .timeout(10000)
        .caller(this)
        .onSuccess([this](const NetworkResult &result) {
            auto page = itzon::parseFollowerPage(result.parseJson());
            if (!page)
            {
                this->showError(QStringLiteral(
                    "itzon.tv returned an invalid follower list."));
                return;
            }

            for (const auto &follower : page->followers)
            {
                const auto localTime = follower.followedAt.toLocalTime();
                auto *item = new QListWidgetItem(
                    QStringLiteral("%1 — %2").arg(
                        follower.username,
                        QLocale().toString(localTime, QLocale::ShortFormat)),
                    this->followersList_);
                item->setData(Qt::UserRole, follower.username);
                item->setToolTip(
                    follower.followedAt.toUTC().toString(Qt::ISODate));
                item->setFont(
                    getApp()->getFonts()->getFont(FontStyle::ChatMedium, 1.0));
            }

            this->loadedPage_ = true;
            this->loading_ = false;
            this->nextCursor_ = page->nextCursor;
            this->summaryLabel_->setText(
                QStringLiteral("%1 followers · %2 loaded")
                    .arg(localizeNumbers(page->total),
                         localizeNumbers(this->followersList_->count())));
            this->loadMore_->setVisible(!this->nextCursor_.isEmpty());
            this->loadMore_->setEnabled(!this->nextCursor_.isEmpty());
            this->updateFilter();
        })
        .onError([this](const NetworkResult &result) {
            if (result.status() == 404)
            {
                this->showError(QStringLiteral(
                    "This follower list is private or unavailable."));
                return;
            }
            const auto apiError = result.parseJson()["error"].toString();
            this->showError(apiError.isEmpty()
                                ? QStringLiteral("Failed to load followers: %1")
                                      .arg(result.formatError())
                                : QStringLiteral("Failed to load followers: %1")
                                      .arg(apiError));
        })
        .execute();
}

void ItzonFollowerListWidget::updateFilter()
{
    const auto query = this->searchBar_->text().trimmed();
    for (int i = 0; i < this->followersList_->count(); ++i)
    {
        auto *item = this->followersList_->item(i);
        item->setHidden(!query.isEmpty() &&
                        !item->data(Qt::UserRole)
                             .toString()
                             .contains(query, Qt::CaseInsensitive));
    }
}

void ItzonFollowerListWidget::showError(const QString &message)
{
    this->loading_ = false;
    this->summaryLabel_->setText(message);
    this->loadMore_->setEnabled(this->loadedPage_ &&
                                !this->nextCursor_.isEmpty());
}

}  // namespace chatterino
