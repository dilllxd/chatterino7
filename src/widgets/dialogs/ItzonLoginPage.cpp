// SPDX-License-Identifier: MIT

#include "widgets/dialogs/ItzonLoginPage.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/itzon/ItzonAccount.hpp"
#include "providers/itzon/ItzonAccountManager.hpp"
#include "providers/itzon/ItzonOAuth.hpp"
#include "util/HttpServer.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {

using namespace chatterino;

QString oauthError(const NetworkResult &result)
{
    const auto json = result.parseJson();
    const auto description = json["error_description"].toString();
    const auto error = json["error"].toString();
    if (!description.isEmpty())
    {
        return description;
    }
    if (!error.isEmpty())
    {
        return error;
    }
    return result.formatError();
}

class ItzonOAuthDialog final : public QDialog
{
public:
    explicit ItzonOAuthDialog(QWidget *parent = nullptr)
        : QDialog(parent)
        , clientID_(itzon::oauthClientID())
        , session_(itzon::createOAuthSession())
    {
        this->setAttribute(Qt::WA_DeleteOnClose);
        this->setWindowTitle(QStringLiteral("Log in with itzon.tv"));
        this->resize(430, 180);

        auto *layout = new QVBoxLayout(this);
        this->status_ = new QLabel(
            QStringLiteral("Waiting for authorization in your browser."));
        this->status_->setWordWrap(true);
        this->status_->setAlignment(Qt::AlignCenter);
        layout->addWidget(this->status_, 1);

        this->server_ = new HttpServer(0, this);
        if (!this->server_->isListening())
        {
            this->status_->setText(QStringLiteral(
                "Could not start the local OAuth callback listener."));
        }
        else
        {
            this->redirectURI_.setScheme(QStringLiteral("http"));
            this->redirectURI_.setHost(QStringLiteral("127.0.0.1"));
            this->redirectURI_.setPort(this->server_->port());
            this->redirectURI_.setPath(QStringLiteral("/oauth/itzon/callback"));
            this->authorizeURL_ = itzon::authorizationUrl(
                this->clientID_, this->redirectURI_, this->session_);
            this->server_->setHandler([this](const QString &requestTarget) {
                return this->handleCallback(requestTarget);
            });
        }

        auto *buttons = new QWidget(this);
        auto *buttonLayout = new QHBoxLayout(buttons);
        auto *openBrowser =
            new QPushButton(QStringLiteral("Open authorization page"), buttons);
        openBrowser->setEnabled(this->server_->isListening());
        QObject::connect(openBrowser, &QPushButton::clicked, this, [this] {
            QDesktopServices::openUrl(this->authorizeURL_);
        });
        buttonLayout->addWidget(openBrowser);

        auto *copyURL = new QPushButton(QStringLiteral("Copy URL"), buttons);
        copyURL->setEnabled(this->server_->isListening());
        QObject::connect(copyURL, &QPushButton::clicked, this, [this] {
            qApp->clipboard()->setText(
                this->authorizeURL_.toString(QUrl::FullyEncoded));
        });
        buttonLayout->addWidget(copyURL);
        layout->addWidget(buttons);

        auto *dialogButtons = new QDialogButtonBox(QDialogButtonBox::Cancel);
        QObject::connect(dialogButtons, &QDialogButtonBox::rejected, this,
                         &QDialog::reject);
        layout->addWidget(dialogButtons);

        if (this->server_->isListening())
        {
            QTimer::singleShot(0, this, [this] {
                QDesktopServices::openUrl(this->authorizeURL_);
            });
        }
    }

private:
    std::pair<unsigned, QByteArray> handleCallback(const QString &requestTarget)
    {
        QUrl request(QStringLiteral("http://127.0.0.1") + requestTarget);
        if (request.path() != this->redirectURI_.path())
        {
            return {404, QByteArrayLiteral("Not found")};
        }

        QUrlQuery query(request);
        if (query.queryItemValue(QStringLiteral("state")) !=
            QString::fromLatin1(this->session_.state))
        {
            this->status_->setText(
                QStringLiteral("Authorization failed: state mismatch."));
            return {400, QByteArrayLiteral("OAuth state mismatch.")};
        }

        const auto error = query.queryItemValue(QStringLiteral("error"));
        if (!error.isEmpty())
        {
            this->status_->setText(
                QStringLiteral("Authorization was not completed: %1")
                    .arg(error.toHtmlEscaped()));
            return {400, QByteArrayLiteral("Authorization was not completed.")};
        }

        const auto code = query.queryItemValue(QStringLiteral("code"));
        if (code.isEmpty() || this->exchangeStarted_)
        {
            return {400, QByteArrayLiteral("Missing or already-used code.")};
        }
        this->exchangeStarted_ = true;
        this->status_->setText(QStringLiteral("Finishing sign in..."));
        this->exchangeCode(code);
        return {
            200,
            QByteArrayLiteral(
                "<!doctype html><html><body><h2>Authorization received.</h2>"
                "<p>Return to Chatterino to finish signing in. You can close "
                "this tab.</p></body></html>"),
        };
    }

