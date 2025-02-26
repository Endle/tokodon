// SPDX-FileCopyrightText: 2021 kaniini <https://git.pleroma.social/kaniini>
// SPDX-FileCopyrightText: 2021 Carl Schwan <carl@carlschwan.eu>
// SPDX-License-Identifier: GPL-3.0-only

#include "abstractaccount.h"

#include "accountmanager.h"
#include "messagefiltercontainer.h"
#include "preferences.h"
#include "relationship.h"
#include "tokodon_debug.h"
#include <KLocalizedString>
#include <QFile>
#include <QHttpMultiPart>
#include <QNetworkReply>
#include <QUrlQuery>

AbstractAccount::AbstractAccount(QObject *parent, const QString &name, const QString &instanceUri)
    : QObject(parent)
    , m_name(name)
    , m_instance_uri(instanceUri)
    // default to 500, instances which support more signal it
    , m_maxPostLength(500)
{
    m_identity.reparentIdentity(this);
}

AbstractAccount::AbstractAccount(QObject *parent)
    : QObject(parent)
    // default to 500, instances which support more signal it
    , m_maxPostLength(500)
{
}

Preferences *AbstractAccount::preferences() const
{
    return m_preferences;
}

QString AbstractAccount::username() const
{
    return m_name;
}

void AbstractAccount::setUsername(const QString &username)
{
    if (m_name == username) {
        return;
    }
    m_name = username;
    Q_EMIT usernameChanged();
}

size_t AbstractAccount::maxPostLength() const
{
    return m_maxPostLength;
}

QString AbstractAccount::instanceName() const
{
    return QString(m_instance_name);
}

bool AbstractAccount::haveToken() const
{
    return !m_token.isEmpty();
}

QUrl AbstractAccount::apiUrl(const QString &path) const
{
    const auto instanceUrl = QUrl::fromUserInput(m_instance_uri);

    auto url = QUrl::fromUserInput(m_instance_uri);
    url.setScheme("https");
    url.setPath(instanceUrl.path() + path);

    return url;
}

void AbstractAccount::registerApplication(const QString &appName, const QString &website)
{
    // clear any previous bearer token credentials
    m_token = QString();

    // register
    const QUrl regUrl = apiUrl("/api/v1/apps");
    const QJsonObject obj = {
        {"client_name", appName},
        {"redirect_uris", "urn:ietf:wg:oauth:2.0:oob"},
        {"scopes", "read write follow"},
        {"website", website},
    };
    const QJsonDocument doc(obj);

    post(regUrl, doc, false, this, [=](QNetworkReply *reply) {
        if (!reply->isFinished()) {
            qCDebug(TOKODON_LOG) << "not finished";
            return;
        }

        const auto data = reply->readAll();
        const auto doc = QJsonDocument::fromJson(data);

        m_client_id = doc.object()["client_id"].toString();
        m_client_secret = doc.object()["client_secret"].toString();

        s_messageFilter->insert(m_client_secret, "CLIENT_SECRET");

        if (isRegistered()) {
            Q_EMIT registered();
        }
    });
}

const Identity &AbstractAccount::identity()
{
    return m_identity;
}

Identity *AbstractAccount::identityObj()
{
    return &m_identity;
}

const std::shared_ptr<Identity> AbstractAccount::identityLookup(const QString &acct, const QJsonObject &doc)
{
    auto id = m_identityCache[acct];
    if (id && id->account() == acct) {
        return m_identityCache[acct];
    }

    id = std::make_shared<Identity>();
    id->reparentIdentity(this);
    id->fromSourceData(doc);
    m_identityCache[acct] = id;

    return m_identityCache[acct];
}

bool AbstractAccount::identityCached(const QString &acct) const
{
    auto id = m_identityCache[acct];
    return id && id->account() == acct;
}

QUrlQuery AbstractAccount::buildOAuthQuery() const
{
    return QUrlQuery{{"client_id", m_client_id}};
}

QUrl AbstractAccount::getAuthorizeUrl() const
{
    QUrl url = apiUrl("/oauth/authorize");
    QUrlQuery q = buildOAuthQuery();

    q.addQueryItem("redirect_uri", "urn:ietf:wg:oauth:2.0:oob");
    q.addQueryItem("response_type", "code");
    q.addQueryItem("scope", "read write follow");

    url.setQuery(q);

    return url;
}

QUrl AbstractAccount::getTokenUrl() const
{
    return apiUrl("/oauth/token");
}

void AbstractAccount::setInstanceUri(const QString &instance_uri)
{
    // instance URI changed, get new credentials
    QUrl instance_url = QUrl::fromUserInput(instance_uri);
    instance_url.setScheme("https"); // getting token from http is not supported

    m_instance_uri = instance_url.toString();
    registerApplication("Tokodon", "https://app.kde.org/neochat");
}

QString AbstractAccount::instanceUri() const
{
    return QString(m_instance_uri);
}

