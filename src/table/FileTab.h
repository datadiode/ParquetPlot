// Copyright (C) 2026 Furkan Selim Cetin
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "FilterCondition.h"
#include "ParquetDocument.h"

#include <QHash>
#include <QWidget>

#include <memory>

class FilterBuilderWidget;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableView;
class ParquetTableModel;
class QComboBox;

class FileTab final : public QWidget
{
    Q_OBJECT

public:
    explicit FileTab(QString filePath, QWidget *parent = nullptr);

    bool load(QString *errorMessage);
    QString displayName() const;
    QString filePath() const;
    bool showFromRow(qsizetype row);

private:
    bool reload();
    void previousPage();
    void nextPage();
    void updatePageControls(qsizetype filteredRows);
    RowFilter buildFilter(QString *errorMessage) const;
    bool conditionToFilter(const FilterCondition &condition, RowCondition *out, QString *errorMessage) const;
    QVector<RowCondition> searchToFilter(QString *errorMessage) const;
    QString columnType(const QString &column) const;

    QString filePath_;
    std::unique_ptr<ParquetDocument> document_;
    QHash<QString, QString> columnTypes_;

    ParquetTableModel *model_ = nullptr;
    FilterBuilderWidget *filterBuilder_ = nullptr;
    QTableView *table_ = nullptr;
    QLineEdit *searchBox_ = nullptr;
    QComboBox *searchMode_ = nullptr;
    QSpinBox *pageSize_ = nullptr;
    QPushButton *previousButton_ = nullptr;
    QPushButton *nextButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *pageLabel_ = nullptr;

    qsizetype offset_ = 0;
    qsizetype currentPageRows_ = 0;
};
