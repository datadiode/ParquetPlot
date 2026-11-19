// Copyright (C) 2026 Furkan Selim Cetin
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ParquetDocument.h"

#include "parquet_reader.h"

#include <arrow/array.h>
#include <arrow/chunked_array.h>
#include <arrow/scalar.h>
#include <arrow/table.h>
#include <arrow/type.h>

#include <QApplication>
#include <QFileInfo>
#include <QScopeGuard>

#include <algorithm>
#include <exception>

namespace
{

    // Locates a row inside a chunked column; cheap for sequential access.
    struct ColumnCursor
    {
        const arrow::ChunkedArray *column = nullptr;
        int chunk = 0;
        int64_t chunkStart = 0;

        const arrow::Array *seek(int64_t row, int64_t *local)
        {
            while (chunk > 0 && row < chunkStart)
            {
                --chunk;
                chunkStart -= column->chunk(chunk)->length();
            }
            while (chunk < column->num_chunks() &&
                   row >= chunkStart + column->chunk(chunk)->length())
            {
                chunkStart += column->chunk(chunk)->length();
                ++chunk;
            }
            if (chunk >= column->num_chunks())
                return nullptr;
            *local = row - chunkStart;
            return column->chunk(chunk).get();
        }
    };

    bool isNumericArrowType(arrow::Type::type id)
    {
        return arrow::is_integer(id) || arrow::is_floating(id) || id == arrow::Type::BOOL;
    }

    bool numericValue(const arrow::Array &array, int64_t i, double *out)
    {
        switch (array.type_id())
        {
        case arrow::Type::BOOL:
            *out = static_cast<const arrow::BooleanArray &>(array).Value(i) ? 1.0 : 0.0;
            return true;
        case arrow::Type::INT8:
            *out = static_cast<const arrow::Int8Array &>(array).Value(i);
            return true;
        case arrow::Type::INT16:
            *out = static_cast<const arrow::Int16Array &>(array).Value(i);
            return true;
        case arrow::Type::INT32:
            *out = static_cast<const arrow::Int32Array &>(array).Value(i);
            return true;
        case arrow::Type::INT64:
            *out = static_cast<double>(static_cast<const arrow::Int64Array &>(array).Value(i));
            return true;
        case arrow::Type::UINT8:
            *out = static_cast<const arrow::UInt8Array &>(array).Value(i);
            return true;
        case arrow::Type::UINT16:
            *out = static_cast<const arrow::UInt16Array &>(array).Value(i);
            return true;
        case arrow::Type::UINT32:
            *out = static_cast<const arrow::UInt32Array &>(array).Value(i);
            return true;
        case arrow::Type::UINT64:
            *out = static_cast<double>(static_cast<const arrow::UInt64Array &>(array).Value(i));
            return true;
        case arrow::Type::FLOAT:
            *out = static_cast<const arrow::FloatArray &>(array).Value(i);
            return true;
        case arrow::Type::DOUBLE:
            *out = static_cast<const arrow::DoubleArray &>(array).Value(i);
            return true;
        default:
            return false;
        }
    }

    std::string cellText(const arrow::Array &array, int64_t i)
    {
        if (array.IsNull(i))
            return "NULL";
        switch (array.type_id())
        {
        case arrow::Type::BOOL:
            return static_cast<const arrow::BooleanArray &>(array).Value(i) ? "true" : "false";
        case arrow::Type::FLOAT:
            return std::to_string(static_cast<const arrow::FloatArray&>(array).Value(i));
        case arrow::Type::DOUBLE:
            return std::to_string(static_cast<const arrow::DoubleArray&>(array).Value(i));
        case arrow::Type::INT64:
            return std::to_string(static_cast<const arrow::Int64Array &>(array).Value(i));
        case arrow::Type::UINT64:
            return std::to_string(static_cast<const arrow::UInt64Array &>(array).Value(i));
        case arrow::Type::STRING:
            return std::string(static_cast<const arrow::StringArray &>(array).GetView(i));
        case arrow::Type::LARGE_STRING:
            return std::string(static_cast<const arrow::LargeStringArray &>(array).GetView(i));
        default:
        {
            double value = 0.0;
            if (arrow::is_integer(array.type_id()) && numericValue(array, i, &value))
                return std::to_string(static_cast<int64_t>(value));
            auto scalar = array.GetScalar(i);
            return scalar.ok() ? scalar.ValueOrDie()->ToString() : std::string();
        }
        }
    }

