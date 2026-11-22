# Third-party software

ParquetPlot release packages include dynamically linked runtime libraries and
Qt plugins so the application can run without development packages installed.

- [Qt 6](https://www.qt.io/) — LGPL-3.0-only, GPL-2.0-only, or GPL-3.0-only,
  with separately licensed third-party components. The packages use dynamic
  linking. Corresponding Qt source is available from
  <https://code.qt.io/cgit/qt/qtbase.git/>.
- [Apache Arrow and Apache Parquet](https://arrow.apache.org/) — Apache-2.0.
- [QCustomPlot 2.1.1](https://www.qcustomplot.com/) — GPL-3.0-or-later. Its
  checksum-verified source archive is compiled directly into ParquetPlot.
- The integrated table browser in `src/table` is adapted from ParquetViewer,
  Copyright (C) 2026 Furkan Selim Cetin, GPL-3.0-or-later. Its original copyright
  and license identifiers are retained in the source files.
