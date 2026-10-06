#pragma once

#include <set>
#include <vector>

#include <QDialog>
#include <QList>
#include <QPlainTextEdit>
#include <QString>
#include <QStringList>

#include "core/assembler.h"

class Emulator;
class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QTabBar;
class QTreeWidget;

// The editor of an assembler source: instructions, strings, numbers and
// comments in colours, and a grey margin on the left where a click sets or
// clears a breakpoint on the line. Lines are counted from 1.
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit CodeEditor(QWidget* parent = nullptr);

    // Breakpoints go with their lines when text is put in or taken out
    // above them.
    QList<int> breakpoints() const;
    void toggleBreakpoint(int line);
    void clearBreakpoints();
    // The line an error was found on, shown in pink until the text
    // changes; 0 for none.
    void setErrorLine(int line);
    int errorLine() const { return errorLine_; }

    void gotoLine(int line, int column = 1);
    int line() const;
    int column() const;

    // For the margin, which is a widget of its own.
    void paintMargin(QPaintEvent* event);
    int lineAt(int y) const;

signals:
    void breakpointsChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void markLines();

    QWidget* margin_;
    int errorLine_ = 0;
};

// What the Find and Replace windows ask.
struct FindOptions {
    QString text;
    QString replacement;
    bool caseSensitive = false;
    bool wholeWords = false;
    bool prompt = false;        // ask before each replacement
    bool backward = false;
    bool selectedOnly = false;  // within the selection, not the whole file
    bool entireScope = false;   // from the start (or the end), not from the cursor
};

// The symbols of the last assembly: name, value, and whether the program
// uses them.
class SymbolsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SymbolsDialog(QWidget* parent = nullptr);
    void setSymbols(const std::vector<tuxape::AsmSymbol>& symbols);
    // Names that start as the filter says, '*' and '?' standing for any
    // characters.
    void setFilter(const QString& filter);
    QStringList listed() const;  // "name value", in the order shown
    void select(int row);
    bool saveSymbols(const QString& path) const;

signals:
    void sourceRequested(const QString& file, int line);

private:
    void fill();
    void showSelected();
    const tuxape::AsmSymbol* selected() const;

    std::vector<tuxape::AsmSymbol> symbols_;
    QLineEdit* filter_;
    QTreeWidget* list_;
    QLabel* source_;
    QLabel* line_;
};

// WinAPE's assembler window: source files in tabs, assembled straight
// into the machine's memory, with the errors listed underneath.
class AssemblerDialog : public QDialog {
    Q_OBJECT

public:
    explicit AssemblerDialog(Emulator* emulator, QWidget* parent = nullptr);

    // ---- The files open, one in each tab ----
    int fileCount() const { return static_cast<int>(files_.size()); }
    int currentFile() const;
    void setCurrentFile(int index);
    int newFile();
    // Opens a file in a new tab, or goes to the tab it is already in.
    bool openFile(const QString& path);
    // Saving asks for a name when the file has none. False if it could not
    // be written or the user gave up.
    bool saveFile(int index);
    bool saveFileAs(int index, const QString& path);
    bool saveAll();
    // Asks first about changes not saved; false if the user cancels.
    bool closeFile(int index);
    // The same question for every file, without closing any: before the
    // application ends.
    bool saveBeforeLeaving();
    QString filePath(int index) const;
    QString fileTitle(int index) const;  // what the tab says, without its star
    QString tabText(int index) const;
    CodeEditor* editor(int index = -1) const;  // the current file's by default

    // ---- Assembling ----
    // Assembles the current file and puts the result in the machine. True
    // if there was no error.
    bool assemble();
    // The same, then starts the program at the address its `run` gives.
    bool run();
    const tuxape::AsmResult& result() const { return result_; }
    QStringList errorTexts() const;
    // Goes to the source line of an error in the list.
    void gotoError(int row);
    void gotoSource(const QString& file, int line);
    SymbolsDialog* showSymbols();
    // What the machine prints, when its printer is the assembler: added to
    // a tab of its own, "Printer Output".
    void appendPrinterOutput(const QString& text);

    // ---- Editing ----
    // Finds the next match and selects it; false if there is none.
    bool find(const FindOptions& options);
    bool findAgain();
    int replaceAll(const FindOptions& options);
    void gotoNextBreakpoint();

    // ---- Options ----
    QString libraryPath() const { return libraryPath_; }
    bool pushPcOnRun() const { return pushPc_; }
    bool hideOutput() const { return hideOutput_; }
    void setOptions(const QString& libraryPath, bool pushPcOnRun, bool hideOutput);
    void showOptions();

signals:
    void optionsChanged();
    // The program is in place and wants the machine running.
    void runRequested();
    // The machine's breakpoints are no longer what they were.
    void breakpointsChanged();

private:
    struct Source {
        CodeEditor* editor = nullptr;
        QString path;   // empty until saved
        QString title;
        bool crlf = false;  // how its lines ended when it was read
    };

    void buildMenus(class QMenuBar* bar);
    void updateTab(int index);
    void updateStatus();
    QString sourceName(int index) const;
    QString locate(const QString& name, const QString& from) const;
    void showOutput(const QString& name);
    void applyToMachine(const QString& folder);
    void applyBreakpoints();
    QString store(const QString& name, bool direct, const std::vector<uint8_t>& data, uint16_t load, int exec,
                  const QString& folder);
    bool showFind(bool replace);
    bool replaceNext(const FindOptions& options);

    Emulator* emulator_;
    QList<Source> files_;
    QStackedWidget* stack_;
    QTabBar* tabs_;
    QListWidget* errors_;
    QLabel* position_;
    QLabel* status_;
    SymbolsDialog* symbols_ = nullptr;
    tuxape::AsmResult result_;
    std::vector<tuxape::AsmError> listed_;  // what each row of the error list points at
    std::set<uint16_t> ownBreakpoints_;     // those this window gave the machine
    FindOptions lastFind_;
    int untitled_ = 0;
    QString libraryPath_;
    bool pushPc_ = true;
    bool hideOutput_ = false;
};
