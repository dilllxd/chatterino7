// SPDX-License-Identifier: MIT

#pragma once

#include "messages/MessageElement.hpp"

#include <QStringView>

#include <utility>

namespace chatterino {

class ItzonBadges
{
public:
    static std::pair<EmotePtr, MessageElementFlag> role(QStringView name);
    static std::pair<EmotePtr, MessageElementFlag> subscriber(QStringView name);
    static QString sanitizeSubscriberName(QStringView name);
};

}  // namespace chatterino
