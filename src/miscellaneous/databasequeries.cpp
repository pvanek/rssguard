// This file is part of RSS Guard.
//
// Copyright (C) 2011-2016 by Martin Rotter <rotter.martinos@gmail.com>
//
// RSS Guard is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// RSS Guard is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with RSS Guard. If not, see <http://www.gnu.org/licenses/>.

#include "miscellaneous/databasequeries.h"

#include "services/abstract/category.h"
#include "services/owncloud/owncloudserviceroot.h"
#include "services/owncloud/owncloudcategory.h"
#include "services/owncloud/owncloudfeed.h"
#include "services/owncloud/network/owncloudnetworkfactory.h"
#include "services/standard/standardserviceroot.h"
#include "services/standard/standardcategory.h"
#include "services/standard/standardfeed.h"
#include "services/tt-rss/ttrssserviceroot.h"
#include "services/tt-rss/ttrsscategory.h"
#include "services/tt-rss/ttrssfeed.h"
#include "services/tt-rss/network/ttrssnetworkfactory.h"
#include "miscellaneous/textfactory.h"
#include "miscellaneous/application.h"
#include "miscellaneous/iconfactory.h"

#include <QVariant>
#include <QUrl>
#include <QSqlError>


bool DatabaseQueries::markMessagesReadUnread(QSqlDatabase db, const QStringList &ids, RootItem::ReadStatus read) {
  QSqlQuery q(db);
  q.setForwardOnly(true);

  return q.exec(QString(QSL("UPDATE Messages SET is_read = %2 WHERE id IN (%1);"))
                .arg(ids.join(QSL(", ")), read == RootItem::Read ? QSL("1") : QSL("0")));
}

bool DatabaseQueries::markMessageImportant(QSqlDatabase db, int id, RootItem::Importance importance) {
  QSqlQuery q(db);
  q.setForwardOnly(true);

  if (!q.prepare(QSL("UPDATE Messages SET is_important = :important WHERE id = :id;"))) {
    qWarning("Query preparation failed for message importance switch.");
    return false;
  }

  q.bindValue(QSL(":id"), id);
  q.bindValue(QSL(":important"), (int) importance);

  // Commit changes.
  return q.exec();
}

bool DatabaseQueries::markFeedsReadUnread(QSqlDatabase db, const QStringList &ids, int account_id, RootItem::ReadStatus read) {
  QSqlQuery q(db);
  q.setForwardOnly(true);
  q.prepare(QString("UPDATE Messages SET is_read = :read "
                    "WHERE feed IN (%1) AND is_deleted = 0 AND is_pdeleted = 0 AND account_id = :account_id;").arg(ids.join(QSL(", "))));

  q.bindValue(QSL(":read"), read == RootItem::Read ? 1 : 0);
  q.bindValue(QSL(":account_id"), account_id);

  return q.exec();
}

bool DatabaseQueries::markBinReadUnread(QSqlDatabase db, int account_id, RootItem::ReadStatus read) {
  QSqlQuery q(db);
  q.setForwardOnly(true);
  q.prepare("UPDATE Messages SET is_read = :read "
            "WHERE is_deleted = 1 AND is_pdeleted = 0 AND account_id = :account_id;");

  q.bindValue(QSL(":read"), read == RootItem::Read ? 1 : 0);
  q.bindValue(QSL(":account_id"), account_id);

  return q.exec();
}

bool DatabaseQueries::markAccountReadUnread(QSqlDatabase db, int account_id, RootItem::ReadStatus read) {
  QSqlQuery q(db);
  q.setForwardOnly(true);
  q.prepare(QSL("UPDATE Messages SET is_read = :read WHERE is_pdeleted = 0 AND account_id = :account_id;"));

  q.bindValue(QSL(":account_id"), account_id);
  q.bindValue(QSL(":read"), read == RootItem::Read ? 1 : 0);

  return q.exec();
}

bool DatabaseQueries::switchMessagesImportance(QSqlDatabase db, const QStringList &ids) {
  QSqlQuery q(db);
  q.setForwardOnly(true);

  return q.exec(QString(QSL("UPDATE Messages SET is_important = NOT is_important WHERE id IN (%1);")).arg(ids.join(QSL(", "))));
}

bool DatabaseQueries::permanentlyDeleteMessages(QSqlDatabase db, const QStringList &ids) {
  QSqlQuery q(db);
  q.setForwardOnly(true);

  return q.exec(QString(QSL("UPDATE Messages SET is_pdeleted = 1 WHERE id IN (%1);")).arg(ids.join(QSL(", "))));
}

bool DatabaseQueries::deleteOrRestoreMessagesToFromBin(QSqlDatabase db, const QStringList &ids, bool deleted) {
  QSqlQuery q(db);
  q.setForwardOnly(true);

  return q.exec(QString(QSL("UPDATE Messages SET is_deleted = %2 WHERE id IN (%1);")).arg(ids.join(QSL(", ")),
                                                                                          QString::number(deleted ? 1 : 0)));
}

bool DatabaseQueries::restoreBin(QSqlDatabase db, int account_id) {
  QSqlQuery q(db);
  q.setForwardOnly(true);
  q.prepare("UPDATE Messages SET is_deleted = 0 "
            "WHERE is_deleted = 1 AND is_pdeleted = 0 AND account_id = :account_id;");
  q.bindValue(QSL(":account_id"), account_id);

  return q.exec();
}

bool DatabaseQueries::purgeImportantMessages(QSqlDatabase db) {
  QSqlQuery q(db);
  q.setForwardOnly(true);
  q.prepare(QSL("DELETE FROM Messages WHERE is_important = 1;"));

  return q.exec();
}

bool DatabaseQueries::purgeReadMessages(QSqlDatabase db) {
  QSqlQuery q(db);
  q.setForwardOnly(true);
  q.prepare(QSL("DELETE FROM Messages WHERE is_important = :is_important AND is_deleted = :is_deleted AND is_read = :is_read;"));
  q.bindValue(QSL(":is_read"), 1);

  // Remove only messages which are NOT in recycle bin.
  q.bindValue(QSL(":is_deleted"), 0);

  // Remove only messages which are NOT starred.
  q.bindValue(QSL(":is_important"), 0);

  return q.exec();
}

bool DatabaseQueries::purgeOldMessages(QSqlDatabase db, int older_than_days) {
  QSqlQuery q(db);
  const qint64 since_epoch = QDateTime::currentDateTimeUtc().addDays(-older_than_days).toMSecsSinceEpoch();

  q.setForwardOnly(true);
  q.prepare(QSL("DELETE FROM Messages WHERE is_important = :is_important AND date_created < :date_created;"));
  q.bindValue(QSL(":date_created"), since_epoch);

  // Remove only messages which are NOT starred.
  q.bindValue(QSL(":is_important"), 0);

  return q.exec();
}

