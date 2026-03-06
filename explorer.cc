#include <QApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>
#include <memory>
#include <vector>

class FileHandler {
public:
  virtual ~FileHandler() = default;
  virtual bool read(const QString &filename) = 0;
  virtual bool write(const QString &filename) = 0;
  virtual QString getData() const = 0;
  virtual void setData(const QString &data) = 0;
  virtual QString getTypeName() const = 0;

protected:
  QString data_;
  QString path_;
};

class TextFileHandler final : public FileHandler {
public:
  bool read(const QString &filename) override {
    QFile f(filename);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
      return false;
    path_ = filename;
    data_ = QString::fromUtf8(f.readAll());
    return true;
  }
  bool write(const QString &filename) override {
    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
      return false;
    f.write(data_.toUtf8());
    return true;
  }
  QString getData() const override { return data_; }
  void setData(const QString &data) override { data_ = data; }
  QString getTypeName() const override { return "Text"; }
};

class JsonFileHandler final : public FileHandler {
public:
  bool read(const QString &filename) override {
    QFile f(filename);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
      return false;
    path_ = filename;
    data_ = QString::fromUtf8(f.readAll());
    QString trimmed = data_.trimmed();
    if (!trimmed.startsWith('{') && !trimmed.startsWith('['))
      data_ = "// WARNING: may not be valid JSON\n" + data_;
    return true;
  }
  bool write(const QString &filename) override {
    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
      return false;
    f.write(data_.toUtf8());
    return true;
  }
  QString getData() const override { return data_; }
  void setData(const QString &data) override { data_ = data; }
  QString getTypeName() const override { return "JSON"; }
};

class XmlFileHandler final : public FileHandler {
public:
  bool read(const QString &filename) override {
    QFile f(filename);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
      return false;
    path_ = filename;
    data_ = QString::fromUtf8(f.readAll());
    QString trimmed = data_.trimmed();
    if (!trimmed.startsWith('<'))
      data_ = "<!-- WARNING: may not be valid XML -->\n" + data_;
    return true;
  }
  bool write(const QString &filename) override {
    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
      return false;
    f.write(data_.toUtf8());
    return true;
  }
  QString getData() const override { return data_; }
  void setData(const QString &data) override { data_ = data; }
  QString getTypeName() const override { return "XML"; }
};

class BinaryFileHandler final : public FileHandler {
public:
  bool read(const QString &filename) override {
    QFile f(filename);
    if (!f.open(QIODevice::ReadOnly))
      return false;
    path_ = filename;
    raw_ = f.readAll();
    data_.clear();
    for (int i = 0; i < raw_.size(); ++i) {
      if (i > 0 && i % 16 == 0)
        data_ += '\n';
      else if (i > 0)
        data_ += ' ';
      data_ += QString("%1")
                   .arg((unsigned char)raw_[i], 2, 16, QChar('0'))
                   .toUpper();
    }
    return true;
  }
  bool write(const QString &filename) override {
    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly))
      return false;
    QByteArray out;
    QStringList tokens = data_.simplified().split(' ');
    for (auto &t : tokens) {
      bool ok;
      unsigned char byte = t.toUInt(&ok, 16);
      if (ok)
        out.append(byte);
    }
    f.write(out);
    return true;
  }
  QString getData() const override { return data_; }
  void setData(const QString &data) override { data_ = data; }
  QString getTypeName() const override { return "Binary"; }

private:
  QByteArray raw_;
};

class FileHandlerFactory {
public:
  static std::unique_ptr<FileHandler> createHandler(const QString &filename) {
    QString ext = QFileInfo(filename).suffix().toLower();
    if (ext == "json")
      return std::make_unique<JsonFileHandler>();
    if (ext == "xml" || ext == "html")
      return std::make_unique<XmlFileHandler>();
    if (ext == "dat" || ext == "bin")
      return std::make_unique<BinaryFileHandler>();
    return std::make_unique<TextFileHandler>();
  }
};

