// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/BaseWindow.hpp"

#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace chatterino {

class ItzonChannel;
class TwitchChannel;

class ChatterListWidget : public BaseWindow
{
    Q_OBJECT

public:
    ChatterListWidget(const TwitchChannel *twitchChannel, QWidget *parent);
    ChatterListWidget(const ItzonChannel *itzonChannel, QWidget *parent);

    Q_SIGNAL void userClicked(QString userLogin);

private:
    void initialize(const QString &channelName);
    QListWidgetItem *formatListItem(const QString &text) const;
    void addLabel(const QString &label);
    void addUserList(QStringList users, const QString &label);
    void performListSearch();

    QLineEdit *searchBar_{};
    QListWidget *chattersList_{};
    QListWidget *resultList_{};
    QLabel *loadingLabel_{};
};

}  // namespace chatterino