bool DatabaseQueries::purgeRecycleBin(QSqlDatabase db) {
  QSqlQuery q(db);

  q.setForwardOnly(true);
  q.prepare(QSL("DELETE FROM Messages WHERE is_important = :is_important AND is_deleted = :is_deleted;"));
  q.bindValue(QSL(":is_deleted"), 1);

  // Remove only messages which are NOT starred.
  q.bindValue(QSL(":is_important"), 0);

  return q.exec();
}

QMap<int,QPair<int,int> > DatabaseQueries::getMessageCountsForCategory(QSqlDatabase db, int custom_id, int account_id,
                                                                       bool including_total_counts, bool *ok) {
  QMap<int, QPair<int,int> > counts;
  QSqlQuery q(db);
  q.setForwardOnly(true);

  if (including_total_counts) {
    q.prepare("SELECT feed, sum((is_read + 1) % 2), count(*) FROM Messages "
              "WHERE feed IN (SELECT custom_id FROM Feeds WHERE category = :category AND account_id = :account_id) AND is_deleted = 0 AND is_pdeleted = 0 AND account_id = :account_id "
              "GROUP BY feed;");
  }
  else {
    q.prepare("SELECT feed, sum((is_read + 1) % 2) FROM Messages "
              "WHERE feed IN (SELECT custom_id FROM Feeds WHERE category = :category AND account_id = :account_id) AND is_deleted = 0 AND is_pdeleted = 0 AND account_id = :account_id "
              "GROUP BY feed;");
  }

  q.bindValue(QSL(":category"), custom_id);
  q.bindValue(QSL(":account_id"), account_id);

  if (q.exec()) {
    while (q.next()) {
      int feed_id = q.value(0).toInt();
      int unread_count = q.value(1).toInt();

      if (including_total_counts) {
        int total_count = q.value(2).toInt();

        counts.insert(feed_id, QPair<int,int>(unread_count, total_count));
      }
      else {
        counts.insert(feed_id, QPair<int,int>(unread_count, 0));
      }
    }

    if (ok != NULL) {
      *ok = true;
    }
  }
  else {
    if (ok != NULL) {
      *ok = false;
    }
  }

  return counts;
}

QMap<int,QPair<int,int> > DatabaseQueries::getMessageCountsForAccount(QSqlDatabase db, int account_id,
                                                                      bool including_total_counts, bool *ok) {
  QMap<int,QPair<int,int> > counts;
  QSqlQuery q(db);
  q.setForwardOnly(true);

  if (including_total_counts) {
    q.prepare("SELECT feed, sum((is_read + 1) % 2), count(*) FROM Messages "
              "WHERE is_deleted = 0 AND is_pdeleted = 0 AND account_id = :account_id "
              "GROUP BY feed;");
  }
  else {
    q.prepare("SELECT feed, sum((is_read + 1) % 2) FROM Messages "
              "WHERE is_deleted = 0 AND is_pdeleted = 0 AND account_id = :account_id "
              "GROUP BY feed;");
  }

  q.bindValue(QSL(":account_id"), account_id);

  if (q.exec()) {
    while (q.next()) {
      int feed_id = q.value(0).toInt();
      int unread_count = q.value(1).toInt();

      if (including_total_counts) {
        int total_count = q.value(2).toInt();

        counts.insert(feed_id, QPair<int,int>(unread_count, total_count));
      }
      else {
        counts.insert(feed_id, QPair<int,int>(unread_count, 0));
      }
    }

    if (ok != NULL) {
      *ok = true;
    }
  }
  else {
    if (ok != NULL) {
      *ok = false;
    }
  }

  return counts;
}

int DatabaseQueries::getMessageCountsForFeed(QSqlDatabase db, int feed_custom_id,
                                             int account_id, bool including_total_counts, bool *ok) {
  QSqlQuery q(db);
  q.setForwardOnly(true);

  if (including_total_counts) {
    q.prepare("SELECT count(*) FROM Messages "
              "WHERE feed = :feed AND is_deleted = 0 AND is_pdeleted = 0 AND account_id = :account_id;");
  }
  else {
    q.prepare("SELECT count(*) FROM Messages "
              "WHERE feed = :feed AND is_deleted = 0 AND is_pdeleted = 0 AND is_read = 0 AND account_id = :account_id;");
  }

  q.bindValue(QSL(":feed"), feed_custom_id);
  q.bindValue(QSL(":account_id"), account_id);

  if (q.exec() && q.next()) {
    if (ok != NULL) {
      *ok = true;
    }

    return q.value(0).toInt();
  }
  else {
    if (ok != NULL) {
      *ok = false;
    }

    return 0;
  }
}

int DatabaseQueries::getMessageCountsForBin(QSqlDatabase db, int account_id, bool including_total_counts, bool *ok) {
  QSqlQuery q(db);
  q.setForwardOnly(true);

  if (including_total_counts) {
    q.prepare("SELECT count(*) FROM Messages "
              "WHERE is_deleted = 1 AND is_pdeleted = 0 AND account_id = :account_id;");
  }
  else {
    q.prepare("SELECT count(*) FROM Messages "
              "WHERE is_read = 0 AND is_deleted = 1 AND is_pdeleted = 0 AND account_id = :account_id;");
  }

  q.bindValue(QSL(":account_id"), account_id);

  if (q.exec() && q.next()) {
    if (ok != NULL) {
      *ok = true;
    }

    return q.value(0).toInt();
  }
  else {
    if (ok != NULL) {
      *ok = false;
    }

    return 0;
  }
}

QList<Message> DatabaseQueries::getUndeletedMessagesForFeed(QSqlDatabase db, int feed_custom_id, int account_id, bool *ok) {
  QList<Message> messages;
  QSqlQuery q(db);
  q.setForwardOnly(true);
  q.prepare("SELECT * "
            "FROM Messages "
            "WHERE is_deleted = 0 AND is_pdeleted = 0 AND feed = :feed AND account_id = :account_id;");

  q.bindValue(QSL(":feed"), feed_custom_id);
  q.bindValue(QSL(":account_id"), account_id);

  if (q.exec()) {
    while (q.next()) {
      bool decoded;
      Message message = Message::fromSqlRecord(q.record(), &decoded);

      if (decoded) {
        messages.append(message);
      }
    }

    if (ok != NULL) {
      *ok = true;
    }
  }
  else {
    if (ok != NULL) {
      *ok = false;
    }
  }

  return messages;
}

QList<Message> DatabaseQueries::getUndeletedMessagesForBin(QSqlDatabase db, int account_id, bool *ok) {
  QList<Message> messages;
  QSqlQuery q(db);
  q.setForwardOnly(true);
  q.prepare("SELECT * "
            "FROM Messages "
            "WHERE is_deleted = 1 AND is_pdeleted = 0 AND account_id = :account_id;");

  q.bindValue(QSL(":account_id"), account_id);

  if (q.exec()) {
    while (q.next()) {
      bool decoded;
      Message message = Message::fromSqlRecord(q.record(), &decoded);

      if (decoded) {
        messages.append(message);
      }
    }

    if (ok != NULL) {
      *ok = true;
    }
  }
  else {
    if (ok != NULL) {
      *ok = false;
    }
  }

  return messages;
}