static QString simpleDiff(const QString &oldText, const QString &newText) {
  QStringList oldLines = oldText.split('\n');
  QStringList newLines = newText.split('\n');
  QString result;
  int maxLines = std::max(oldLines.size(), newLines.size());
  for (int i = 0; i < maxLines; ++i) {
    QString ol = i < oldLines.size() ? oldLines[i] : QString();
    QString nl = i < newLines.size() ? newLines[i] : QString();
    if (ol != nl) {
      if (i < oldLines.size())
        result += QString("- L%1: %2\n").arg(i + 1).arg(ol);
      if (i < newLines.size())
        result += QString("+ L%1: %2\n").arg(i + 1).arg(nl);
    }
  }
  if (result.isEmpty())
    result = "(no changes)";
  return result;
}


enum class OperationType { Open, Modify, Save };

static QString opTypeName(OperationType t) {
  switch (t) {
  case OperationType::Open:
    return "open";
  case OperationType::Modify:
    return "modif";
  case OperationType::Save:
    return "save";
  }
  return "fignya";
}

struct Operation {
  OperationType type;
  QString timestamp;
  std::shared_ptr<FileHandler> handler;
  QString dataBefore; // снимок данных ДО операции — для отката
  QString filePath;
};

struct UndoResult {
  bool success = false;
  QString info;
  std::shared_ptr<FileHandler> handler;
};

class OperationHistory {
public:
  void record(OperationType type, const std::shared_ptr<FileHandler> &handler,
              const QString &path, const QString &dataBefore) {
    Operation op;
    op.type = type;
    op.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    op.handler = handler;
    op.dataBefore = dataBefore;
    op.filePath = path;
    ops_.push_back(std::move(op));
  }

  bool canUndo() const { return !ops_.empty(); }

  UndoResult undo() {
    UndoResult result;
    if (ops_.empty())
      return result;

    Operation op = std::move(ops_.back());
    ops_.pop_back();

    if (op.handler) {
      op.handler->setData(op.dataBefore);
    }

    result.success = true;
    result.handler = op.handler;
    result.info =
        QString("%1 on \"%2\"\n"
                "\n%3\n"
                "%4")
            .arg(opTypeName(op.type))
            .arg(QFileInfo(op.filePath).fileName())
            .arg(simpleDiff(op.dataBefore,
                            op.handler ? op.handler->getData() : QString()))
            .arg(op.handler ? QString::number(op.handler.use_count())
                            : QStringLiteral("???"));
    return result;
  }


  void saveToFile(const QString &filename) const {
    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
      return;
    QTextStream out(&f);
    for (const auto &op : ops_) {
      out << opTypeName(op.type) << "|" << op.timestamp << "|" << op.filePath
          << "|" << QString(op.dataBefore.toUtf8().toBase64()) << "\n";
    }
  }

  bool loadFromFile(const QString &filename) {
    QFile f(filename);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
      return false;
    QTextStream in(&f);
    loadedLog_.clear();
    while (!in.atEnd()) {
      QString line = in.readLine().trimmed();
      if (line.isEmpty())
        continue;
      QStringList parts = line.split('|');
      if (parts.size() >= 3) {
        loadedLog_ +=
            QString("[%1] %2: %3\n")
                .arg(parts[1], parts[0], QFileInfo(parts[2]).fileName());
      }
    }
    return true;
  }


  QString formatLog() const {
    QString result;
    if (!loadedLog_.isEmpty())
      result += "loaded\n" + loadedLog_ + "\n";

    result +=
        "current session (" + QString::number(ops_.size()) + " ops)\n";
    for (size_t i = 0; i < ops_.size(); ++i) {
      const auto &op = ops_[i];
      result += QString("%1 [%2] %3: %4 (handler use_count=%5)\n")
                    .arg(i + 1)
                    .arg(op.timestamp)
                    .arg(opTypeName(op.type))
                    .arg(QFileInfo(op.filePath).fileName())
                    .arg(op.handler ? QString::number(op.handler.use_count())
                                    : QStringLiteral("0"));
    }
    if (ops_.empty() && loadedLog_.isEmpty())
      result += "(no operations)\n";
    return result;
  }

  QString formatForPath(const QString &path) const {
    QString result;
    for (size_t i = 0; i < ops_.size(); ++i) {
      if (ops_[i].filePath != path)
        continue;
      const auto &op = ops_[i];
      result += QString("%1 [%2] %3 (refs=%4)\n")
                    .arg(i + 1)
                    .arg(op.timestamp)
                    .arg(opTypeName(op.type))
                    .arg(op.handler ? QString::number(op.handler.use_count())
                                    : QStringLiteral("0"));
    }
    if (result.isEmpty())
      result = "empty\n";
    return result;
  }