void AbstractAccount::setToken(const QString &authcode)
{
    const QUrl tokenUrl = getTokenUrl();
    QUrlQuery q = buildOAuthQuery();

    q.addQueryItem("client_secret", m_client_secret);
    q.addQueryItem("redirect_uri", "urn:ietf:wg:oauth:2.0:oob");
    q.addQueryItem("grant_type", "authorization_code");
    q.addQueryItem("code", authcode);

    post(tokenUrl, q, false, this, [=](QNetworkReply *reply) {
        auto data = reply->readAll();
        auto doc = QJsonDocument::fromJson(data);

        m_token = doc.object()["access_token"].toString();
        s_messageFilter->insert(m_token, "ACCESS_TOKEN");
        AccountManager::instance().addAccount(this);
        validateToken();
    });
}

void AbstractAccount::postStatus(Post *p)
{
    QUrl post_status_url = apiUrl("/api/v1/statuses");
    auto doc = p->toJsonDocument();

    post(post_status_url, doc, true, this, [=](QNetworkReply *reply) {
        auto data = reply->readAll();
        auto doc = QJsonDocument::fromJson(data);
        auto obj = doc.object();
        qDebug() << "Message sent:" << obj;
    });
}

void AbstractAccount::mutatePost(Post *p, const QString &verb, bool deliver_home)
{
    const QUrl mutation_url = apiUrl(QString("/api/v1/statuses/%1/%2").arg(p->inReplyTo(), verb));
    const QJsonDocument doc;

    post(mutation_url, doc, true, this, [=](QNetworkReply *reply) {
        const auto data = reply->readAll();
        const auto doc = QJsonDocument::fromJson(data);

        if (deliver_home) {
            QList<Post *> posts;
            auto obj = doc.object();

            auto p = new Post(this, obj, this);
            posts.push_back(p);

            Q_EMIT fetchedTimeline("home", posts);
        }
    });
}

void AbstractAccount::favorite(Post *p)
{
    mutatePost(p, "favourite");
}

void AbstractAccount::unfavorite(Post *p)
{
    mutatePost(p, "unfavourite");
}

void AbstractAccount::repeat(Post *p)
{
    mutatePost(p, "reblog", true);
}

void AbstractAccount::unrepeat(Post *p)
{
    mutatePost(p, "unreblog");
}

// It seemed clearer to keep this logic separate from the general instance metadata collection, on the off chance
// that it might need to be extended later on.
static AbstractAccount::AllowedContentType parseVersion(const QString &instanceVer)
{
    using ContentType = AbstractAccount::AllowedContentType;

    unsigned int result = ContentType::PlainText;
    if (instanceVer.contains("glitch")) {
        result |= ContentType::Markdown | ContentType::Html;
    }

    return static_cast<ContentType>(result);
}

static QMap<QString, AbstractAccount::AllowedContentType> stringToContentType = {
    {"text/plain", AbstractAccount::AllowedContentType::PlainText},
    {"text/bbcode", AbstractAccount::AllowedContentType::BBCode},
    {"text/html", AbstractAccount::AllowedContentType::Html},
    {"text/markdown", AbstractAccount::AllowedContentType::Markdown},
};

static AbstractAccount::AllowedContentType parsePleromaInfo(const QJsonDocument &doc)
{
    using ContentType = AbstractAccount::AllowedContentType;
    unsigned int result = ContentType::PlainText;

    auto obj = doc.object();
    if (obj.contains("metadata")) {
        auto metadata = obj["metadata"].toObject();
        if (!metadata.contains("postFormats"))
            return static_cast<ContentType>(result);

        auto formats = metadata["postFormats"].toArray();

        for (auto c : formats) {
            auto fmt = c.toString();

            if (!stringToContentType.contains(fmt))
                continue;

            result |= (unsigned int)stringToContentType[fmt];
        }
    }

    return static_cast<ContentType>(result);
}

void AbstractAccount::fetchInstanceMetadata()
{
    QUrl instance_url = apiUrl("/api/v1/instance");
    QUrl pleroma_info = apiUrl("/nodeinfo/2.1.json");

    get(instance_url, false, this, [=](QNetworkReply *reply) {
        if (200 != reply->attribute(QNetworkRequest::HttpStatusCodeAttribute))
            return;

        const auto data = reply->readAll();
        const auto doc = QJsonDocument::fromJson(data);

        if (!doc.isObject())
            return;

        const auto obj = doc.object();

        if (obj.contains("max_toot_chars")) {
            m_maxPostLength = (unsigned)obj["max_toot_chars"].toInt();
        }

        // One can only hope that there will always be a version attached
        if (obj.contains("version")) {
            m_allowedContentTypes = parseVersion(obj["version"].toString());
        }

        m_instance_name = obj["title"].toString();
        Q_EMIT fetchedInstanceMetadata();
    });
    m_instance_name = QString("social");
    Q_EMIT fetchedInstanceMetadata();

    get(pleroma_info, false, this, [=](QNetworkReply *reply) {
        const auto data = reply->readAll();
        const auto doc = QJsonDocument::fromJson(data);

        m_allowedContentTypes = parsePleromaInfo(doc);
        Q_EMIT fetchedInstanceMetadata();
    });
}

void AbstractAccount::invalidate()
{
    Q_EMIT invalidated();
}