    QString displayType(const arrow::DataType &type)
    {
        switch (type.id())
        {
        case arrow::Type::BOOL:
            return QStringLiteral("BOOLEAN");
        case arrow::Type::INT8:
            return QStringLiteral("TINYINT");
        case arrow::Type::INT16:
            return QStringLiteral("SMALLINT");
        case arrow::Type::INT32:
            return QStringLiteral("INTEGER");
        case arrow::Type::INT64:
            return QStringLiteral("BIGINT");
        case arrow::Type::UINT8:
            return QStringLiteral("UTINYINT");
        case arrow::Type::UINT16:
            return QStringLiteral("USMALLINT");
        case arrow::Type::UINT32:
            return QStringLiteral("UINTEGER");
        case arrow::Type::UINT64:
            return QStringLiteral("UBIGINT");
        case arrow::Type::FLOAT:
            return QStringLiteral("FLOAT");
        case arrow::Type::DOUBLE:
            return QStringLiteral("DOUBLE");
        case arrow::Type::STRING:
        case arrow::Type::LARGE_STRING:
            return QStringLiteral("VARCHAR");
        default:
            return QString::fromStdString(type.ToString()).toUpper();
        }
    }

    QString filterKey(const RowFilter &filter)
    {
        QString key;
        for (const auto &clause : filter.clauses)
        {
            key += QLatin1Char('(');
            for (const auto &c : clause)
                key += QStringLiteral("%1\x1f%2\x1f%3\x1f%4\x1f%5\x1e")
                           .arg(c.column)
                           .arg(static_cast<int>(c.op))
                           .arg(c.text.c_str())
                           .arg(c.numeric ? 1 : 0)
                           .arg(c.number, 0, 'g', 17);
            key += QLatin1Char(')');
        }
        return key;
    }

} // namespace

ParquetDocument::ParquetDocument(QObject *parent)
    : QObject(parent)
{
}

ParquetDocument::~ParquetDocument() = default;

bool ParquetDocument::open(const QString &filePath, QString *errorMessage)
{
    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("File does not exist: %1").arg(filePath);
        return false;
    }

    try
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QScopeGuard restoreOverrideCursor(QApplication::restoreOverrideCursor);

        table_ = ParquetReader::readTable(info.absoluteFilePath().toStdString());
    }
    catch (const std::exception &e)
    {
        if (errorMessage != nullptr)
            *errorMessage = QString::fromUtf8(e.what());
        return false;
    }

    filePath_ = info.absoluteFilePath();
    schema_.clear();
    for (const auto &field : table_->schema()->fields())
        schema_.push_back({QString::fromStdString(field->name()), displayType(*field->type())});
    cachedFilterKey_.clear();
    cachedRows_.clear();
    cacheValid_ = false;
    return true;
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
        names.push_back(column.name);
    return names;
}

bool ParquetDocument::matches(const RowCondition &condition, const arrow::Array &array, int64_t i)
{
    const bool isNull = array.IsNull(i);
    switch (condition.op)
    {
    case RowCondition::Op::IsNull:
        return isNull;
    case RowCondition::Op::IsNotNull:
        return !isNull;
    case RowCondition::Op::IsEmpty:
        return isNull || cellText(array, i).empty();
    case RowCondition::Op::IsNotEmpty:
        return !isNull && !cellText(array, i).empty();
    default:
        break;
    }
    if (isNull)
        return false;

    if (condition.numeric && isNumericArrowType(array.type_id()))
    {
        double value = 0.0;
        numericValue(array, i, &value);
        switch (condition.op)
        {
        case RowCondition::Op::Equals:
            return value == condition.number;
        case RowCondition::Op::NotEquals:
            return value != condition.number;
        case RowCondition::Op::Greater:
            return value > condition.number;
        case RowCondition::Op::Less:
            return value < condition.number;
        case RowCondition::Op::AtLeast:
            return value >= condition.number;
        case RowCondition::Op::AtMost:
            return value <= condition.number;
        default:
            break;
        }
    }

    const std::string text = cellText(array, i);
    switch (condition.op)
    {
    case RowCondition::Op::Equals:
    case RowCondition::Op::TextEquals:
        return text == condition.text;
    case RowCondition::Op::NotEquals:
        return text != condition.text;
    case RowCondition::Op::Greater:
        return text > condition.text;
    case RowCondition::Op::Less:
        return text < condition.text;
    case RowCondition::Op::AtLeast:
        return text >= condition.text;
    case RowCondition::Op::AtMost:
        return text <= condition.text;
    case RowCondition::Op::Contains:
        return text.find(condition.text) != std::string::npos;
    case RowCondition::Op::StartsWith:
        return text.starts_with(condition.text);
    case RowCondition::Op::EndsWith:
        return text.ends_with(condition.text);
    default:
        return false;
    }
}