QList<Message> DatabaseQueries::getUndeletedMessagesForAccount(QSqlDatabase db, int account_id, bool *ok) {
  QList<Message> messages;
  QSqlQuery q(db);
  q.setForwardOnly(true);
  q.prepare("SELECT * "
            "FROM Messages "
            "WHERE is_deleted = 0 AND is_pdeleted = 0 AND account_id = :account_id;");
  q.bindValue(QSL(":account_id"), account_id);

  if (q.exec()) {
    while (q.next()) {
      bool decoded;
      Message message = Message::fromSqlRecord(q.record(), &decoded);

      if (decoded) {
        messages.append(message);
      }
    }

    if (ok != NULL) {
      *ok = true;
    }
  }
  else {
    if (ok != NULL) {
      *ok = false;
    }
  }

  return messages;
}

int DatabaseQueries::updateMessages(QSqlDatabase db,
                                    const QList<Message> &messages,
                                    int feed_custom_id,
                                    int account_id,
                                    const QString &url,
                                    bool *any_message_changed,
                                    bool *ok) {
  if (messages.isEmpty()) {
    *any_message_changed = false;
    *ok = true;
    return 0;
  }

  // Does not make any difference, since each feed now has
  // its own "custom ID" (standard feeds have their custom ID equal to primary key ID).
  int updated_messages = 0;

  // Prepare queries.
  QSqlQuery query_select_with_url(db);
  QSqlQuery query_select_with_id(db);
  QSqlQuery query_update(db);
  QSqlQuery query_insert(db);

  // Here we have query which will check for existence of the "same" message in given feed.
  // The two message are the "same" if:
  //   1) they belong to the same feed AND,
  //   2) they have same URL AND,
  //   3) they have same AUTHOR.
  query_select_with_url.setForwardOnly(true);
  query_select_with_url.prepare("SELECT id, date_created, is_read, is_important FROM Messages "
                                "WHERE feed = :feed AND title = :title AND url = :url AND author = :author AND account_id = :account_id;");

  // When we have custom ID of the message, we can check directly for existence
  // of that particular message.
  query_select_with_id.setForwardOnly(true);
  query_select_with_id.prepare("SELECT id, date_created, is_read, is_important FROM Messages "
                               "WHERE custom_id = :custom_id AND account_id = :account_id;");

  // Used to insert new messages.
  query_insert.setForwardOnly(true);
  query_insert.prepare("INSERT INTO Messages "
                       "(feed, title, is_read, is_important, url, author, date_created, contents, enclosures, custom_id, custom_hash, account_id) "
                       "VALUES (:feed, :title, :is_read, :is_important, :url, :author, :date_created, :contents, :enclosures, :custom_id, :custom_hash, :account_id);");

  // Used to update existing messages.
  query_update.setForwardOnly(true);
  query_update.prepare("UPDATE Messages "
                       "SET title = :title, is_read = :is_read, is_important = :is_important, url = :url, author = :author, date_created = :date_created, contents = :contents, enclosures = :enclosures "
                       "WHERE id = :id;");

  if (!db.transaction()) {
    db.rollback();
    qDebug("Transaction start for message downloader failed: '%s'.", qPrintable(db.lastError().text()));
    return updated_messages;
  }

  foreach (Message message, messages) {
    // Check if messages contain relative URLs and if they do, then replace them.
    if (message.m_url.startsWith(QL1S("//"))) {
      message.m_url = QString(URI_SCHEME_HTTP) + message.m_url.mid(2);
    }
    else if (message.m_url.startsWith(QL1S("/"))) {
      QString new_message_url = QUrl(url).toString(QUrl::RemoveUserInfo |
                                                   QUrl::RemovePath |
                                                   QUrl::RemoveQuery |
                                                   QUrl::RemoveFilename |
                                                   QUrl::StripTrailingSlash);

      new_message_url += message.m_url;
      message.m_url = new_message_url;
    }

    int id_existing_message = -1;
    qint64 date_existing_message;
    bool is_read_existing_message;
    bool is_important_existing_message;

    if (message.m_customId.isEmpty()) {
      // We need to recognize existing messages according URL & AUTHOR.
      // NOTE: This concerns messages from standard account.
      query_select_with_url.bindValue(QSL(":feed"), feed_custom_id);
      query_select_with_url.bindValue(QSL(":title"), message.m_title);
      query_select_with_url.bindValue(QSL(":url"), message.m_url);
      query_select_with_url.bindValue(QSL(":author"), message.m_author);
      query_select_with_url.bindValue(QSL(":account_id"), account_id);

      if (query_select_with_url.exec() && query_select_with_url.next()) {
        id_existing_message = query_select_with_url.value(0).toInt();
        date_existing_message = query_select_with_url.value(1).value<qint64>();
        is_read_existing_message = query_select_with_url.value(2).toBool();
        is_important_existing_message = query_select_with_url.value(3).toBool();
      }

      query_select_with_url.finish();
    }
    else {
      // We can recognize existing messages via their custom ID.
      // NOTE: This concerns messages from custom accounts, like TT-RSS or ownCloud News.
      query_select_with_id.bindValue(QSL(":account_id"), account_id);
      query_select_with_id.bindValue(QSL(":custom_id"), message.m_customId);

      if (query_select_with_id.exec() && query_select_with_id.next()) {
        id_existing_message = query_select_with_id.value(0).toInt();
        date_existing_message = query_select_with_id.value(1).value<qint64>();
        is_read_existing_message = query_select_with_id.value(2).toBool();
        is_important_existing_message = query_select_with_id.value(3).toBool();
      }

      query_select_with_id.finish();
    }

    // Now, check if this message is already in the DB.
    if (id_existing_message >= 0) {
      // Message is already in the DB.
      //
      // Now, we update it if at least one of next conditions is true:
      //   1) Message has custom ID AND (its date OR read status OR starred status are changed).
      //   2) Message has its date fetched from feed AND its date is different from date in DB.
      if (/* 1 */ (!message.m_customId.isEmpty() && (message.m_created.toMSecsSinceEpoch() != date_existing_message || message.m_isRead != is_read_existing_message || message.m_isImportant != is_important_existing_message)) ||
          /* 2 */ (message.m_createdFromFeed && message.m_created.toMSecsSinceEpoch() != date_existing_message)) {
        // Message exists, it is changed, update it.
        query_update.bindValue(QSL(":title"), message.m_title);
        query_update.bindValue(QSL(":is_read"), (int) message.m_isRead);
        query_update.bindValue(QSL(":is_important"), (int) message.m_isImportant);
        query_update.bindValue(QSL(":url"), message.m_url);
        query_update.bindValue(QSL(":author"), message.m_author);
        query_update.bindValue(QSL(":date_created"), message.m_created.toMSecsSinceEpoch());
        query_update.bindValue(QSL(":contents"), message.m_contents);
        query_update.bindValue(QSL(":enclosures"), Enclosures::encodeEnclosuresToString(message.m_enclosures));
        query_update.bindValue(QSL(":id"), id_existing_message);

        *any_message_changed = true;

        if (query_update.exec() && !message.m_isRead) {
          updated_messages++;
        }

        query_update.finish();
        qDebug("Updating message '%s' in DB.", qPrintable(message.m_title));
      }
    }
    else {
      // Message with this URL is not fetched in this feed yet.
      query_insert.bindValue(QSL(":feed"), feed_custom_id);
      query_insert.bindValue(QSL(":title"), message.m_title);
      query_insert.bindValue(QSL(":is_read"), (int) message.m_isRead);
      query_insert.bindValue(QSL(":is_important"), (int) message.m_isImportant);
      query_insert.bindValue(QSL(":url"), message.m_url);
      query_insert.bindValue(QSL(":author"), message.m_author);
      query_insert.bindValue(QSL(":date_created"), message.m_created.toMSecsSinceEpoch());
      query_insert.bindValue(QSL(":contents"), message.m_contents);
      query_insert.bindValue(QSL(":enclosures"), Enclosures::encodeEnclosuresToString(message.m_enclosures));
      query_insert.bindValue(QSL(":custom_id"), message.m_customId);
      query_insert.bindValue(QSL(":custom_hash"), message.m_customHash);
      query_insert.bindValue(QSL(":account_id"), account_id);

      if (query_insert.exec() && query_insert.numRowsAffected() == 1) {
        updated_messages++;
      }

      query_insert.finish();
      qDebug("Adding new message '%s' to DB.", qPrintable(message.m_title));
    }
  }

  // Now, fixup custom IDS for messages which initially did not have them,
  // just to keep the data consistent.
  if (db.exec("UPDATE Messages "
              "SET custom_id = (SELECT id FROM Messages t WHERE t.id = Messages.id) "
              "WHERE Messages.custom_id IS NULL OR Messages.custom_id = '';").lastError().isValid()) {
    qWarning("Failed to set custom ID for all messages.");
  }

  if (!db.commit()) {
    db.rollback();
    qDebug("Transaction commit for message downloader failed.");

    if (ok != NULL) {
      *ok = false;
    }
  }
  else {
    if (ok != NULL) {
      *ok = true;
    }
  }

  return updated_messages;
}

