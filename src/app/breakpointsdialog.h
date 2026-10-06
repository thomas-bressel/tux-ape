#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class Emulator;
class QLabel;
class QPushButton;
class QTabWidget;
class QTreeWidget;

// WinAPE's Breakpoints window: the breakpoints on code, on memory and on
// input and output, each with how many times it has counted and the
// condition it may have. A double click on one opens its properties.
class BreakpointsDialog : public QDialog {
    Q_OBJECT

public:
    enum Page { Code, Memory, InputOutput };

    explicit BreakpointsDialog(Emulator* emulator, QWidget* parent = nullptr);

    void refresh();
    int page() const;
    void setPage(int page);
    // A page's rows, their columns between bars.
    QStringList rows(int page) const;
    void select(int row);

    // A breakpoint's condition and pass count. False, and nothing changed,
    // if the condition cannot be made sense of.
    bool setProperties(int page, int row, const QString& condition, int passCount);
    // The devices the Add window offers, the last being "User Defined
    // Port"; lines that start with spaces are parts of the one above.
    static QStringList devices();
    // A breakpoint on a device, or with `device` the last on a port of
    // one's own: a port that is `port` under `mask`, read, written or both.
    bool addIoBreak(int device, const QString& condition = {}, int passCount = 0, unsigned port = 0,
                    unsigned mask = 0xFFFF, bool input = true, bool output = true);
    bool addMemoryBreak(unsigned address, int size, bool write, const QString& condition = {}, int passCount = 0);
    void clear(int page, int row);
    void clearAll(int page);

    // The windows the buttons and a double click open.
    void showAdd();
    void showProperties();

signals:
    // The machine's breakpoints are no longer what they were.
    void changed();

private:
    QTreeWidget* list(int page) const;
    void updateButtons();

    Emulator* emulator_;
    QTabWidget* tabs_;
    QPushButton* add_;
    QPushButton* clear_;
    QPushButton* clearAll_;
};

// WinAPE's Timers window: for each timer that breakpoint conditions have
// started and stopped (timer_start(id), timer_stop(id)), how many times,
// and the microseconds it took: the last time, the least, the most and on
// average.
class TimersDialog : public QDialog {
    Q_OBJECT

public:
    explicit TimersDialog(Emulator* emulator, QWidget* parent = nullptr);
    void refresh();
    // The rows, their columns between bars.
    QStringList rows() const;
    void clearAll();

private:
    Emulator* emulator_;
    QTreeWidget* list_;
};
