// Copyright (C) 2026 Furkan Selim Cetin
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include "String.hpp"

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

    bool rowCount(const QString &whereClause, qsizetype *count, QString *errorMessage);
    bool queryPage(const QString &whereClause, qsizetype limit, qsizetype offset, QueryPage *page, QString *errorMessage);

private:
    bool runCsvQuery(const QString &sql, QStringList *header, QVector<QVector<CellString>> *rows, QString *errorMessage);
    bool loadSchema(QString *errorMessage);
    QString parquetSourceSql() const;
    QString duckdbExecutable() const;

    QString filePath_;
    QVector<ColumnInfo> schema_;
};

QString quoteSqlString(const QString &value);
QString quoteSqlIdentifier(const QString &identifier);