    void exchangeCode(const QString &code)
    {
        QUrlQuery payload{
            {QStringLiteral("grant_type"),
             QStringLiteral("authorization_code")},
            {QStringLiteral("client_id"), this->clientID_},
            {QStringLiteral("code"), code},
            {QStringLiteral("code_verifier"),
             QString::fromLatin1(this->session_.codeVerifier)},
            {QStringLiteral("redirect_uri"), this->redirectURI_.toString()},
        };
        NetworkRequest(QStringLiteral("https://itzon.tv/api/oauth/token"),
                       NetworkRequestType::Post)
            .header("Content-Type", "application/x-www-form-urlencoded")
            .hideRequestBody()
            .payload(payload.toString(QUrl::FullyEncoded).toUtf8())
            .timeout(20'000)
            .caller(this)
            .onError([this](const NetworkResult &result) {
                this->status_->setText(
                    QStringLiteral("Token exchange failed: %1")
                        .arg(oauthError(result).toHtmlEscaped()));
            })
            .onSuccess([this](const NetworkResult &result) {
                const auto token = result.parseJson();
                const auto accessToken = token["access_token"].toString();
                const auto refreshToken = token["refresh_token"].toString();
                const auto scope = token["scope"].toString();
                if (accessToken.isEmpty() || refreshToken.isEmpty() ||
                    !scope.split(' ', Qt::SkipEmptyParts)
                         .contains(QStringLiteral("chat")))
                {
                    this->status_->setText(QStringLiteral(
                        "itzon returned a token without the required chat "
                        "scope."));
                    return;
                }
                this->fetchIdentity(token);
            })
            .execute();
    }

    void fetchIdentity(const QJsonObject &token)
    {
        const auto accessToken = token["access_token"].toString();
        NetworkRequest(QStringLiteral("https://itzon.tv/api/oauth/userinfo"))
            .header("Authorization", QStringLiteral("Bearer ") + accessToken)
            .timeout(20'000)
            .caller(this)
            .onError([this](const NetworkResult &result) {
                this->status_->setText(
                    QStringLiteral("Could not fetch your itzon identity: %1")
                        .arg(oauthError(result).toHtmlEscaped()));
            })
            .onSuccess([this, token](const NetworkResult &result) {
                const auto user = result.parseJson();
                const auto username = user["username"].toString().trimmed();
                if (username.isEmpty())
                {
                    this->status_->setText(QStringLiteral(
                        "itzon returned an identity without a username."));
                    return;
                }

                const auto expiresIn =
                    std::max<qint64>(1, token["expires_in"].toInteger(3600));
                const auto refreshExpiresIn = std::max<qint64>(
                    1, token["refresh_expires_in"].toInteger(30 * 24 * 3600));
                ItzonAccountData data{
                    .username = username,
                    .token = token["access_token"].toString(),
                    .authType = ItzonAuthType::OAuth,
                    .refreshToken = token["refresh_token"].toString(),
                    .clientID = this->clientID_,
                    .scope = token["scope"].toString(),
                    .userID = QString::number(user["id"].toInteger()),
                    .avatar = user["avatar"].toString(),
                    .expiresAt =
                        QDateTime::currentDateTimeUtc().addSecs(expiresIn),
                    .refreshExpiresAt = QDateTime::currentDateTimeUtc().addSecs(
                        refreshExpiresIn),
                };
                if (!getApp()->getAccounts()->itzon.addOAuth(std::move(data)))
                {
                    this->status_->setText(QStringLiteral(
                        "Could not save the authorized itzon account."));
                    return;
                }
                this->accept();
            })
            .execute();
    }

    QString clientID_;
    itzon::OAuthSession session_;
    HttpServer *server_ = nullptr;
    QLabel *status_ = nullptr;
    QUrl redirectURI_;
    QUrl authorizeURL_;
    bool exchangeStarted_ = false;
};

}  // namespace

namespace chatterino {

ItzonLoginPage::ItzonLoginPage()
{
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs);