  size_t size() const { return ops_.size(); }

private:
  std::vector<Operation> ops_;
  QString loadedLog_;
};


class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  MainWindow() {
    setWindowTitle("File Explorer");
    resize(900, 600);

    auto *central = new QWidget;
    setCentralWidget(central);
    auto *mainLayout = new QHBoxLayout(central);

    auto *leftLayout = new QVBoxLayout;
    auto *btnOpen = new QPushButton("Open File");
    auto *btnSave = new QPushButton("Save");
    auto *btnSaveAll = new QPushButton("Save All");
    auto *btnUndo = new QPushButton("Undo");
    auto *btnSaveHist = new QPushButton("Save History");
    auto *btnLoadHist = new QPushButton("Load History");
    list_ = new QListWidget;
    leftLayout->addWidget(btnOpen);
    leftLayout->addWidget(btnSave);
    leftLayout->addWidget(btnSaveAll);
    leftLayout->addWidget(btnUndo);
    leftLayout->addWidget(btnSaveHist);
    leftLayout->addWidget(btnLoadHist);
    leftLayout->addWidget(list_);
    mainLayout->addLayout(leftLayout, 1);

    auto *splitter = new QSplitter(Qt::Vertical);
    auto *topRight = new QWidget;
    auto *topLayout = new QVBoxLayout(topRight);
    topLayout->setContentsMargins(0, 0, 0, 0);
    info_ = new QLabel("No file loaded");
    edit_ = new QTextEdit;
    topLayout->addWidget(info_);
    topLayout->addWidget(edit_);
    splitter->addWidget(topRight);

    auto *bottomRight = new QWidget;
    auto *bottomLayout = new QVBoxLayout(bottomRight);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->addWidget(new QLabel("history"));
    history_ = new QTextEdit;
    history_->setReadOnly(true);
    bottomLayout->addWidget(history_);
    splitter->addWidget(bottomRight);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(splitter, 3);

    connect(btnOpen, &QPushButton::clicked, this, &MainWindow::openFile);
    connect(btnSave, &QPushButton::clicked, this, &MainWindow::saveFile);
    connect(btnSaveAll, &QPushButton::clicked, this, &MainWindow::saveAll);
    connect(btnUndo, &QPushButton::clicked, this, &MainWindow::undoLast);
    connect(btnSaveHist, &QPushButton::clicked, this, &MainWindow::saveHistory);
    connect(btnLoadHist, &QPushButton::clicked, this, &MainWindow::loadHistory);
    connect(list_, &QListWidget::currentRowChanged, this,
            &MainWindow::selectFile);
    connect(edit_, &QTextEdit::textChanged, this, &MainWindow::textChanged);
  }

private:
  int findHandlerIndex(const std::shared_ptr<FileHandler> &h) const {
    for (size_t i = 0; i < handlers_.size(); ++i)
      if (handlers_[i] == h)
        return (int)i;
    return -1;
  }

  void commitModification(int row) {
    if (row < 0 || row >= (int)handlers_.size())
      return;
    QString current = handlers_[row]->getData();
    if (current != snapshots_[row]) {
      opHistory_.record(OperationType::Modify, handlers_[row], paths_[row],
                        snapshots_[row]);
      snapshots_[row] = current;
    }
  }

  void updateHistoryDisplay() {
    if (currentRow_ >= 0 && currentRow_ < (int)paths_.size()) {
      history_->setPlainText("file\n" +
                             opHistory_.formatForPath(paths_[currentRow_]) +
                             "\noperations\n" +
                             opHistory_.formatLog());
    } else {
      history_->setPlainText(opHistory_.formatLog());
    }
  }