bool DatabaseQueries::purgeMessagesFromBin(QSqlDatabase db, bool clear_only_read, int account_id) {
  QSqlQuery q(db);
  q.setForwardOnly(true);

  if (clear_only_read) {
    q.prepare("UPDATE Messages SET is_pdeleted = 1 "
              "WHERE is_read = 1 AND is_deleted = 1 AND account_id = :account_id;");
  }
  else {
    q.prepare(QSL("UPDATE Messages SET is_pdeleted = 1 WHERE is_deleted = 1 AND account_id = :account_id;"));
  }

  q.bindValue(QSL(":account_id"), account_id);

  return q.exec();
}

bool DatabaseQueries::deleteAccount(QSqlDatabase db, int account_id) {
  QSqlQuery query(db);
  query.setForwardOnly(true);

  QStringList queries;
  queries << QSL("DELETE FROM Messages WHERE account_id = :account_id;") <<
             QSL("DELETE FROM Feeds WHERE account_id = :account_id;") <<
             QSL("DELETE FROM Categories WHERE account_id = :account_id;") <<
             QSL("DELETE FROM Accounts WHERE id = :account_id;");

  foreach (const QString &q, queries) {
    query.prepare(q);
    query.bindValue(QSL(":account_id"), account_id);

    if (!query.exec()) {
      qCritical("Removing of account from DB failed, this is critical: '%s'.", qPrintable(query.lastError().text()));
      return false;
    }
    else {
      query.finish();
    }
  }

  return true;
}

bool DatabaseQueries::deleteAccountData(QSqlDatabase db, int account_id, bool delete_messages_too) {
  bool result = true;
  QSqlQuery q(db);
  q.setForwardOnly(true);

  if (delete_messages_too) {
    q.prepare(QSL("DELETE FROM Messages WHERE account_id = :account_id;"));
    q.bindValue(QSL(":account_id"), account_id);

    result &= q.exec();
  }

  q.prepare(QSL("DELETE FROM Feeds WHERE account_id = :account_id;"));
  q.bindValue(QSL(":account_id"), account_id);

  result &= q.exec();

  q.prepare(QSL("DELETE FROM Categories WHERE account_id = :account_id;"));
  q.bindValue(QSL(":account_id"), account_id);

  result &= q.exec();

  return result;
}

bool DatabaseQueries::cleanFeeds(QSqlDatabase db, const QStringList &ids, bool clean_read_only, int account_id) {
  QSqlQuery q(db);
  q.setForwardOnly(true);

  if (clean_read_only) {
    q.prepare(QString("UPDATE Messages SET is_deleted = :deleted "
                      "WHERE feed IN (%1) AND is_deleted = 0 AND is_pdeleted = 0 AND is_read = 1 AND account_id = :account_id;")
              .arg(ids.join(QSL(", "))));
  }
  else {
    q.prepare(QString("UPDATE Messages SET is_deleted = :deleted "
                      "WHERE feed IN (%1) AND is_deleted = 0 AND is_pdeleted = 0 AND account_id = :account_id;")
              .arg(ids.join(QSL(", "))));
  }

  q.bindValue(QSL(":deleted"), 1);
  q.bindValue(QSL(":account_id"), account_id);

  if (!q.exec()) {
    qDebug("Cleaning of feeds failed: '%s'.", qPrintable(q.lastError().text()));
    return false;
  }
  else {
    return true;
  }
}

bool DatabaseQueries::purgeLeftoverMessages(QSqlDatabase db, int account_id) {
  QSqlQuery q(db);

  q.setForwardOnly(true);
  q.prepare(QSL("DELETE FROM Messages WHERE account_id = :account_id AND feed NOT IN (SELECT custom_id FROM Feeds WHERE account_id = :account_id);"));
  q.bindValue(QSL(":account_id"), account_id);

  if (!q.exec()) {
    qWarning("Removing of left over messages failed: '%s'.", qPrintable(q.lastError().text()));
    return false;
  }
  else {
    return true;
  }
}

