// Copyright (C) 2026 Furkan Selim Cetin
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include "FilterCondition.h"
#include "String.hpp"

#include <cstdint>
#include <memory>

namespace arrow
{
class Array;
class Table;
}

using CellString = PuqiAR::LibString::String;

struct ColumnInfo
{
    QString name;
    QString type;
};

struct QueryPage
{
    QStringList columns;
    QVector<QVector<CellString>> rows;
};

class ParquetDocument final : public QObject
{
    Q_OBJECT

public:
    explicit ParquetDocument(QObject *parent = nullptr);
    ~ParquetDocument() override;

    bool open(const QString &filePath, QString *errorMessage);
    bool isOpen() const;

    QString filePath() const;
    QVector<ColumnInfo> schema() const;
    QStringList columnNames() const;

    bool rowCount(const RowFilter &filter, qsizetype *count, QString *errorMessage);
    bool queryPage(const RowFilter &filter, qsizetype limit, qsizetype offset, QueryPage *page, QString *errorMessage);

private:
    static bool matches(const RowCondition &condition, const arrow::Array &array, int64_t index);
    bool applyFilter(const RowFilter &filter, QString *errorMessage);

    QString filePath_;
    QVector<ColumnInfo> schema_;
    std::shared_ptr<arrow::Table> table_;
    QString cachedFilterKey_;
    QVector<int64_t> cachedRows_;
    bool filtered_ = false;
    bool cacheValid_ = false;
};
