/*
 * Copyright (C) by Klaas Freitag <freitag@kde.org>
 * Copyright (C) by Olivier Goffart <ogoffart@woboq.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include <QInputDialog>
#include <QLabel>
#include <QDesktopServices>
#include <QNetworkReply>
#include <QTimer>
#include <QBuffer>
#include "creds/httpcredentialsgui.h"
#include "theme.h"
#include "account.h"
#include "networkjobs.h"
#include <QMessageBox>
#include "common/asserts.h"

using namespace QKeychain;

namespace OCC {

void HttpCredentialsGui::askFromUser()
{
    // Unfortunately there's a bug that doesn't allow us to send the "is this
    // OAuth2 or Basic auth?" GET request directly. Scheduling it for the event
    // loop works though. See #5989.
    QMetaObject::invokeMethod(this, "askFromUserAsync", Qt::QueuedConnection);
}

void HttpCredentialsGui::askFromUserAsync()
{
    // First, we will check what kind of auth we need.
    auto job = new DetermineAuthTypeJob(_account->sharedFromThis(), this);
    job->setTimeout(30 * 1000);
    QObject::connect(job, &DetermineAuthTypeJob::authType, this, [this](DetermineAuthTypeJob::AuthType type) {
        if (type == DetermineAuthTypeJob::OAuth) {
            _asyncAuth.reset(new OAuth(_account, this));
            _asyncAuth->_expectedUser = _user;
            connect(_asyncAuth.data(), &OAuth::result,
                this, &HttpCredentialsGui::asyncAuthResult);
            connect(_asyncAuth.data(), &OAuth::destroyed,
                this, &HttpCredentialsGui::authorisationLinkChanged);
            _asyncAuth->start();
            emit authorisationLinkChanged();
        } else if (type == DetermineAuthTypeJob::Basic) {
            // Show the dialog
            // We will re-enter the event loop, so better wait the next iteration
            QMetaObject::invokeMethod(this, "showDialog", Qt::QueuedConnection);
        } else {
            // Network error? Unsupported auth type?
            emit asked();
        }
    });
    job->start();
}

void HttpCredentialsGui::asyncAuthResult(OAuth::Result r, const QString &user,
    const QString &token, const QString &refreshToken)
{
    switch (r) {
    case OAuth::NotSupported:
        // We will re-enter the event loop, so better wait the next iteration
        QMetaObject::invokeMethod(this, "showDialog", Qt::QueuedConnection);
        _asyncAuth.reset(0);
        return;
    case OAuth::Error:
        _asyncAuth.reset(0);
        emit asked();
        return;
    case OAuth::LoggedIn:
        break;
    }

    ASSERT(_user == user); // ensured by _asyncAuth

    _password = token;
    _refreshToken = refreshToken;
    _ready = true;
    persist();
    _asyncAuth.reset(0);
    emit asked();
}

void HttpCredentialsGui::showDialog()
{
    QString msg = tr("Please enter %1 password:<br>"
                     "<br>"
                     "User: %2<br>"
                     "Account: %3<br>")
                      .arg(Utility::escape(Theme::instance()->appNameGUI()),
                          Utility::escape(_user),
                          Utility::escape(_account->displayName()));

    QString reqTxt = requestAppPasswordText(_account);
    if (!reqTxt.isEmpty()) {
        msg += QLatin1String("<br>") + reqTxt + QLatin1String("<br>");
    }
    if (!_fetchErrorString.isEmpty()) {
        msg += QLatin1String("<br>")
            + tr("Reading from keychain failed with error: '%1'")
                  .arg(Utility::escape(_fetchErrorString))
            + QLatin1String("<br>");
    }

    QInputDialog dialog;
    dialog.setWindowTitle(tr("Enter Password"));
    dialog.setLabelText(msg);
    dialog.setTextValue(_previousPassword);
    dialog.setTextEchoMode(QLineEdit::Password);
    if (QLabel *dialogLabel = dialog.findChild<QLabel *>()) {
        dialogLabel->setOpenExternalLinks(true);
        dialogLabel->setTextFormat(Qt::RichText);
    }

    bool ok = dialog.exec();
    if (ok) {
        _password = dialog.textValue();
        _refreshToken.clear();
        _ready = true;
        persist();
    }
    emit asked();
}

QString HttpCredentialsGui::requestAppPasswordText(const Account *account)
{
    return QString();
}
} // namespace OCC
