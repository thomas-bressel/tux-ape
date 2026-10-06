#include "crtview.h"

#include <algorithm>
#include <cmath>

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QPainter>
#include <QTimer>

namespace {

// What was measured on the photographs, in pixels of mode 1 (a 320th of
// the picture's width, borders left out) and in scan lines:
//   - the mask is a slot mask; a triad of it is 0.85 pixel wide, a slot
//     0.82 line high, alternate columns half a slot down;
//   - a line one pixel thick is seen 1.4 pixel thick: the beam's spot;
//   - scan lines show most in the middle tones: the beam is finer there;
//   - right beside a large bright area the dark is lit to a few hundredths
//     of it, and still to a hundredth a third of the screen away;
//   - blue lands a fifth of a pixel to the right of red and green, wider.
constexpr double kTriad = 0.85;
constexpr double kSlot = 0.82;
// The whole frame, borders included, is this many mode 1 pixels wide.
constexpr double kFrameWidth = 384.0;

const char* const kVertex = R"(
attribute vec2 position;
uniform float flip;
varying vec2 uv;
void main()
{
    uv = vec2(position.x, position.y * flip) * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

// A blur along one direction, nine taps two strides wide. Light is kept
// as its square root between two passes: eight bits hold the dark well
// that way.
const char* const kBlur = R"(
#ifdef GL_ES
precision highp float;
#endif
uniform sampler2D source;
uniform vec2 stride;
uniform float decode;
varying vec2 uv;
vec3 fetch(vec2 at)
{
    vec3 c = texture2D(source, at).rgb;
    return decode > 0.5 ? pow(c, vec3(2.2)) : c * c;
}
void main()
{
    vec3 sum = fetch(uv) * 0.2042;
    sum += (fetch(uv + stride) + fetch(uv - stride)) * 0.1802;
    sum += (fetch(uv + 2.0 * stride) + fetch(uv - 2.0 * stride)) * 0.1238;
    sum += (fetch(uv + 3.0 * stride) + fetch(uv - 3.0 * stride)) * 0.0663;
    sum += (fetch(uv + 4.0 * stride) + fetch(uv - 4.0 * stride)) * 0.0276;
    gl_FragColor = vec4(sqrt(sum), 1.0);
}
)";

const char* const kTube = R"(
#ifdef GL_ES
precision highp float;
#endif
uniform sampler2D frame;
uniform sampler2D tight;
uniform sampler2D wide;
uniform vec2 frameSize;   // the frame: texels across, scan lines down
uniform vec2 outSize;     // the picture on the screen, in its pixels
uniform float triad;      // the mask's pitch in screen pixels; 0: no mask
uniform float depth;      // how dark the mask is, 0 to 1
uniform float slot;       // a slot's height in screen pixels; 0: stripes
varying vec2 uv;

vec3 lin(vec3 c)
{
    return pow(c, vec3(2.2));
}

// The light of one scan line at a place along it. The spot is not a
// point: each pixel runs into its neighbours. The blue gun lands a little
// to the right of the others, and its spot is wider.
vec3 scanline(float row, float x)
{
    float v = (row + 0.5) / frameSize.y;
    float du = 1.0 / frameSize.x;
    float u = x * du;
    vec3 a = lin(texture2D(frame, vec2(u - 0.9 * du, v)).rgb);
    vec3 b = lin(texture2D(frame, vec2(u, v)).rgb);
    vec3 c = lin(texture2D(frame, vec2(u + 0.9 * du, v)).rgb);
    vec3 beam = a * 0.23 + b * 0.54 + c * 0.23;
    float ub = u - 0.40 * du;
    float blue = pow(texture2D(frame, vec2(ub - 1.3 * du, v)).b, 2.2) * 0.27
               + pow(texture2D(frame, vec2(ub, v)).b, 2.2) * 0.46
               + pow(texture2D(frame, vec2(ub + 1.3 * du, v)).b, 2.2) * 0.27;
    return vec3(beam.rg, blue);
}

void main()
{
    // The glass is curved: the picture's corners draw in.
    vec2 p = uv * 2.0 - 1.0;
    vec2 q = p * (1.0 + vec2(0.025, 0.034) * p.yx * p.yx);
    vec2 edge = abs(q) - vec2(0.93);
    float face = 1.0 - smoothstep(-0.006, 0.002, length(max(edge, 0.0)) - 0.07);
    vec2 s = q * 0.5 + 0.5;

    // The two scan lines this place lies between. A brighter beam is a
    // wider one; where the screen has too few pixels to a line to show
    // them, the lines run together.
    float x = s.x * frameSize.x;
    float y = s.y * frameSize.y - 0.5;
    float row = floor(y);
    float f = y - row;
    vec3 c0 = scanline(row, x);
    vec3 c1 = scanline(row + 1.0, x);
    float sharp = clamp((outSize.y / frameSize.y - 2.0) / 2.0, 0.0, 1.0);
    vec3 w0 = mix(vec3(0.70), mix(vec3(0.27), vec3(0.37), c0), sharp);
    vec3 w1 = mix(vec3(0.70), mix(vec3(0.27), vec3(0.37), c1), sharp);
    vec3 beam = c0 * exp(-f * f / (2.0 * w0 * w0)) + c1 * exp(-(1.0 - f) * (1.0 - f) / (2.0 * w1 * w1));
    beam /= mix(1.5, 0.87, sharp);

    // The mask, as the photographs show it enlarged: stripes of red, green
    // and blue phosphor, three to a triad, cut into slots: bright beads
    // with rounded ends, a dark line between two triads and a dark gap
    // between two slots, those of one triad half a slot lower than its
    // neighbour's. A screen with few pixels shows the stripes alone.
    if (triad > 0.0) {
        vec2 at = gl_FragCoord.xy;
        float across = at.x / triad;
        float third = fract(across) * 3.0;
        float stripe = floor(third);
        vec3 lit = vec3(stripe == 0.0, stripe == 1.0, stripe == 2.0);
        vec3 mask = mix(vec3(1.0 - 0.55 * depth), vec3(1.0 + 0.30 * depth), lit);
        if (slot > 0.0) {
            float bead = 1.0 - smoothstep(0.62, 1.0, abs(fract(across) - 0.5) * 2.0);
            float down = fract((at.y + mod(floor(across), 2.0) * slot * 0.5) / slot);
            bead *= smoothstep(0.0, 0.26, down) * smoothstep(0.0, 0.26, 1.0 - down);
            mask *= mix(1.0 - 0.8 * depth, 1.0, bead) * 1.3;
        }
        beam *= mask;
    }

    // The glow of what is bright on what is around it: close by, and
    // far across the glass.
    vec3 close = texture2D(tight, s).rgb;
    vec3 distant = texture2D(wide, s).rgb;
    vec3 colour = beam + close * close * 0.12 + distant * distant * 0.085;
    // The tube's white is a cold one.
    colour *= vec3(0.95, 0.99, 1.05);

    colour *= 1.0 - 0.10 * dot(p, p);
    gl_FragColor = vec4(pow(clamp(colour * face, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}
)";

}  // namespace

// ---- the drawing ----------------------------------------------------------------

bool CrtRenderer::available()
{
    static const bool can = [] {
        QOpenGLContext probe;
        return probe.create();
    }();
    return can;
}

int CrtRenderer::triadPixels(int pictureWidth)
{
    const double real = kTriad * pictureWidth / kFrameWidth;
    // Three pixels are the least a triad can be drawn with; under half of
    // that the mask would be nothing like the real one.
    return real < 1.6 ? 0 : std::max(3, static_cast<int>(std::lround(real)));
}

void CrtRenderer::setFrame(const QImage& frame)
{
    frame_ = frame.convertToFormat(QImage::Format_RGBX8888);
    dirty_ = true;
}

bool CrtRenderer::initialize()
{
    initializeOpenGLFunctions();
    // What an earlier context held is gone with it.
    for (QOpenGLFramebufferObject*& buffer : glow_)
        buffer = nullptr;
    frameTexture_ = 0;
    textureSize_ = QSize();

    const auto build = [](QOpenGLShaderProgram& program, const char* fragment) {
        program.removeAllShaders();
        program.bindAttributeLocation("position", 0);
        return program.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertex) &&
               program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) && program.link();
    };
    if (!build(blur_, kBlur) || !build(tube_, kTube))
        return false;
    const auto smooth = [this](GLuint texture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    glGenTextures(1, &frameTexture_);
    smooth(frameTexture_);
    // The glow is worked out on smaller and smaller pictures.
    const QSize sizes[5] = {{384, 270}, {384, 270}, {192, 135}, {96, 68}, {96, 68}};
    bool sound = true;
    for (int i = 0; i < 5; ++i) {
        glow_[i] = new QOpenGLFramebufferObject(sizes[i]);
        smooth(glow_[i]->texture());
        sound = sound && glow_[i]->isValid();
    }
    dirty_ = true;
    return sound;
}

void CrtRenderer::release()
{
    for (QOpenGLFramebufferObject*& buffer : glow_) {
        delete buffer;
        buffer = nullptr;
    }
    if (frameTexture_)
        glDeleteTextures(1, &frameTexture_);
    frameTexture_ = 0;
    textureSize_ = QSize();
    blur_.removeAllShaders();
    tube_.removeAllShaders();
}

void CrtRenderer::draw(GLuint framebuffer, const QSize& pixels, bool mask)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    if (frame_.isNull() || !frameTexture_ || !glow_[0]) {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, frameTexture_);
    if (dirty_) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (frame_.size() != textureSize_) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_.width(), frame_.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         frame_.constBits());
            textureSize_ = frame_.size();
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame_.width(), frame_.height(), GL_RGBA, GL_UNSIGNED_BYTE,
                            frame_.constBits());
        }
        dirty_ = false;
    }

    static const GLfloat kQuad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    const float texels = static_cast<float>(frame_.width()), lines = static_cast<float>(frame_.height());

    // The glow: the picture blurred a little, then a great deal.
    blur_.bind();
    blur_.enableAttributeArray(0);
    blur_.setAttributeArray(0, GL_FLOAT, kQuad, 2);
    blur_.setUniformValue("source", 0);
    blur_.setUniformValue("flip", 1.0f);
    const auto pass = [&](int target, GLuint source, float dx, float dy, bool decode) {
        glow_[target]->bind();
        glViewport(0, 0, glow_[target]->width(), glow_[target]->height());
        glBindTexture(GL_TEXTURE_2D, source);
        blur_.setUniformValue("stride", QVector2D(dx, dy));
        blur_.setUniformValue("decode", decode ? 1.0f : 0.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };
    pass(0, frameTexture_, 1.2f / texels, 0.0f, true);
    pass(1, glow_[0]->texture(), 0.0f, 0.6f / lines, false);
    pass(2, glow_[1]->texture(), 2.0f / 384.0f, 0.0f, false);
    pass(3, glow_[2]->texture(), 0.0f, 2.0f / 135.0f, false);
    pass(4, glow_[3]->texture(), 2.6f / 96.0f, 0.0f, false);
    pass(3, glow_[4]->texture(), 0.0f, 2.6f / 68.0f, false);
    blur_.disableAttributeArray(0);
    blur_.release();

    // The tube.
    const int wide = pixels.width(), high = pixels.height();
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, wide, high);
    glClear(GL_COLOR_BUFFER_BIT);
    const double realTriad = kTriad * wide / kFrameWidth;
    const double slot = kSlot * high / lines;
    tube_.bind();
    tube_.enableAttributeArray(0);
    tube_.setAttributeArray(0, GL_FLOAT, kQuad, 2);
    tube_.setUniformValue("flip", -1.0f);
    tube_.setUniformValue("frame", 0);
    tube_.setUniformValue("tight", 1);
    tube_.setUniformValue("wide", 2);
    tube_.setUniformValue("frameSize", QVector2D(texels, lines));
    tube_.setUniformValue("outSize", QVector2D(static_cast<float>(wide), static_cast<float>(high)));
    tube_.setUniformValue("triad", static_cast<float>(mask ? triadPixels(wide) : 0));
    // The mask comes in as the picture grows large enough for it.
    tube_.setUniformValue("depth", static_cast<float>(std::clamp((realTriad - 1.6) / 1.2, 0.0, 1.0)));
    tube_.setUniformValue("slot", slot >= 4.0 ? static_cast<float>(std::lround(slot)) : 0.0f);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, glow_[3]->texture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, glow_[1]->texture());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, frameTexture_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    tube_.disableAttributeArray(0);
    tube_.release();
}

