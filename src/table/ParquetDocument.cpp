// Copyright (C) 2026 Furkan Selim Cetin
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ParquetDocument.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

#ifndef DUCKDB_EXECUTABLE
#define DUCKDB_EXECUTABLE "duckdb"
#endif

namespace
{

    // RFC 4180 parser: quoted fields may contain separators, quotes ("") and line breaks.
    void parseCsv(const QByteArray &data, QStringList *header, QVector<QVector<CellString>> *rows)
    {
        QVector<CellString> record;
        CellString field;
        bool inQuotes = false;
        bool haveRecord = false;
        bool first = true;

        const auto endField = [&]()
        {
            record.push_back(field);
            field.clear();
        };
        const auto endRecord = [&]()
        {
            endField();
            if (first)
            {
                for(auto &value : record)
                {
                    header->push_back(QString::fromStdString(value.toStdString()));
                }
                first = false;
            }
            else
            {
                rows->push_back(std::move(record));
            }
            record.clear();
            if (!header->isEmpty())
                record.reserve(header->size());
            haveRecord = false;
        };

        const char *p = data.constData();
        const char *const end = p + data.size();
        while (p < end)
        {
            const char c = *p++;
            if (inQuotes)
            {
                if (c == '"')
                {
                    if (p < end && *p == '"')
                    {
                        field.push_back('"');
                        ++p;
                    }
                    else
                    {
                        inQuotes = false;
                    }
                }
                else
                {
                    field.push_back(c);
                }
                continue;
            }
            switch (c)
            {
            case '"':
                inQuotes = true;
                haveRecord = true;
                break;
            case ',':
                endField();
                haveRecord = true;
                break;
            case '\r':
                break;
            case '\n':
                endRecord();
                break;
            default:
                field.push_back(c);
                haveRecord = true;
                break;
            }
        }
        if (haveRecord || !field.empty() || !record.empty())
            endRecord();
    }

} // namespace

ParquetDocument::ParquetDocument(QObject *parent)
    : QObject(parent)
{
}

ParquetDocument::~ParquetDocument()
{
}

bool ParquetDocument::open(const QString &filePath, QString *errorMessage)
{
    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("File does not exist: %1").arg(filePath);
        }
        return false;
    }

    filePath_ = info.absoluteFilePath();
    return loadSchema(errorMessage);
}

bool ParquetDocument::isOpen() const
{
    return !filePath_.isEmpty();
}

QString ParquetDocument::filePath() const
{
    return filePath_;
}

QVector<ColumnInfo> ParquetDocument::schema() const
{
    return schema_;
}

QStringList ParquetDocument::columnNames() const
{
    QStringList names;
    names.reserve(schema_.size());
    for (const auto &column : schema_)
    {
        names.push_back(column.name);
    }
    return names;
}

bool ParquetDocument::rowCount(const QString &whereClause, qsizetype *count, QString *errorMessage)
{
    const QString sql = QStringLiteral("SELECT count(*) FROM %1 %2")
                            .arg(parquetSourceSql(), whereClause.isEmpty() ? QString() : QStringLiteral("WHERE %1").arg(whereClause));

    QStringList header;
    QVector<QVector<CellString>> rows;
    if (!runCsvQuery(sql, &header, &rows, errorMessage))
    {
        return false;
    }

    if (rows.isEmpty() || rows.first().isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("DuckDB returned no row count.");
        }
        return false;
    }

    bool ok = false;
    const qlonglong value = QString::fromStdString(rows.first().first().toStdString()).toLongLong(&ok);
    if (!ok)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("DuckDB returned an invalid row count.");
        }
        return false;
    }

    *count = static_cast<qsizetype>(value);
    return true;
}

bool ParquetDocument::queryPage(const QString &whereClause, qsizetype limit, qsizetype offset, QueryPage *page, QString *errorMessage)
{
    QString sql = QStringLiteral("SELECT * FROM %1 %2")
                      .arg(parquetSourceSql(),
                           whereClause.isEmpty() ? QString() : QStringLiteral("WHERE %1").arg(whereClause));
    if (limit >= 0)
    {
        sql += QStringLiteral(" LIMIT %1 OFFSET %2").arg(limit).arg(offset);
    }

    QStringList header;
    QVector<QVector<CellString>> rows;
    if (!runCsvQuery(sql, &header, &rows, errorMessage))
    {
        return false;
    }

    QueryPage nextPage;
    nextPage.columns = columnNames();
    const qsizetype columnCount = nextPage.columns.size();
    for (auto &row : rows)
    {
        row.resize(columnCount, "NULL");
    }
    nextPage.rows = std::move(rows);

    *page = std::move(nextPage);
    return true;
}

bool ParquetDocument::runCsvQuery(const QString &sql, QStringList *header, QVector<QVector<CellString>> *rows, QString *errorMessage)
{
    QProcess process;
    process.setProgram(duckdbExecutable());
    process.setArguments({
        QStringLiteral("-no-init"),
        QStringLiteral("-csv"),
        QStringLiteral("-nullvalue"),
        QStringLiteral("NULL"),
        QStringLiteral("-c"),
        sql,
    });
    process.start();

    if (!process.waitForStarted())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Could not start DuckDB CLI: %1").arg(duckdbExecutable());
        }
        return false;
    }

    process.waitForFinished(-1);

    const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    const QByteArray stdoutBytes = process.readAllStandardOutput();

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = stderrText.isEmpty()
                                ? QStringLiteral("DuckDB CLI failed.")
                                : stderrText;
        }
        return false;
    }

    header->clear();
    rows->clear();
    parseCsv(stdoutBytes, header, rows);
    return true;
}

bool ParquetDocument::loadSchema(QString *errorMessage)
{
    const QString sql = QStringLiteral("DESCRIBE SELECT * FROM %1").arg(parquetSourceSql());

    QStringList header;
    QVector<QVector<CellString>> rows;
    if (!runCsvQuery(sql, &header, &rows, errorMessage))
    {
        return false;
    }

    const qsizetype nameIndex = header.indexOf(QStringLiteral("column_name"));
    const qsizetype typeIndex = header.indexOf(QStringLiteral("column_type"));
    if (nameIndex < 0 || typeIndex < 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Could not parse DuckDB schema output.");
        }
        return false;
    }

    QVector<ColumnInfo> nextSchema;
    nextSchema.reserve(rows.size());
    for (const auto &row : rows)
    {
        nextSchema.push_back({
            QString::fromStdString(row.value(nameIndex).toStdString()),
            QString::fromStdString(row.value(typeIndex).toStdString()),
        });
    }

    schema_ = std::move(nextSchema);
    return true;
}

QString ParquetDocument::parquetSourceSql() const
{
    return QStringLiteral("read_parquet(%1)").arg(quoteSqlString(filePath_));
}

QString ParquetDocument::duckdbExecutable() const
{
    const QString bundledExecutable = QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
        QStringLiteral("duckdb.exe")
#else
        QStringLiteral("duckdb")
#endif
    );
    if (QFileInfo(bundledExecutable).isExecutable())
    {
        return bundledExecutable;
    }

    return QString::fromUtf8(DUCKDB_EXECUTABLE);
}

QString quoteSqlString(const QString &value)
{
    QString escaped = value;
    escaped.replace('\'', QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString quoteSqlIdentifier(const QString &identifier)
{
    QString escaped = identifier;
    escaped.replace('"', QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}
