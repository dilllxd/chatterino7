// SPDX-License-Identifier: MIT

#pragma once

#include <pajlada/signals/signalholder.hpp>
#include <QListWidget>

namespace chatterino {

class ItzonAccountSwitchWidget : public QListWidget
{
public:
    explicit ItzonAccountSwitchWidget(QWidget *parent = nullptr);
    void refresh();

private:
    void refreshItems();
    pajlada::Signals::SignalHolder managedConnections_;
};

}  // namespace chatterino