QImage CrtRenderer::render(const QImage& frame, const QSize& pixels, bool mask)
{
    QOpenGLContext context;
    QOffscreenSurface surface;
    surface.create();
    if (!context.create() || !surface.isValid() || !context.makeCurrent(&surface))
        return {};
    QImage picture;
    {
        CrtRenderer renderer;
        if (renderer.initialize()) {
            QOpenGLFramebufferObject target(pixels);
            renderer.setFrame(frame);
            renderer.draw(target.handle(), pixels, mask);
            picture = target.toImage().convertToFormat(QImage::Format_RGB32);
        }
        renderer.release();
    }
    context.doneCurrent();
    return picture;
}

// ---- the widget -----------------------------------------------------------------

CrtView::CrtView(QWidget* parent)
    : QOpenGLWidget(parent)
{
}

CrtView::~CrtView()
{
    if (context()) {
        makeCurrent();
        renderer_.release();
        doneCurrent();
    }
}

void CrtView::setFrame(const QImage& frame)
{
    renderer_.setFrame(frame);
    update();
}

void CrtView::setMask(bool on)
{
    mask_ = on;
    update();
}

// Where widgets cannot be drawn by the graphics card (a display with no
// such thing) the view never comes to life: said once it has had the time.
void CrtView::showEvent(QShowEvent* event)
{
    QOpenGLWidget::showEvent(event);
    QTimer::singleShot(300, this, [this] {
        if (isVisible() && !isValid() && !failed_) {
            failed_ = true;
            emit unusable();
        }
    });
}

void CrtView::initializeGL()
{
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
        makeCurrent();
        renderer_.release();
        doneCurrent();
    });
    if (!renderer_.initialize()) {
        failed_ = true;
        emit unusable();
    }
}

void CrtView::paintGL()
{
    if (failed_)
        return;
    const qreal ratio = devicePixelRatioF();
    renderer_.draw(defaultFramebufferObject(),
                   QSize(static_cast<int>(std::lround(width() * ratio)), static_cast<int>(std::lround(height() * ratio))), mask_);
    if (overlay) {
        QPainter painter(this);
        overlay(painter);
    }
}
