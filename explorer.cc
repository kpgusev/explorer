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
    if (!trimmed.startsWith('{') && !trimmed.startsWith('[')) {
      data_ = "// WARNING: may not be valid JSON\n" + data_;
    }
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
    if (!trimmed.startsWith('<')) {
      data_ = "<!-- WARNING: may not be valid XML -->\n" + data_;
    }
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
    list_ = new QListWidget;
    leftLayout->addWidget(btnOpen);
    leftLayout->addWidget(btnSave);
    leftLayout->addWidget(btnSaveAll);
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
    bottomLayout->addWidget(new QLabel("History:"));
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
    connect(list_, &QListWidget::currentRowChanged, this,
            &MainWindow::selectFile);
    connect(edit_, &QTextEdit::textChanged, this, &MainWindow::textChanged);
  }

private slots:
  void openFile() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open", QString(),
        "All Files (*.*);;Text (*.txt);;JSON (*.json);;XML (*.xml);;Binary "
        "(*.dat *.bin)");
    if (path.isEmpty())
      return;

    auto handler = FileHandlerFactory::createHandler(path);
    if (!handler->read(path)) {
      QMessageBox::warning(this, "Error", "Cannot read file");
      return;
    }
    handlers_.push_back(std::move(handler));
    paths_.push_back(path);
    snapshots_.push_back(handlers_.back()->getData());
    historyLogs_.push_back(
        QString("[%1] Opened file\n")
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));

    list_->addItem(QFileInfo(path).fileName());
    list_->setCurrentRow(list_->count() - 1);
  }

  void selectFile(int row) {
    if (currentRow_ >= 0 && currentRow_ < (int)handlers_.size()) {
      handlers_[currentRow_]->setData(edit_->toPlainText());
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
    history_->setPlainText(historyLogs_[row]);
    QFileInfo fi(paths_[row]);
    info_->setText(QString("Type: %1 | Size: %2 bytes | Path: %3")
                       .arg(handlers_[row]->getTypeName())
                       .arg(fi.size())
                       .arg(fi.filePath()));
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
    QString current = handlers_[currentRow_]->getData();
    QString diff = simpleDiff(snapshots_[currentRow_], current);
    historyLogs_[currentRow_] +=
        QString("[%1] Saved. Diff:\n%2\n")
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
            .arg(diff);
    snapshots_[currentRow_] = current;
    history_->setPlainText(historyLogs_[currentRow_]);

    if (!handlers_[currentRow_]->write(paths_[currentRow_])) {
      QMessageBox::warning(this, "Error", "Cannot write file");
    }
  }

  void saveAll() {
    for (size_t i = 0; i < handlers_.size(); ++i) {
      QString current = handlers_[i]->getData();
      QString diff = simpleDiff(snapshots_[i], current);
      historyLogs_[i] +=
          QString("[%1] Saved (batch). Diff:\n%2\n")
              .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
              .arg(diff);
      snapshots_[i] = current;
      if (!handlers_[i]->write(paths_[i])) {
        QMessageBox::warning(this, "Error",
                             QString("Cannot write %1").arg(paths_[i]));
      }
    }
    if (currentRow_ >= 0 && currentRow_ < (int)historyLogs_.size())
      history_->setPlainText(historyLogs_[currentRow_]);
  }

private:
  QListWidget *list_;
  QTextEdit *edit_;
  QTextEdit *history_;
  QLabel *info_;
  std::vector<std::unique_ptr<FileHandler>> handlers_;
  QStringList paths_;
  QStringList snapshots_;
  QStringList historyLogs_;
  int currentRow_ = -1;
  bool blockEdit_ = false;
};

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  MainWindow w;
  w.show();
  return app.exec();
}

#include "explorer.moc"