private slots:
  void openFile() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open", QString(),
        "All Files (*.*);;Text (*.txt);;JSON (*.json);;XML "
        "(*.xml);;Binary (*.dat *.bin)");
    if (path.isEmpty())
      return;

    std::shared_ptr<FileHandler> handler(
        FileHandlerFactory::createHandler(path));
    if (!handler->read(path)) {
      QMessageBox::warning(this, "Error", "Cannot read file");
      return;
    }

    opHistory_.record(OperationType::Open, handler, path, QString());

    handlers_.push_back(handler);
    paths_.push_back(path);
    snapshots_.push_back(handler->getData());

    list_->addItem(QFileInfo(path).fileName());
    list_->setCurrentRow(list_->count() - 1);
  }

  void selectFile(int row) {
    if (currentRow_ >= 0 && currentRow_ < (int)handlers_.size()) {
      handlers_[currentRow_]->setData(edit_->toPlainText());
      commitModification(currentRow_);
    }

    currentRow_ = row;
    if (row < 0 || row >= (int)handlers_.size()) {
      edit_->clear();
      history_->clear();
      info_->setText("No file loaded");
      return;
    }

    blockEdit_ = true;
    edit_->setPlainText(handlers_[row]->getData());
    blockEdit_ = false;

    QFileInfo fi(paths_[row]);
    info_->setText(
        QString("Type: %1 | Size: %2 B | Path: %3 | shared_ptr refs: %4")
            .arg(handlers_[row]->getTypeName())
            .arg(fi.size())
            .arg(fi.filePath())
            .arg(handlers_[row].use_count()));

    updateHistoryDisplay();
  }

  void textChanged() {
    if (blockEdit_)
      return;
    if (currentRow_ < 0 || currentRow_ >= (int)handlers_.size())
      return;
    handlers_[currentRow_]->setData(edit_->toPlainText());
  }

  void saveFile() {
    if (currentRow_ < 0 || currentRow_ >= (int)handlers_.size())
      return;

    handlers_[currentRow_]->setData(edit_->toPlainText());
    commitModification(currentRow_);

    QString current = handlers_[currentRow_]->getData();
    opHistory_.record(OperationType::Save, handlers_[currentRow_],
                      paths_[currentRow_], current);

    if (!handlers_[currentRow_]->write(paths_[currentRow_]))
      QMessageBox::warning(this, "Error", "Cannot write file");

    updateHistoryDisplay();
  }

  void saveAll() {
    if (currentRow_ >= 0 && currentRow_ < (int)handlers_.size())
      handlers_[currentRow_]->setData(edit_->toPlainText());

    for (size_t i = 0; i < handlers_.size(); ++i) {
      commitModification((int)i);
      QString current = handlers_[i]->getData();
      opHistory_.record(OperationType::Save, handlers_[i], paths_[i], current);
      if (!handlers_[i]->write(paths_[i]))
        QMessageBox::warning(this, "Error",
                             QString("Cannot write %1").arg(paths_[i]));
    }

    updateHistoryDisplay();
  }

  void undoLast() {
    if (!opHistory_.canUndo()) {
      QMessageBox::information(this, "Undo", "Nothing to undo");
      return;
    }

    UndoResult r = opHistory_.undo();
    if (!r.success)
      return;

    int idx = findHandlerIndex(r.handler);
    if (idx >= 0) {
      snapshots_[idx] = handlers_[idx]->getData();
      if (idx == currentRow_) {
        blockEdit_ = true;
        edit_->setPlainText(handlers_[idx]->getData());
        blockEdit_ = false;
      }
    }

    updateHistoryDisplay();
    QMessageBox::information(this, "Undo", r.info);
  }

  void saveHistory() {
    QString path = QFileDialog::getSaveFileName(
        this, "Save History", "history.log", "Log (*.log);;All Files (*.*)");
    if (path.isEmpty())
      return;
    opHistory_.saveToFile(path);
    QMessageBox::information(this, "History", "Operation history saved");
  }

  void loadHistory() {
    QString path = QFileDialog::getOpenFileName(this, "Load History", QString(),
                                                "Log (*.log);;All Files (*.*)");
    if (path.isEmpty())
      return;
    if (opHistory_.loadFromFile(path)) {
      updateHistoryDisplay();
      QMessageBox::information(this, "History",
                               "Previous session history loaded");
    } else {
      QMessageBox::warning(this, "Error", "Cannot read history file");
    }
  }

private:
  QListWidget *list_;
  QTextEdit *edit_;
  QTextEdit *history_;
  QLabel *info_;

  std::vector<std::shared_ptr<FileHandler>> handlers_;
  QStringList paths_;
  QStringList snapshots_;
  int currentRow_ = -1;
  bool blockEdit_ = false;

  OperationHistory opHistory_;
};

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  MainWindow w;
  w.show();
  return app.exec();
}

#include "explorer.moc"