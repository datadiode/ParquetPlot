// Copyright (C) 2026 Furkan Selim Cetin
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FileTab.h"

#include "FilterBuilderWidget.h"
#include "ParquetTableModel.h"

#include <QComboBox>
#include <QAbstractItemView>
#include <QApplication>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QSpinBox>
#include <QStyle>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

namespace
{

    struct QuickOperator
    {
        QString token;
        QString filterOperator;
    };

    bool isNumericType(const QString &type)
    {
        const QString upper = type.toUpper();
        return upper.contains(QStringLiteral("INT")) || upper == QStringLiteral("FLOAT") || upper == QStringLiteral("DOUBLE") || upper == QStringLiteral("REAL") || upper.startsWith(QStringLiteral("DECIMAL"));
    }

    bool isBooleanType(const QString &type)
    {
        return type.compare(QStringLiteral("BOOLEAN"), Qt::CaseInsensitive) == 0 || type.compare(QStringLiteral("BOOL"), Qt::CaseInsensitive) == 0;
    }

    bool parseBoolean(const QString &value, double *number)
    {
        const QString normalized = value.trimmed().toLower();
        if (normalized == QStringLiteral("true") || normalized == QStringLiteral("1") || normalized == QStringLiteral("yes"))
        {
            *number = 1.0;
            return true;
        }
        if (normalized == QStringLiteral("false") || normalized == QStringLiteral("0") || normalized == QStringLiteral("no"))
        {
            *number = 0.0;
            return true;
        }
        return false;
    }

    QString unquoteQuickValue(QString value)
    {
        value = value.trimmed();
        if (value.size() >= 2)
        {
            const QChar first = value.front();
            const QChar last = value.back();
            if ((first == '\'' && last == '\'') || (first == '"' && last == '"'))
            {
                value = value.sliced(1, value.size() - 2);
            }
        }
        return value;
    }

    QVector<QuickOperator> quickOperators()
    {
        return {
            {QStringLiteral("!="), QStringLiteral("does not equal")},
            {QStringLiteral("<>"), QStringLiteral("does not equal")},
            {QStringLiteral(">="), QStringLiteral("at least")},
            {QStringLiteral("<="), QStringLiteral("at most")},
            {QStringLiteral("="), QStringLiteral("equals")},
            {QStringLiteral(">"), QStringLiteral("greater than")},
            {QStringLiteral("<"), QStringLiteral("less than")},
        };
    }

    bool tryParseQuickFilter(const QString &text, const QStringList &columns, FilterCondition *condition)
    {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty())
        {
            return false;
        }

        QStringList sortedColumns = columns;
        std::sort(sortedColumns.begin(), sortedColumns.end(), [](const QString &left, const QString &right)
                  { return left.size() > right.size(); });

        for (const QString &column : sortedColumns)
        {
            if (!trimmed.startsWith(column, Qt::CaseInsensitive))
            {
                continue;
            }

            qsizetype cursor = column.size();
            while (cursor < trimmed.size() && trimmed.at(cursor).isSpace())
            {
                ++cursor;
            }

            const QString remaining = trimmed.sliced(cursor);
            for (const auto &op : quickOperators())
            {
                if (!remaining.startsWith(op.token))
                {
                    continue;
                }

                const QString value = unquoteQuickValue(remaining.sliced(op.token.size()));
                condition->column = column;
                condition->op = op.filterOperator;
                condition->value = value;
                return true;
            }
        }

        return false;
    }

} // namespace