bool DatabaseQueries::storeAccountTree(QSqlDatabase db, RootItem *tree_root, int account_id) {
  QSqlQuery query_category(db);
  QSqlQuery query_feed(db);
  query_category.setForwardOnly(true);
  query_feed.setForwardOnly(true);
  query_category.prepare("INSERT INTO Categories (parent_id, title, account_id, custom_id) "
                         "VALUES (:parent_id, :title, :account_id, :custom_id);");
  query_feed.prepare("INSERT INTO Feeds (title, icon, category, protected, update_type, update_interval, account_id, custom_id) "
                     "VALUES (:title, :icon, :category, :protected, :update_type, :update_interval, :account_id, :custom_id);");

  // Iterate all children.
  foreach (RootItem *child, tree_root->getSubTree()) {
    if (child->kind() == RootItemKind::Category) {
      query_category.bindValue(QSL(":parent_id"), child->parent()->id());
      query_category.bindValue(QSL(":title"), child->title());
      query_category.bindValue(QSL(":account_id"), account_id);
      query_category.bindValue(QSL(":custom_id"), QString::number(child->toCategory()->customId()));

      if (query_category.exec()) {
        child->setId(query_category.lastInsertId().toInt());
      }
      else {
        return false;
      }
    }
    else if (child->kind() == RootItemKind::Feed) {
      Feed *feed = child->toFeed();

      query_feed.bindValue(QSL(":title"), feed->title());
      query_feed.bindValue(QSL(":icon"), qApp->icons()->toByteArray(feed->icon()));
      query_feed.bindValue(QSL(":category"), feed->parent()->customId());
      query_feed.bindValue(QSL(":protected"), 0);
      query_feed.bindValue(QSL(":update_type"), (int) feed->autoUpdateType());
      query_feed.bindValue(QSL(":update_interval"), feed->autoUpdateInitialInterval());
      query_feed.bindValue(QSL(":account_id"), account_id);
      query_feed.bindValue(QSL(":custom_id"), feed->customId());

      if (query_feed.exec()) {
        feed->setId(query_feed.lastInsertId().toInt());
      }
      else {
        return false;
      }
    }
  }

  return true;
}

QStringList DatabaseQueries::customIdsOfMessagesFromAccount(QSqlDatabase db, int account_id, bool *ok) {
  QSqlQuery q(db);
  QStringList ids;
  q.setForwardOnly(true);
  q.prepare(QSL("SELECT custom_id FROM Messages WHERE is_deleted = 0 AND is_pdeleted = 0 AND account_id = :account_id;"));
  q.bindValue(QSL(":account_id"), account_id);

  if (ok != NULL) {
    *ok = q.exec();
  }
  else {
    q.exec();
  }

  while (q.next()) {
    ids.append(q.value(0).toString());
  }

  return ids;
}

QStringList DatabaseQueries::customIdsOfMessagesFromBin(QSqlDatabase db, int account_id, bool *ok) {
  QSqlQuery q(db);
  QStringList ids;
  q.setForwardOnly(true);
  q.prepare(QSL("SELECT custom_id FROM Messages WHERE is_deleted = 1 AND is_pdeleted = 0 AND account_id = :account_id;"));
  q.bindValue(QSL(":account_id"), account_id);

  if (ok != NULL) {
    *ok = q.exec();
  }
  else {
    q.exec();
  }

  while (q.next()) {
    ids.append(q.value(0).toString());
  }

  return ids;
}

QStringList DatabaseQueries::customIdsOfMessagesFromFeed(QSqlDatabase db, int feed_custom_id, int account_id, bool *ok) {
  QSqlQuery q(db);
  QStringList ids;
  q.setForwardOnly(true);
  q.prepare(QSL("SELECT custom_id FROM Messages WHERE is_deleted = 0 AND is_pdeleted = 0 AND feed = :feed AND account_id = :account_id;"));
  q.bindValue(QSL(":account_id"), account_id);
  q.bindValue(QSL(":feed"), feed_custom_id);

  if (ok != NULL) {
    *ok = q.exec();
  }
  else {
    q.exec();
  }

  while (q.next()) {
    ids.append(q.value(0).toString());
  }

  return ids;
}

QList<ServiceRoot*> DatabaseQueries::getOwnCloudAccounts(QSqlDatabase db, bool *ok) {
  QSqlQuery query(db);
  QList<ServiceRoot*> roots;

  if (query.exec("SELECT * FROM OwnCloudAccounts;")) {
    while (query.next()) {
      OwnCloudServiceRoot *root = new OwnCloudServiceRoot();
      root->setId(query.value(0).toInt());
      root->setAccountId(query.value(0).toInt());
      root->network()->setAuthUsername(query.value(1).toString());
      root->network()->setAuthPassword(TextFactory::decrypt(query.value(2).toString()));
      root->network()->setUrl(query.value(3).toString());
      root->network()->setForceServerSideUpdate(query.value(4).toBool());

      root->updateTitle();
      roots.append(root);
    }

    if (ok != NULL) {
      *ok = true;
    }
  }
  else {
    qWarning("OwnCloud: Getting list of activated accounts failed: '%s'.", qPrintable(query.lastError().text()));

    if (ok != NULL) {
      *ok = false;
    }
  }

  return roots;
}

QList<ServiceRoot*> DatabaseQueries::getTtRssAccounts(QSqlDatabase db, bool *ok) {
  QSqlQuery query(db);
  QList<ServiceRoot*> roots;

  if (query.exec("SELECT * FROM TtRssAccounts;")) {
    while (query.next()) {
      TtRssServiceRoot *root = new TtRssServiceRoot();
      root->setId(query.value(0).toInt());
      root->setAccountId(query.value(0).toInt());
      root->network()->setUsername(query.value(1).toString());
      root->network()->setPassword(TextFactory::decrypt(query.value(2).toString()));
      root->network()->setAuthIsUsed(query.value(3).toBool());
      root->network()->setAuthUsername(query.value(4).toString());
      root->network()->setAuthPassword(TextFactory::decrypt(query.value(5).toString()));
      root->network()->setUrl(query.value(6).toString());
      root->network()->setForceServerSideUpdate(query.value(7).toBool());

      root->updateTitle();
      roots.append(root);
    }

    if (ok != NULL) {
      *ok = true;
    }
  }
  else {
    qWarning("TT-RSS: Getting list of activated accounts failed: '%s'.", qPrintable(query.lastError().text()));

    if (ok != NULL) {
      *ok = false;
    }
  }

  return roots;
}

bool DatabaseQueries::deleteOwnCloudAccount(QSqlDatabase db, int account_id) {
  QSqlQuery q(db);

  q.setForwardOnly(true);
  q.prepare(QSL("DELETE FROM OwnCloudAccounts WHERE id = :id;"));
  q.bindValue(QSL(":id"), account_id);

  return q.exec();
}

