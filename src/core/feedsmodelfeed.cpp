#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include "core/databasefactory.h"
#include "core/feedsmodelfeed.h"


FeedsModelFeed::FeedsModelFeed(FeedsModelRootItem *parent_item)
  : FeedsModelRootItem(parent_item), m_totalCount(0), m_unreadCount(0) {
}

FeedsModelFeed::~FeedsModelFeed() {
}

int FeedsModelFeed::childCount() const {
  // Because feed has no children.
  return 0;
}

int FeedsModelFeed::countOfAllMessages() const {
  return m_totalCount;
}

int FeedsModelFeed::countOfUnreadMessages() const {
  return m_unreadCount;
}

FeedsModelFeed::Type FeedsModelFeed::type() const {
  return m_type;
}

void FeedsModelFeed::setType(const Type &type) {
  m_type = type;
}

void FeedsModelFeed::setTitle(const QString &title) {
  m_title = title;
}

QString FeedsModelFeed::title() const {
  return m_title;
}