FileTab::FileTab(QString filePath, QWidget *parent)
    : QWidget(parent), filePath_(std::move(filePath)), document_(std::make_unique<ParquetDocument>())
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto *toolbar = new QToolBar(this);
    toolbar->setIconSize({18, 18});

    previousButton_ = new QPushButton(this);
    previousButton_->setObjectName("previousPage");
    previousButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    previousButton_->setToolTip(tr("Previous page"));
    connect(previousButton_, &QPushButton::clicked, this, &FileTab::previousPage);
    toolbar->addWidget(previousButton_);

    nextButton_ = new QPushButton(this);
    nextButton_->setObjectName("nextPage");
    nextButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    nextButton_->setToolTip(tr("Next page"));
    connect(nextButton_, &QPushButton::clicked, this, &FileTab::nextPage);
    toolbar->addWidget(nextButton_);

    auto *refreshButton = new QPushButton(this);
    refreshButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    refreshButton->setToolTip(tr("Reload"));
    connect(refreshButton, &QPushButton::clicked, this, &FileTab::reload);
    toolbar->addWidget(refreshButton);

    pageLabel_ = new QLabel(this);
    toolbar->addWidget(pageLabel_);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr("Rows"), this));

    pageSize_ = new QSpinBox(this);
    pageSize_->setObjectName("tablePageSize");
    pageSize_->setRange(-1, 100000);
    pageSize_->setSingleStep(500);
    pageSize_->setSpecialValueText(tr("All (-1)"));
    pageSize_->setToolTip(tr("Rows per page. Type -1 to show every matching row."));
    pageSize_->setValue(1000);
    connect(pageSize_, &QSpinBox::valueChanged, this, [this]()
            {
        if (pageSize_->value() == 0) {
            pageSize_->setValue(1);
            return;
        }
        offset_ = 0;
        reload(); });
    toolbar->addWidget(pageSize_);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr("Find"), this));

    searchBox_ = new QLineEdit(this);
    searchBox_->setObjectName("tableSearch");
    searchBox_->setPlaceholderText(tr("value or target = 1"));
    searchBox_->setMinimumWidth(260);
    connect(searchBox_, &QLineEdit::returnPressed, this, [this]()
            {
        offset_ = 0;
        reload(); });
    toolbar->addWidget(searchBox_);

    searchMode_ = new QComboBox(this);
    searchMode_->setObjectName("tableSearchMode");
    searchMode_->addItems({tr("contains"), tr("exact")});
    connect(searchMode_, &QComboBox::currentTextChanged, this, [this]()
            {
        offset_ = 0;
        reload(); });
    toolbar->addWidget(searchMode_);

    root->addWidget(toolbar);

    filterBuilder_ = new FilterBuilderWidget(this);
    connect(filterBuilder_, &FilterBuilderWidget::filtersApplied, this, [this]()
            {
        offset_ = 0;
        reload(); });
    root->addWidget(filterBuilder_);

    model_ = new ParquetTableModel(this);
    table_ = new QTableView(this);
    table_->setObjectName("parquetTable");
    table_->setModel(model_);
    table_->setAlternatingRowColors(true);
    table_->setSortingEnabled(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->horizontalHeader()->setSectionsMovable(true);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->verticalHeader()->setDefaultSectionSize(24);
    root->addWidget(table_, 1);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName("tableStatus");
    statusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(statusLabel_);
}

bool FileTab::load(QString *errorMessage)
{
    if (!document_->open(filePath_, errorMessage))
    {
        return false;
    }

    columnTypes_.clear();
    for (const auto &column : document_->schema())
    {
        columnTypes_.insert(column.name, column.type);
    }
    filterBuilder_->setColumns(document_->columnNames());
    if (!reload())
    {
        *errorMessage = statusLabel_->text();
        return false;
    }
    return true;
}

QString FileTab::displayName() const
{
    return QFileInfo(filePath_).fileName();
}

QString FileTab::filePath() const
{
    return filePath_;
}

bool FileTab::showFromRow(qsizetype row)
{
    // A plot position refers to an original source row, independent of table filters.
    searchBox_->clear();
    filterBuilder_->clear();
    offset_ = std::max<qsizetype>(0, row);
    if (!reload())
        return false;
    const int visibleRow = pageSize_->value() < 0
        ? static_cast<int>(std::min<qsizetype>(offset_, model_->rowCount() - 1)) : 0;
    if (model_->rowCount() > 0)
    {
        table_->setCurrentIndex(model_->index(visibleRow, 0));
        table_->scrollTo(model_->index(visibleRow, 0), QAbstractItemView::PositionAtTop);
    }
    return true;
}