bool DatabaseQueries::overwriteOwnCloudAccount(QSqlDatabase db, const QString &username, const QString &password,
                                               const QString &url, bool force_server_side_feed_update, int account_id) {
  QSqlQuery query(db);

  query.prepare("UPDATE OwnCloudAccounts "
                "SET username = :username, password = :password, url = :url, force_update = :force_update "
                "WHERE id = :id;");
  query.bindValue(QSL(":username"), username);
  query.bindValue(QSL(":password"), TextFactory::encrypt(password));
  query.bindValue(QSL(":url"), url);
  query.bindValue(QSL(":force_update"), force_server_side_feed_update ? 1 : 0);
  query.bindValue(QSL(":id"), account_id);

  if (query.exec()) {
    return true;
  }
  else {
    qWarning("ownCloud: Updating account failed: '%s'.", qPrintable(query.lastError().text()));
    return false;
  }
}

bool DatabaseQueries::createOwnCloudAccount(QSqlDatabase db, int id_to_assign, const QString &username,
                                            const QString &password, const QString &url,
                                            bool force_server_side_feed_update) {
  QSqlQuery q(db);

  q.prepare("INSERT INTO OwnCloudAccounts (id, username, password, url, force_update) "
            "VALUES (:id, :username, :password, :url, :force_update);");
  q.bindValue(QSL(":id"), id_to_assign);
  q.bindValue(QSL(":username"), username);
  q.bindValue(QSL(":password"), TextFactory::encrypt(password));
  q.bindValue(QSL(":url"), url);
  q.bindValue(QSL(":force_update"), force_server_side_feed_update ? 1 : 0);

  if (q.exec()) {
    return true;
  }
  else {
    qWarning("ownCloud: Inserting of new account failed: '%s'.", qPrintable(q.lastError().text()));
    return false;
  }
}

int DatabaseQueries::createAccount(QSqlDatabase db, const QString &code, bool *ok) {
  QSqlQuery q(db);

  // First obtain the ID, which can be assigned to this new account.
  if (!q.exec("SELECT max(id) FROM Accounts;") || !q.next()) {
    qWarning("Getting max ID from Accounts table failed: '%s'.", qPrintable(q.lastError().text()));

    if (ok != NULL) {
      *ok = false;
    }

    return 0;
  }

  int id_to_assign = q.value(0).toInt() + 1;

  q.prepare(QSL("INSERT INTO Accounts (id, type) VALUES (:id, :type);"));
  q.bindValue(QSL(":id"), id_to_assign);
  q.bindValue(QSL(":type"), code);

  if (q.exec()) {
    if (ok != NULL) {
      *ok = true;
    }

    return id_to_assign;
  }
  else {
    if (ok != NULL) {
      *ok = false;
    }

    qWarning("Inserting of new account failed: '%s'.", qPrintable(q.lastError().text()));
    return 0;
  }
}

Assignment DatabaseQueries::getOwnCloudCategories(QSqlDatabase db, int account_id, bool *ok) {
  Assignment categories;

  // Obtain data for categories from the database.
  QSqlQuery q(db);
  q.setForwardOnly(true);
  q.prepare(QSL("SELECT * FROM Categories WHERE account_id = :account_id;"));
  q.bindValue(QSL(":account_id"), account_id);

  if (!q.exec()) {
    qFatal("ownCloud: Query for obtaining categories failed. Error message: '%s'.", qPrintable(q.lastError().text()));

    if (ok != NULL) {
      *ok = false;
    }
  }

  while (q.next()) {
    AssignmentItem pair;
    pair.first = q.value(CAT_DB_PARENT_ID_INDEX).toInt();
    pair.second = new OwnCloudCategory(q.record());

    categories << pair;
  }

  if (ok != NULL) {
    *ok = true;
  }

  return categories;
}

Assignment DatabaseQueries::getOwnCloudFeeds(QSqlDatabase db, int account_id, bool *ok) {
  Assignment feeds;

  QSqlQuery q(db);
  q.setForwardOnly(true);
  q.prepare(QSL("SELECT * FROM Feeds WHERE account_id = :account_id;"));
  q.bindValue(QSL(":account_id"), account_id);

  if (!q.exec()) {
    qFatal("ownCloud: Query for obtaining feeds failed. Error message: '%s'.", qPrintable(q.lastError().text()));

    if (ok != NULL) {
      *ok = false;
    }
  }

  while (q.next()) {
    AssignmentItem pair;
    pair.first = q.value(FDS_DB_CATEGORY_INDEX).toInt();
    pair.second = new OwnCloudFeed(q.record());

    feeds << pair;
  }

  if (ok != NULL) {
    *ok = true;
  }

  return feeds;
}

bool DatabaseQueries::deleteFeed(QSqlDatabase db, int feed_custom_id, int account_id) {
  QSqlQuery q(db);
  q.setForwardOnly(true);

  // Remove all messages from this feed.
  q.prepare(QSL("DELETE FROM Messages WHERE feed = :feed AND account_id = :account_id;"));
  q.bindValue(QSL(":feed"), feed_custom_id);
  q.bindValue(QSL(":account_id"), account_id);

  if (!q.exec()) {
    return false;
  }

  // Remove feed itself.
  q.prepare(QSL("DELETE FROM Feeds WHERE custom_id = :feed AND account_id = :account_id;"));
  q.bindValue(QSL(":feed"), feed_custom_id);
  q.bindValue(QSL(":account_id"), account_id);

  return q.exec();
}

bool DatabaseQueries::deleteCategory(QSqlDatabase db, int id) {
  QSqlQuery q(db);

  // Remove this category from database.
  q.setForwardOnly(true);
  q.prepare(QSL("DELETE FROM Categories WHERE id = :category;"));
  q.bindValue(QSL(":category"), id);

  return q.exec();
}

int DatabaseQueries::addCategory(QSqlDatabase db, int parent_id, int account_id, const QString &title,
                                 const QString &description, QDateTime creation_date, const QIcon &icon,
                                 bool *ok) {
  QSqlQuery q(db);

  q.setForwardOnly(true);
  q.prepare("INSERT INTO Categories "
            "(parent_id, title, description, date_created, icon, account_id) "
            "VALUES (:parent_id, :title, :description, :date_created, :icon, :account_id);");
  q.bindValue(QSL(":parent_id"), parent_id);
  q.bindValue(QSL(":title"), title);
  q.bindValue(QSL(":description"), description);
  q.bindValue(QSL(":date_created"), creation_date.toMSecsSinceEpoch());
  q.bindValue(QSL(":icon"), qApp->icons()->toByteArray(icon));
  q.bindValue(QSL(":account_id"), account_id);

  if (!q.exec()) {
    qDebug("Failed to add category to database: '%s'.", qPrintable(q.lastError().text()));

    if (ok != NULL) {
      *ok = false;
    }

    // Query failed.
    return 0;
  }
  else {
    if (ok != NULL) {
      *ok = true;
    }

    int new_id = q.lastInsertId().toInt();

    // Now set custom ID in the DB.
    q.prepare(QSL("UPDATE Categories SET custom_id = :custom_id WHERE id = :id;"));
    q.bindValue(QSL(":custom_id"), QString::number(new_id));
    q.bindValue(QSL(":id"), new_id);
    q.exec();

    return new_id;
  }
}

