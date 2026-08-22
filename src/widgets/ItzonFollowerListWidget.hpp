// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/BaseWindow.hpp"

#include <QByteArray>
#include <QString>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace chatterino {

class ItzonChannel;

class ItzonFollowerListWidget : public BaseWindow
{
    Q_OBJECT

public:
    ItzonFollowerListWidget(const ItzonChannel *channel, QWidget *parent);

    Q_SIGNAL void userClicked(QString userLogin);

private:
    void loadNextPage();
    void updateFilter();
    void showError(const QString &message);

    QString channelName_;
    QString endpointPath_;
    QString nextCursor_;
    QByteArray token_;
    QLineEdit *searchBar_{};
    QLabel *summaryLabel_{};
    QListWidget *followersList_{};
    QPushButton *loadMore_{};
    bool loading_ = false;
    bool loadedPage_ = false;
};

}  // namespace chatterino
