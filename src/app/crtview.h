#pragma once

#include <functional>

#include <QImage>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>

class QOpenGLFramebufferObject;
class QPainter;

// The picture as an Amstrad CTM644 colour monitor shows it, drawn by the
// graphics card: scan lines whose beam is wider where it is brighter, the
// tube's slot mask, the glow bright areas throw on what is around them,
// the blue gun landing a little off, and the curve of the glass. TuxAPE's
// own (WinAPE has none); its figures were taken from photographs of a
// CTM644 showing the cards of tools/ctm644-test-cards.bas.
//
// CrtRenderer does the drawing, in whatever OpenGL context is current;
// CrtView is the widget that shows it.
class CrtRenderer : protected QOpenGLFunctions {
public:
    // To be called with a context current; false if its graphics card
    // will not have the shaders. release() too needs the context.
    bool initialize();
    void release();
    // The picture to show: one row to a scan line of the CPC.
    void setFrame(const QImage& frame);
    bool hasFrame() const { return !frame_.isNull(); }
    // Draws the picture over the whole of a framebuffer of that many
    // pixels. A colour tube has a mask; a green or a grey one has none.
    void draw(GLuint framebuffer, const QSize& pixels, bool mask);

    // The mask's pitch, in pixels of the screen, for a picture this wide;
    // 0 when the picture is too small to show it.
    static int triadPixels(int pictureWidth);
    // Whether this machine can draw with its graphics card at all.
    static bool available();
    // A picture drawn without any window, for a screenshot or a test.
    // Null if it cannot be done here.
    static QImage render(const QImage& frame, const QSize& pixels, bool mask = true);

private:
    QImage frame_;
    bool dirty_ = false;
    GLuint frameTexture_ = 0;
    QSize textureSize_;
    QOpenGLShaderProgram blur_;
    QOpenGLShaderProgram tube_;
    QOpenGLFramebufferObject* glow_[5] = {};
};

// The widget: a child of the screen widget, which keeps the keyboard and
// the mouse.
class CrtView : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit CrtView(QWidget* parent = nullptr);
    ~CrtView() override;

    void setFrame(const QImage& frame);
    void setMask(bool on);
    bool mask() const { return mask_; }
    // True once drawing has been found impossible here.
    bool failed() const { return failed_; }

    // Drawn over the picture, in the view's own coordinates.
    std::function<void(QPainter&)> overlay;

signals:
    // No drawing with the graphics card here after all: the caller is to
    // go back to plain drawing.
    void unusable();

protected:
    void initializeGL() override;
    void paintGL() override;
    void showEvent(QShowEvent* event) override;

private:
    CrtRenderer renderer_;
    bool mask_ = true;
    bool failed_ = false;
};
