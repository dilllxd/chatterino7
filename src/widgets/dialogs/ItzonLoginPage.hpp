// SPDX-License-Identifier: MIT

#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;

namespace chatterino {

class ItzonLoginPage : public QWidget
{
public:
    ItzonLoginPage();

private:
    QLineEdit *username_ = nullptr;
    QLineEdit *token_ = nullptr;
    QPushButton *add_ = nullptr;
};

}  // namespace chatterino
