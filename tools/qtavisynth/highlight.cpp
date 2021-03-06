#include "highlight.h"
#include "filters.h"
#include "rgboverlay.h"
#include <QPropertyAnimation>
#include <QEasingCurve>

class HighlightProperties : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QRectF rectangle READ rectangle WRITE setRectangle);
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity);

public:
    HighlightProperties(QObject *parent = 0);
    QRectF rectangle() const;
    void setRectangle(const QRectF &rectangle);
    qreal opacity() const;
    void setOpacity(qreal opacity);

    static const QByteArray rectanglePropertyName;
    static const QByteArray opacityPropertyName;

protected:
    QRectF m_rectangle;
    qreal m_opacity;
};

HighlightProperties::HighlightProperties(QObject *parent)
    : QObject(parent)
    , m_opacity(0)
{
}

QRectF HighlightProperties::rectangle() const
{
    return m_rectangle;
}

void HighlightProperties::setRectangle(const QRectF &rectangle)
{
    m_rectangle = rectangle;
}

qreal HighlightProperties::opacity() const
{
    return m_opacity;
}

void HighlightProperties::setOpacity(qreal opacity)
{
    m_opacity = opacity;
}

const QByteArray HighlightProperties::rectanglePropertyName = "rectangle";
const QByteArray HighlightProperties::opacityPropertyName = "opacity";
const int Highlight::m_blendInFrames = 12;
const int Highlight::m_blendOutFrames = 6;

Highlight::Highlight(const VideoInfo &videoInfo,
                     const QRect &rectangle, int startFrame, int endFrame)
    : m_videoInfo(videoInfo)
{
    m_videoInfo.pixel_type = VideoInfo::CS_BGR32;

    m_properties = new HighlightProperties(&m_highlightAnimations);
    QSequentialAnimationGroup *rectangleAnimation = new QSequentialAnimationGroup;
    QSequentialAnimationGroup *opacityAnimation = new QSequentialAnimationGroup;

    const int shrink = 30;
    const QRectF startRectangle =
            rectangle.adjusted(-shrink, -shrink, shrink, shrink);
    const struct Animation {
        QSequentialAnimationGroup *sequence;
        const QByteArray &propertyName;
        const QEasingCurve easing;
        int pauseBefore;
        int duration;
        const QVariant startValue;
        const QVariant endValue;
    } animations[] = {
        { opacityAnimation, HighlightProperties::opacityPropertyName, QEasingCurve::Linear, startFrame, m_blendInFrames, 0.0, 1.0 },
        { opacityAnimation, HighlightProperties::opacityPropertyName, QEasingCurve::Linear, endFrame - startFrame - m_blendInFrames - m_blendOutFrames, m_blendOutFrames, 1.0, 0.0 },
        { rectangleAnimation, HighlightProperties::rectanglePropertyName, QEasingCurve::OutQuad, startFrame, m_blendInFrames, startRectangle, rectangle }
    };

    for (int i = 0; i < int(sizeof animations / sizeof animations[0]); ++i) {
        const struct Animation &a = animations[i];
        a.sequence->addPause(a.pauseBefore);
        QPropertyAnimation *animation =
                new QPropertyAnimation(m_properties, a.propertyName);
        animation->setEasingCurve(a.easing);
        animation->setDuration(a.duration);
        animation->setStartValue(a.startValue);
        animation->setEndValue(a.endValue);
        a.sequence->addAnimation(animation);
    }

    rectangleAnimation->addPause(m_videoInfo.num_frames - rectangleAnimation->duration());
    opacityAnimation->addPause(m_videoInfo.num_frames - opacityAnimation->duration());

    m_highlightAnimations.addAnimation(rectangleAnimation);
    m_highlightAnimations.addAnimation(opacityAnimation);
    m_highlightAnimations.start();
    m_highlightAnimations.pause();
}

void Highlight::paintFrame(QPainter *painter, int frameNumber, const QRect &rect) const
{
    Q_UNUSED(rect)
    m_highlightAnimations.setCurrentTime(frameNumber);
    Filters::paintHighlight(painter, m_properties->rectangle(), m_properties->opacity());
}

PVideoFrame __stdcall Highlight::GetFrame(int n, IScriptEnvironment* env)
{
    PVideoFrame frame = env->NewVideoFrame(m_videoInfo);
    unsigned char* frameBits = frame->GetWritePtr();
    QImage image(frameBits, m_videoInfo.width, m_videoInfo.height, QImage::Format_ARGB32);
    image.fill(0);
    QPainter p(&image);
    p.scale(1, -1);
    p.translate(0, -image.height());
    paintFrame(&p, n, QRect());
    return frame;
}

AVSValue __cdecl Highlight::CreateHighlight(AVSValue args, void* user_data,
                                            IScriptEnvironment* env)
{
    Q_UNUSED(user_data)
    const PClip background = args[0].AsClip();
    const QRect rect(args[1].AsInt(0), args[2].AsInt(0),
                     args[3].AsInt(100), args[4].AsInt(100));
    const int start = args[5].AsInt(10);
    const int end = args[6].AsInt(30);
    const PClip hightlightClip =
            new Highlight(background->GetVideoInfo(), rect, start, end);
    return new RgbOverlay(background, hightlightClip, env);
}

bool __stdcall Highlight::GetParity(int n)
{
    Q_UNUSED(n)
    return false;
}

const VideoInfo& __stdcall Highlight::GetVideoInfo()
{
    return m_videoInfo;
}

void __stdcall Highlight::SetCacheHints(int cachehints, int frame_range)
{
    Q_UNUSED(cachehints)
    Q_UNUSED(frame_range)
}

void __stdcall Highlight::GetAudio(void* buf, __int64 start, __int64 count,
                                   IScriptEnvironment* env)
{
    Q_UNUSED(buf)
    Q_UNUSED(start)
    Q_UNUSED(count)
    Q_UNUSED(env)
}

#include "highlight.moc"