bool FileTab::reload()
{
    QString error;
    const RowFilter filter = buildFilter(&error);
    if (!error.isEmpty())
    {
        statusLabel_->setText(error);
        return false;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QScopeGuard restoreOverrideCursor(QApplication::restoreOverrideCursor);

    qsizetype filteredRows = 0;
    if (!document_->rowCount(filter, &filteredRows, &error))
    {
        statusLabel_->setText(error);
        return false;
    }

    offset_ = std::clamp<qsizetype>(offset_, 0, std::max<qsizetype>(0, filteredRows - 1));
    QueryPage page;
    const qsizetype limit = pageSize_->value();
    if (!document_->queryPage(filter, limit, limit < 0 ? 0 : offset_, &page, &error))
    {
        statusLabel_->setText(error);
        return false;
    }

    currentPageRows_ = page.rows.size();
    model_->setResult(std::move(page.columns), std::move(page.rows), limit < 0 ? 0 : offset_);
    //table_->resizeColumnsToContents(); // too expensive
    updatePageControls(filteredRows);
    return true;
}

void FileTab::previousPage()
{
    if (pageSize_->value() < 0)
    {
        return;
    }

    offset_ = std::max<qsizetype>(0, offset_ - pageSize_->value());
    reload();
}

void FileTab::nextPage()
{
    if (pageSize_->value() < 0)
    {
        return;
    }

    offset_ += pageSize_->value();
    reload();
}

void FileTab::updatePageControls(qsizetype filteredRows)
{
    if (pageSize_->value() < 0)
    {
        pageLabel_->setText(tr("All %1").arg(filteredRows));
        previousButton_->setEnabled(false);
        nextButton_->setEnabled(false);
        statusLabel_->setText(tr("%1 columns. Showing all %2 rows from %3.")
                                  .arg(document_->columnNames().size())
                                  .arg(currentPageRows_)
                                  .arg(filePath_));
        return;
    }

    const qsizetype firstRow = filteredRows == 0 ? 0 : offset_ + 1;
    const qsizetype lastRow = filteredRows == 0 ? 0 : std::min(offset_ + currentPageRows_, filteredRows);

    pageLabel_->setText(tr("%1-%2 of %3").arg(firstRow).arg(lastRow).arg(filteredRows));
    previousButton_->setEnabled(offset_ > 0);
    nextButton_->setEnabled(offset_ + currentPageRows_ < filteredRows);

    statusLabel_->setText(tr("%1 columns. Showing %2 rows from %3.")
                              .arg(document_->columnNames().size())
                              .arg(currentPageRows_)
                              .arg(filePath_));
}

RowFilter FileTab::buildFilter(QString *errorMessage) const
{
    RowFilter filter;

    const QVector<RowCondition> search = searchToFilter(errorMessage);
    if (!errorMessage->isEmpty())
    {
        return {};
    }
    if (!search.isEmpty())
    {
        filter.clauses.push_back(search);
    }

    for (const auto &condition : filterBuilder_->conditions())
    {
        RowCondition rowCondition;
        if (!conditionToFilter(condition, &rowCondition, errorMessage))
        {
            return {};
        }
        filter.clauses.push_back({rowCondition});
    }

    return filter;
}

bool FileTab::conditionToFilter(const FilterCondition &condition, RowCondition *out, QString *errorMessage) const
{
    const QString type = columnType(condition.column);
    const QString op = condition.op;
    out->column = condition.column;

    if (op == QStringLiteral("is true") || op == QStringLiteral("is false"))
    {
        out->op = RowCondition::Op::Equals;
        out->numeric = true;
        out->number = op == QStringLiteral("is true") ? 1.0 : 0.0;
        out->text = op == QStringLiteral("is true") ? "true" : "false";
        return true;
    }
    if (op == QStringLiteral("is empty"))
    {
        out->op = RowCondition::Op::IsEmpty;
        return true;
    }
    if (op == QStringLiteral("is not empty"))
    {
        out->op = RowCondition::Op::IsNotEmpty;
        return true;
    }

    if (condition.value.trimmed().isEmpty())
    {
        *errorMessage = tr("A filter value is empty.");
        return false;
    }

    if (op == QStringLiteral("contains text") || op == QStringLiteral("starts with") || op == QStringLiteral("ends with"))
    {
        out->op = op == QStringLiteral("contains text") ? RowCondition::Op::Contains
                  : op == QStringLiteral("starts with") ? RowCondition::Op::StartsWith
                                                         : RowCondition::Op::EndsWith;
        out->text = condition.value.toStdString();
        return true;
    }

    static const QHash<QString, RowCondition::Op> comparisons = {
        {QStringLiteral("equals"), RowCondition::Op::Equals},
        {QStringLiteral("does not equal"), RowCondition::Op::NotEquals},
        {QStringLiteral("greater than"), RowCondition::Op::Greater},
        {QStringLiteral("less than"), RowCondition::Op::Less},
        {QStringLiteral("at least"), RowCondition::Op::AtLeast},
        {QStringLiteral("at most"), RowCondition::Op::AtMost},
    };
    if (!comparisons.contains(op))
    {
        *errorMessage = tr("Unknown filter operator: %1").arg(op);
        return false;
    }
    out->op = comparisons.value(op);

    const QString trimmed = condition.value.trimmed();
    if (trimmed.compare(QStringLiteral("null"), Qt::CaseInsensitive) == 0)
    {
        if (out->op == RowCondition::Op::Equals)
        {
            out->op = RowCondition::Op::IsNull;
            return true;
        }
        if (out->op == RowCondition::Op::NotEquals)
        {
            out->op = RowCondition::Op::IsNotNull;
            return true;
        }
        *errorMessage = tr("Only equals / does not equal can be used with NULL.");
        return false;
    }

    out->text = condition.value.toStdString();
    if (isBooleanType(type))
    {
        if (!parseBoolean(trimmed, &out->number))
        {
            *errorMessage = tr("Expected a boolean value like true, false, 1, or 0.");
            return false;
        }
        out->numeric = true;
    }
    else if (isNumericType(type))
    {
        bool ok = false;
        out->number = trimmed.toDouble(&ok);
        if (!ok)
        {
            *errorMessage = tr("Expected a numeric value.");
            return false;
        }
        out->numeric = true;
    }
    return true;
}

QVector<RowCondition> FileTab::searchToFilter(QString *errorMessage) const
{
    const QString value = searchBox_->text().trimmed();
    if (value.isEmpty())
    {
        return {};
    }

    FilterCondition quickFilter;
    if (tryParseQuickFilter(value, document_->columnNames(), &quickFilter))
    {
        RowCondition condition;
        if (!conditionToFilter(quickFilter, &condition, errorMessage))
            return {};
        return {condition};
    }

    // An empty column name matches if any column satisfies the condition.
    RowCondition condition;
    condition.op = searchMode_->currentText() == tr("exact") ? RowCondition::Op::TextEquals
                                                              : RowCondition::Op::Contains;
    condition.text = value.toStdString();
    return {condition};
}

QString FileTab::columnType(const QString &column) const
{
    return columnTypes_.value(column);
}