    auto *basic = new QWidget(tabs);
    auto *basicLayout = new QVBoxLayout(basic);
    auto *oauthButton = new QPushButton("Log in (Opens in browser)", basic);
    const auto oauthAvailable = !itzon::oauthClientID().isEmpty();
    oauthButton->setEnabled(oauthAvailable);
    if (!oauthAvailable)
    {
        oauthButton->setToolTip(
            "This build has no registered itzon OAuth public-client ID.");
    }
    QObject::connect(oauthButton, &QPushButton::clicked, this, [this] {
        auto *dialog = new ItzonOAuthDialog(this);
        QObject::connect(dialog, &QDialog::accepted, this, [this] {
            this->window()->close();
        });
        dialog->show();
    });
    basicLayout->addWidget(oauthButton);
    basicLayout->addStretch(1);
    tabs->addTab(basic, "Basic");

    auto *advanced = new QWidget(tabs);
    auto *advancedLayout = new QVBoxLayout(advanced);
    auto *instructions = new QLabel("1. Fill in your username\n"
                                    "2. Fill in your chat bot token\n"
                                    "3. Press Add user",
                                    advanced);
    instructions->setWordWrap(true);
    advancedLayout->addWidget(instructions);

    auto *form = new QFormLayout;
    this->username_ = new QLineEdit(advanced);
    this->token_ = new QLineEdit(advanced);
    this->token_->setEchoMode(QLineEdit::Password);
    form->addRow("Username", this->username_);
    form->addRow("Chat bot token", this->token_);
    advancedLayout->addLayout(form);

    auto *buttonLayout = new QHBoxLayout;
    this->add_ = new QPushButton("Add user", advanced);
    auto *clear = new QPushButton("Clear fields", advanced);
    this->add_->setEnabled(false);
    buttonLayout->addWidget(this->add_);
    buttonLayout->addWidget(clear);
    advancedLayout->addLayout(buttonLayout);

    auto *openDashboard =
        new QPushButton("Open itzon.tv integration dashboard", advanced);
    QObject::connect(openDashboard, &QPushButton::clicked, [] {
        QDesktopServices::openUrl(
            QUrl("https://itzon.tv/dashboard/integration"));
    });
    advancedLayout->addWidget(openDashboard);
    advancedLayout->addStretch(1);
    tabs->addTab(advanced, "Advanced");

    auto refresh = [this] {
        this->add_->setEnabled(!this->username_->text().trimmed().isEmpty() &&
                               !this->token_->text().trimmed().isEmpty());
    };
    QObject::connect(this->username_, &QLineEdit::textChanged, refresh);
    QObject::connect(this->token_, &QLineEdit::textChanged, refresh);
    QObject::connect(clear, &QPushButton::clicked, this, [this] {
        this->username_->clear();
        this->token_->clear();
    });
    QObject::connect(this->add_, &QPushButton::clicked, this, [this] {
        if (!getApp()->getAccounts()->itzon.add(this->username_->text(),
                                                this->token_->text()))
        {
            QMessageBox::warning(
                this, "Could not add itzon.tv account",
                "Both the username and chat bot token are required.");
            return;
        }
        this->token_->clear();
        this->window()->close();
    });
}

}  // namespace chatterino
