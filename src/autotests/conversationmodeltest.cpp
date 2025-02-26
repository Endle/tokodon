// SPDX-FileCopyrightText: 2022 Carl Schwan <carl@carlschwan.eu>
// SPDX-License-Identifier: LGPL-2.0-or-later

#include <QtTest/QtTest>

#include "abstracttimelinemodel.h"
#include "conversationmodel.h"
#include "helperreply.h"
#include "mockaccount.h"
#include "post.h"
#include <QAbstractItemModelTester>
#include <QSignalSpy>

class ConversationModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
    }

    void testModel()
    {
        auto account = new MockAccount();
        AccountManager::instance().addAccount(account);
        AccountManager::instance().selectAccount(account);
        QUrl url = account->apiUrl("/api/v1/conversations");
        account->registerGet(url, new TestReply("conversation-result.json", account));

        ConversationModel conversationModel;

        QCOMPARE(conversationModel.rowCount({}), 1);
        QCOMPARE(conversationModel.data(conversationModel.index(0, 0), AbstractTimelineModel::AvatarRole),
                 QUrl("https://files.mastodon.social/accounts/avatars/000/000/001/original/d96d39a0abb45b92.jpg"));
        QCOMPARE(conversationModel.data(conversationModel.index(0, 0), AbstractTimelineModel::AuthorIdRole).toString(), "Gargron");
        QCOMPARE(conversationModel.data(conversationModel.index(0, 0), ConversationModel::ConversationIdRole).toString(), "418374");
        QCOMPARE(conversationModel.data(conversationModel.index(0, 0), ConversationModel::UnreadRole), false);
        QCOMPARE(conversationModel.data(conversationModel.index(0, 0), Qt::DisplayRole), QStringLiteral("LOREM"));
        QCOMPARE(conversationModel.data(conversationModel.index(0, 0), AbstractTimelineModel::AuthorDisplayNameRole).toString(), QStringLiteral("Eugen"));
    }
};

QTEST_MAIN(ConversationModelTest)
#include "conversationmodeltest.moc"
