// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/ProviderId.hpp"
#include "widgets/BaseWindow.hpp"

#include <QWidget>

namespace chatterino {

class AccountSwitchWidget;
class KickAccountSwitchWidget;
class ItzonAccountSwitchWidget;
class MicroNotebook;

class AccountSwitchPopup : public BaseWindow
{
    Q_OBJECT

public:
    AccountSwitchPopup(QWidget *parent = nullptr);

    void refresh(ProviderId provider);

protected:
    void paintEvent(QPaintEvent *event) override;

    void themeChangedEvent() override;

private:
    struct {
        AccountSwitchWidget *accountSwitchWidget = nullptr;
        KickAccountSwitchWidget *kickAccountSwitcher = nullptr;
        ItzonAccountSwitchWidget *itzonAccountSwitcher = nullptr;
        MicroNotebook *notebook = nullptr;
    } ui_;
};

}  // namespace chatterino