bool ParquetDocument::applyFilter(const RowFilter &filter, QString *errorMessage)
{
    const QString key = filterKey(filter);
    if (cacheValid_ && key == cachedFilterKey_)
        return true;

    struct Term
    {
        ColumnCursor cursor;
        const RowCondition *condition;
    };
    QVector<QVector<Term>> resolved;
    for (const auto &clause : filter.clauses)
    {
        QVector<Term> terms;
        for (const auto &condition : clause)
        {
            if (condition.column.isEmpty())
            {
                for (int c = 0; c < table_->num_columns(); ++c)
                    terms.push_back({ColumnCursor{table_->column(c).get()}, &condition});
                continue;
            }
            const int index = table_->schema()->GetFieldIndex(condition.column.toStdString());
            if (index < 0)
            {
                if (errorMessage != nullptr)
                    *errorMessage = QStringLiteral("Unknown column: %1").arg(condition.column);
                return false;
            }
            terms.push_back({ColumnCursor{table_->column(index).get()}, &condition});
        }
        resolved.push_back(std::move(terms));
    }

    cachedRows_.clear();
    filtered_ = !resolved.isEmpty();
    if (filtered_)
    {
        const int64_t rowCount = table_->num_rows();
        for (int64_t row = 0; row < rowCount; ++row)
        {
            bool keep = true;
            for (auto &terms : resolved)
            {
                bool any = false;
                for (auto &term : terms)
                {
                    int64_t local = 0;
                    const arrow::Array *array = term.cursor.seek(row, &local);
                    if (array != nullptr && matches(*term.condition, *array, local))
                    {
                        any = true;
                        break;
                    }
                }
                if (!any)
                {
                    keep = false;
                    break;
                }
            }
            if (keep)
                cachedRows_.push_back(row);
        }
    }
    cachedFilterKey_ = key;
    cacheValid_ = true;
    return true;
}

bool ParquetDocument::rowCount(const RowFilter &filter, qsizetype *count, QString *errorMessage)
{
    if (!table_)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("No file is open.");
        return false;
    }
    if (!applyFilter(filter, errorMessage))
        return false;
    *count = filtered_ ? cachedRows_.size() : static_cast<qsizetype>(table_->num_rows());
    return true;
}

bool ParquetDocument::queryPage(const RowFilter &filter, qsizetype limit, qsizetype offset, QueryPage *page, QString *errorMessage)
{
    qsizetype total = 0;
    if (!rowCount(filter, &total, errorMessage))
        return false;

    const qsizetype first = std::clamp<qsizetype>(offset, 0, total);
    const qsizetype last = limit < 0 ? total : std::min(total, first + limit);

    QueryPage nextPage;
    nextPage.columns = columnNames();
    const int columnCount = table_->num_columns();
    QVector<ColumnCursor> cursors(columnCount);
    for (int c = 0; c < columnCount; ++c)
        cursors[c].column = table_->column(c).get();

    nextPage.rows.reserve(last - first);
    for (qsizetype i = first; i < last; ++i)
    {
        const int64_t row = filtered_ ? cachedRows_[i] : static_cast<int64_t>(i);
        QVector<CellString> values;
        values.reserve(columnCount);
        for (int c = 0; c < columnCount; ++c)
        {
            int64_t local = 0;
            const arrow::Array *array = cursors[c].seek(row, &local);
            values.push_back(array ? CellString(cellText(*array, local)) : CellString("NULL"));
        }
        nextPage.rows.push_back(std::move(values));
    }

    *page = std::move(nextPage);
    return true;
}
