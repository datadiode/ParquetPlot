// Copyright (C) 2026 Furkan Selim Cetin
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVector>

struct FilterCondition
{
    QString column;
    QString op;
    QString value;
};

// A single predicate evaluated by ParquetDocument against one cell.
struct RowCondition
{
    enum class Op
    {
        Equals,
        NotEquals,
        Greater,
        Less,
        AtLeast,
        AtMost,
        TextEquals,
        Contains,
        StartsWith,
        EndsWith,
        IsNull,
        IsNotNull,
        IsEmpty,
        IsNotEmpty,
    };

    QString column; // empty: matches if any column satisfies the predicate
    Op op = Op::Equals;
    QString text;         // comparison text (case-insensitive for Contains/StartsWith/EndsWith)
    bool numeric = false; // compare numerically (numeric and boolean columns)
    double number = 0.0;
};

// Conjunction of disjunctions: every inner list needs at least one match.
struct RowFilter
{
    QVector<QVector<RowCondition>> clauses;
};