bool DatabaseQueries::editCategory(QSqlDatabase db, int parent_id, int category_id,
                                   const QString &title, const QString &description, const QIcon &icon) {
  QSqlQuery q(db);

  q.setForwardOnly(true);
  q.prepare("UPDATE Categories "
            "SET title = :title, description = :description, icon = :icon, parent_id = :parent_id "
            "WHERE id = :id;");
  q.bindValue(QSL(":title"), title);
  q.bindValue(QSL(":description"), description);
  q.bindValue(QSL(":icon"), qApp->icons()->toByteArray(icon));
  q.bindValue(QSL(":parent_id"), parent_id);
  q.bindValue(QSL(":id"), category_id);

  return q.exec();
}

int DatabaseQueries::addFeed(QSqlDatabase db, int parent_id, int account_id, const QString &title,
                             const QString &description, QDateTime creation_date, const QIcon &icon,
                             const QString &encoding, const QString &url, bool is_protected,
                             const QString &username, const QString &password,
                             Feed::AutoUpdateType auto_update_type,
                             int auto_update_interval, StandardFeed::Type feed_format, bool *ok) {
  QSqlQuery q(db);

  q.setForwardOnly(true);
  q.prepare("INSERT INTO Feeds "
            "(title, description, date_created, icon, category, encoding, url, protected, username, password, update_type, update_interval, type, account_id) "
            "VALUES (:title, :description, :date_created, :icon, :category, :encoding, :url, :protected, :username, :password, :update_type, :update_interval, :type, :account_id);");
  q.bindValue(QSL(":title"), title);
  q.bindValue(QSL(":description"), description);
  q.bindValue(QSL(":date_created"), creation_date.toMSecsSinceEpoch());
  q.bindValue(QSL(":icon"), qApp->icons()->toByteArray(icon));
  q.bindValue(QSL(":category"), parent_id);
  q.bindValue(QSL(":encoding"), encoding);
  q.bindValue(QSL(":url"), url);
  q.bindValue(QSL(":protected"), is_protected ? 1 : 0);
  q.bindValue(QSL(":username"), username);
  q.bindValue(QSL(":account_id"), account_id);

  if (password.isEmpty()) {
    q.bindValue(QSL(":password"), password);
  }
  else {
    q.bindValue(QSL(":password"), TextFactory::encrypt(password));
  }

  q.bindValue(QSL(":update_type"), (int) auto_update_type);
  q.bindValue(QSL(":update_interval"), auto_update_interval);
  q.bindValue(QSL(":type"), (int) feed_format);

  if (q.exec()) {
    int new_id = q.lastInsertId().toInt();

    // Now set custom ID in the DB.
    q.prepare(QSL("UPDATE Feeds SET custom_id = :custom_id WHERE id = :id;"));
    q.bindValue(QSL(":custom_id"), QString::number(new_id));
    q.bindValue(QSL(":id"), new_id);
    q.exec();

    if (ok != NULL) {
      *ok = true;
    }

    return new_id;
  }
  else {
    if (ok != NULL) {
      *ok = false;
    }

    qDebug("Failed to add feed to database: '%s'.", qPrintable(q.lastError().text()));
    return 0;
  }
}

bool DatabaseQueries::editFeed(QSqlDatabase db, int parent_id, int feed_id, const QString &title,
                               const QString &description, const QIcon &icon,
                               const QString &encoding, const QString &url, bool is_protected,
                               const QString &username, const QString &password,
                               Feed::AutoUpdateType auto_update_type,
                               int auto_update_interval, StandardFeed::Type feed_format) {
  QSqlQuery q(db);
  q.setForwardOnly(true);

  q.prepare("UPDATE Feeds "
            "SET title = :title, description = :description, icon = :icon, category = :category, encoding = :encoding, url = :url, protected = :protected, username = :username, password = :password, update_type = :update_type, update_interval = :update_interval, type = :type "
            "WHERE id = :id;");
  q.bindValue(QSL(":title"), title);
  q.bindValue(QSL(":description"), description);
  q.bindValue(QSL(":icon"), qApp->icons()->toByteArray(icon));
  q.bindValue(QSL(":category"), parent_id);
  q.bindValue(QSL(":encoding"), encoding);
  q.bindValue(QSL(":url"), url);
  q.bindValue(QSL(":protected"), is_protected ? 1 : 0);
  q.bindValue(QSL(":username"), username);

  if (password.isEmpty()) {
    q.bindValue(QSL(":password"), password);
  }
  else {
    q.bindValue(QSL(":password"), TextFactory::encrypt(password));
  }

  q.bindValue(QSL(":update_type"), (int) auto_update_type);
  q.bindValue(QSL(":update_interval"), auto_update_interval);
  q.bindValue(QSL(":type"), feed_format);
  q.bindValue(QSL(":id"), feed_id);

  return q.exec();
}

bool DatabaseQueries::editBaseFeed(QSqlDatabase db, int feed_id, Feed::AutoUpdateType auto_update_type,
                                   int auto_update_interval) {
  QSqlQuery q(db);

  q.setForwardOnly(true);
  q.prepare("UPDATE Feeds "
            "SET update_type = :update_type, update_interval = :update_interval "
            "WHERE id = :id;");

  q.bindValue(QSL(":update_type"), (int) auto_update_type);
  q.bindValue(QSL(":update_interval"), auto_update_interval);
  q.bindValue(QSL(":id"), feed_id);

  return q.exec();
}

QList<ServiceRoot*> DatabaseQueries::getAccounts(QSqlDatabase db, bool *ok) {
  QSqlQuery q(db);
  QList<ServiceRoot*> roots;

  q.setForwardOnly(true);
  q.prepare(QSL("SELECT id FROM Accounts WHERE type = :type;"));
  q.bindValue(QSL(":type"), SERVICE_CODE_STD_RSS);

  if (q.exec()) {
    while (q.next()) {
      StandardServiceRoot *root = new StandardServiceRoot();
      root->setAccountId(q.value(0).toInt());
      roots.append(root);
    }

    if (ok != NULL) {
      *ok = true;
    }
  }
  else {
    if (ok != NULL) {
      *ok = false;
    }
  }

  return roots;
}

Assignment DatabaseQueries::getCategories(QSqlDatabase db, int account_id, bool *ok) {
  Assignment categories;

  // Obtain data for categories from the database.
  QSqlQuery q(db);
  q.setForwardOnly(true);
  q.prepare(QSL("SELECT * FROM Categories WHERE account_id = :account_id;"));
  q.bindValue(QSL(":account_id"), account_id);

  if (!q.exec()) {
    qFatal("Query for obtaining categories failed. Error message: '%s'.",
           qPrintable(q.lastError().text()));

    if (ok != NULL) {
      *ok = false;
    }
  }
  else {
    if (ok != NULL) {
      *ok = true;
    }
  }

  while (q.next()) {
    AssignmentItem pair;
    pair.first = q.value(CAT_DB_PARENT_ID_INDEX).toInt();
    pair.second = new StandardCategory(q.record());

    categories << pair;
  }

  return categories;
}