void AbstractAccount::invalidatePost(Post *p)
{
    Q_EMIT invalidatedPost(p);
}

QUrl AbstractAccount::streamingUrl(const QString &stream)
{
    QUrl url = apiUrl("/api/v1/streaming");
    url.setQuery(QUrlQuery{{"access_token", m_token}, {"stream", stream}});
    url.setScheme("wss");

    return QUrl(url);
}

void AbstractAccount::handleNotification(const QJsonDocument &doc)
{
    const auto obj = doc.object();
    std::shared_ptr<Notification> n = std::make_shared<Notification>(this, obj);

    Q_EMIT notification(n);
}

Post *AbstractAccount::newPost()
{
    return new Post(this);
}

void AbstractAccount::executeAction(Identity *identity, AccountAction accountAction, const QJsonObject &extraArguments)
{
    const QHash<AccountAction, QString> accountActionMap = {
        {AccountAction::Follow, QStringLiteral("/follow")},
        {AccountAction::Unfollow, QStringLiteral("/unfollow")},
        {AccountAction::Block, QStringLiteral("/block")},
        {AccountAction::Unblock, QStringLiteral("/unblock")},
        {AccountAction::Mute, QStringLiteral("/mute")},
        {AccountAction::Unmute, QStringLiteral("/unmute")},
        {AccountAction::Feature, QStringLiteral("/pin")},
        {AccountAction::Unfeature, QStringLiteral("/unpin")},
        {AccountAction::Note, QStringLiteral("/note")},
    };

    const auto apiCall = accountActionMap[accountAction];

    const auto accountId = QString::number(identity->id());
    const QString api_url = QStringLiteral("/api/v1/accounts/") + accountId + apiCall;
    QUrl url = apiUrl(api_url);
    const QJsonDocument doc(extraArguments);

    post(url, doc, true, this, [=](QNetworkReply *reply) {
        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto jsonObj = doc.object();

        // Check if the request failed due to one account blocking the other
        if (!jsonObj.value("error").isUndefined()) {
            const QHash<AccountAction, QString> accountActionMap = {
                {AccountAction::Follow, i18n("Could not follow account")},
                {AccountAction::Unfollow, i18n("Could not unfollow account")},
                {AccountAction::Block, i18n("Could not block account")},
                {AccountAction::Unblock, i18n("Could not unblock account")},
                {AccountAction::Mute, i18n("Could not mute account")},
                {AccountAction::Unmute, i18n("Could not unmute account")},
                {AccountAction::Feature, i18n("Could not feature account")},
                {AccountAction::Unfeature, i18n("Could not unfeature account")},
                {AccountAction::Note, i18n("Could not edit note about an account")},
            };
            const auto errorMessage = accountActionMap[accountAction];
            Q_EMIT errorOccured(errorMessage);
            return;
        }
        // If returned json obj is not an error, it's a relationship status.
        // Returned relationship should have a value of true
        // under either the "following" or "requested" keys.
        auto relationship = identity->relationship();
        relationship->updateFromJson(jsonObj);

        Q_EMIT identity->relationshipChanged();
    });
}

void AbstractAccount::followAccount(Identity *identity, bool reblogs, bool notify)
{
    executeAction(identity,
                  AccountAction::Follow,
                  {
                      {"reblogs", reblogs},
                      {"notify", notify},
                  });
}

void AbstractAccount::unfollowAccount(Identity *identity)
{
    executeAction(identity, AccountAction::Unfollow);
}

void AbstractAccount::blockAccount(Identity *identity)
{
    executeAction(identity, AccountAction::Block);
}

void AbstractAccount::unblockAccount(Identity *identity)
{
    executeAction(identity, AccountAction::Unblock);
}

void AbstractAccount::muteAccount(Identity *identity, bool notifications, int duration)
{
    executeAction(identity, AccountAction::Mute, {{"notifcations", notifications}, {"duration", duration}});
}

void AbstractAccount::unmuteAccount(Identity *identity)
{
    executeAction(identity, AccountAction::Unmute);
}

void AbstractAccount::featureAccount(Identity *identity)
{
    executeAction(identity, AccountAction::Feature);
}

void AbstractAccount::unfeatureAccount(Identity *identity)
{
    executeAction(identity, AccountAction::Unfeature);
}

void AbstractAccount::addNote(Identity *identity, const QString &note)
{
    if (note.isEmpty()) {
        executeAction(identity, AccountAction::Note);
    } else {
        executeAction(identity, AccountAction::Note, {{"comment", note}});
    }
}

void AbstractAccount::setDirtyIdentity()
{
    Q_EMIT identityChanged(this);
}

bool AbstractAccount::isRegistered() const
{
    return !m_client_id.isEmpty() && !m_client_secret.isEmpty();
}

// best effort for now, maybe we fire a signal later
void AbstractAccount::updateAttachment(Attachment *a)
{
    const auto attachementUrl = apiUrl(QString("/api/v1/media/%1").arg(a->m_id));
    const QJsonObject obj{
        {"description", a->m_description},
    };
    const QJsonDocument doc(obj);
    put(attachementUrl, doc, true, this, nullptr);
}
