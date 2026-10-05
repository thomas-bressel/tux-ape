#pragma once

#include <QDialog>
#include <QImage>

class QCheckBox;
class QLabel;
class QPlainTextEdit;

// WinAPE's "Save Screenshot" window: the picture as it will be saved, at
// full size or with its width or its height halved.
class ScreenshotDialog : public QDialog {
    Q_OBJECT

public:
    // `picture` is the screen at full size, 768 x 540.
    explicit ScreenshotDialog(const QImage& picture, QWidget* parent = nullptr);

    void setHalfSize(bool half);
    void setHalfHeight(bool half);
    bool halfSize() const;
    bool halfHeight() const;
    // The picture with the choices above applied.
    QImage result() const;

    // The kinds of file a screenshot can be saved as, for a file dialog, and
    // the saving itself: the kind goes by the file name's ending (.png when
    // it has none). Returns false if the file cannot be written.
    static QString fileFilter();
    static bool save(const QImage& picture, const QString& path);

private:
    QImage picture_;
    QLabel* preview_;
    QCheckBox* halfSize_;
    QCheckBox* halfHeight_;

    void updatePreview();
};

// WinAPE's "Auto-Type" window: text to be typed on the CPC's keyboard, with
// keys and pauses written between tildes (~RETURN~, ~PAUSE 50~...).
class AutoTypeDialog : public QDialog {
    Q_OBJECT

public:
    explicit AutoTypeDialog(const QString& text, QWidget* parent = nullptr);

    QString text() const;
    void setText(const QString& text);
    // Reads or writes the text as a file. False if it cannot be done.
    bool loadText(const QString& path);
    bool saveText(const QString& path) const;

private:
    QPlainTextEdit* edit_;
};