Assignment DatabaseQueries::getFeeds(QSqlDatabase db, int account_id, bool *ok) {
  Assignment feeds;
  QSqlQuery q(db);

  q.setForwardOnly(true);
  q.prepare(QSL("SELECT * FROM Feeds WHERE account_id = :account_id;"));
  q.bindValue(QSL(":account_id"), account_id);

  if (!q.exec()) {
    qFatal("Query for obtaining feeds failed. Error message: '%s'.",
           qPrintable(q.lastError().text()));

    if (ok != NULL) {
      *ok = false;
    }
  }

  if (ok != NULL) {
    *ok = true;
  }

  while (q.next()) {
    // Process this feed.
    StandardFeed::Type type = static_cast<StandardFeed::Type>(q.value(FDS_DB_TYPE_INDEX).toInt());

    switch (type) {
      case StandardFeed::Atom10:
      case StandardFeed::Rdf:
      case StandardFeed::Rss0X:
      case StandardFeed::Rss2X: {
        AssignmentItem pair;
        pair.first = q.value(FDS_DB_CATEGORY_INDEX).toInt();
        pair.second = new StandardFeed(q.record());
        qobject_cast<StandardFeed*>(pair.second)->setType(type);

        feeds << pair;
        break;
      }

      default:
        break;
    }
  }

  return feeds;
}

bool DatabaseQueries::deleteTtRssAccount(QSqlDatabase db, int account_id) {
  QSqlQuery q(db);

  q.setForwardOnly(true);
  q.prepare(QSL("DELETE FROM TtRssAccounts WHERE id = :id;"));
  q.bindValue(QSL(":id"), account_id);

  // Remove extra entry in "Tiny Tiny RSS accounts list" and then delete
  // all the categories/feeds and messages.
  return q.exec();
}

bool DatabaseQueries::overwriteTtRssAccount(QSqlDatabase db, const QString &username, const QString &password,
                                            bool auth_protected, const QString &auth_username, const QString &auth_password,
                                            const QString &url, bool force_server_side_feed_update, int account_id) {
  QSqlQuery q(db);

  q.prepare("UPDATE TtRssAccounts "
            "SET username = :username, password = :password, url = :url, auth_protected = :auth_protected, "
            "auth_username = :auth_username, auth_password = :auth_password, force_update = :force_update "
            "WHERE id = :id;");
  q.bindValue(QSL(":username"), username);
  q.bindValue(QSL(":password"), TextFactory::encrypt(password));
  q.bindValue(QSL(":url"), url);
  q.bindValue(QSL(":auth_protected"), auth_protected ? 1 : 0);
  q.bindValue(QSL(":auth_username"), auth_username);
  q.bindValue(QSL(":auth_password"), TextFactory::encrypt(auth_password));
  q.bindValue(QSL(":force_update"), force_server_side_feed_update ? 1 : 0);
  q.bindValue(QSL(":id"), account_id);

  if (q.exec()) {
    return true;
  }
  else {
    qWarning("TT-RSS: Updating account failed: '%s'.", qPrintable(q.lastError().text()));
    return false;
  }
}

bool DatabaseQueries::createTtRssAccount(QSqlDatabase db, int id_to_assign, const QString &username,
                                         const QString &password, bool auth_protected, const QString &auth_username,
                                         const QString &auth_password, const QString &url,
                                         bool force_server_side_feed_update) {
  QSqlQuery q(db);

  q.prepare("INSERT INTO TtRssAccounts (id, username, password, auth_protected, auth_username, auth_password, url, force_update) "
            "VALUES (:id, :username, :password, :auth_protected, :auth_username, :auth_password, :url, :force_update);");
  q.bindValue(QSL(":id"), id_to_assign);
  q.bindValue(QSL(":username"), username);
  q.bindValue(QSL(":password"), TextFactory::encrypt(password));
  q.bindValue(QSL(":auth_protected"), auth_protected ? 1 : 0);
  q.bindValue(QSL(":auth_username"), auth_username);
  q.bindValue(QSL(":auth_password"), TextFactory::encrypt(auth_password));
  q.bindValue(QSL(":url"), url);
  q.bindValue(QSL(":force_update"), force_server_side_feed_update ? 1 : 0);

  if (q.exec()) {
    return true;
  }
  else {
    qWarning("TT-RSS: Saving of new account failed: '%s'.", qPrintable(q.lastError().text()));
    return false;
  }
}

Assignment DatabaseQueries::getTtRssCategories(QSqlDatabase db, int account_id, bool *ok) {
  Assignment categories;

  // Obtain data for categories from the database.
  QSqlQuery query_categories(db);
  query_categories.setForwardOnly(true);
  query_categories.prepare(QSL("SELECT * FROM Categories WHERE account_id = :account_id;"));
  query_categories.bindValue(QSL(":account_id"), account_id);

  if (!query_categories.exec()) {
    qFatal("Query for obtaining categories failed. Error message: '%s'.", qPrintable(query_categories.lastError().text()));

    if (ok != NULL) {
      *ok = false;
    }
  }
  else {
    if (ok != NULL) {
      *ok = true;
    }
  }

  while (query_categories.next()) {
    AssignmentItem pair;
    pair.first = query_categories.value(CAT_DB_PARENT_ID_INDEX).toInt();
    pair.second = new TtRssCategory(query_categories.record());

    categories << pair;
  }

  return categories;
}

Assignment DatabaseQueries::getTtRssFeeds(QSqlDatabase db, int account_id, bool *ok) {
  Assignment feeds;

  // All categories are now loaded.
  QSqlQuery query_feeds(db);
  query_feeds.setForwardOnly(true);
  query_feeds.prepare(QSL("SELECT * FROM Feeds WHERE account_id = :account_id;"));
  query_feeds.bindValue(QSL(":account_id"), account_id);

  if (!query_feeds.exec()) {
    qFatal("Query for obtaining feeds failed. Error message: '%s'.", qPrintable(query_feeds.lastError().text()));

    if (ok != NULL) {
      *ok = false;
    }
  }
  else {
    if (ok != NULL) {
      *ok = true;
    }
  }

  while (query_feeds.next()) {
    AssignmentItem pair;
    pair.first = query_feeds.value(FDS_DB_CATEGORY_INDEX).toInt();
    pair.second = new TtRssFeed(query_feeds.record());

    feeds << pair;
  }

  return feeds;
}

DatabaseQueries::DatabaseQueries() {
}