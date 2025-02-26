// SPDX-FileCopyrightText: 2021 Carl Schwan <carlschwan@kde.org>
// SPDX-License-Identifier: GPL-3.0-only

#include "timelinemodel.h"

class AbstractAccount;

class AccountModel : public TimelineModel
{
    Q_OBJECT
    Q_PROPERTY(Identity *identity READ identity NOTIFY identityChanged)
    Q_PROPERTY(AbstractAccount *account READ account NOTIFY accountChanged)
    Q_PROPERTY(bool isSelf READ isSelf NOTIFY identityChanged)

public:
    AccountModel(qint64 id, const QString &acct, QObject *parent = nullptr);

    QString displayName() const override;
    AbstractAccount *account() const;
    Identity *identity() const;
    bool isSelf() const;

    void fillTimeline(const QString &fromId = QString()) override;

Q_SIGNALS:
    void identityChanged();
    void accountChanged();

private:
    void updateRelationships();

    std::shared_ptr<Identity> m_identity;
    qint64 m_id;
};
