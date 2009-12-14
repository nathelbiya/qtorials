/*
    This work is licensed under the Creative Commons
    Attribution-Noncommercial-Share Alike 3.0 Unported
    License. To view a copy of this license, visit
    http://creativecommons.org/licenses/by-nc-sa/3.0/
    or send a letter to Creative Commons,
    171 Second Street, Suite 300, San Francisco,
    California, 94105, USA.
*/

#include "windows.h"
#include "avisynth.h"
#include "filters.h"
#include "stillimage.h"
#include "subtitle.h"
#include "tools.h"
#include <QtGui>

#if !defined(QT_SHARED) && !defined(QT_DLL)
Q_IMPORT_PLUGIN(qgif)
Q_IMPORT_PLUGIN(qjpeg)
Q_IMPORT_PLUGIN(qtiff)
Q_IMPORT_PLUGIN(qsvg)
#endif

class ZoomNPanProperties : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QRectF rect READ rect WRITE setRect);

public:
    QRectF rect() const
    {
        return m_rect;
    }

    void setRect(const QRectF &rect)
    {
        m_rect = rect;
    }

    static const QByteArray propertyName;

protected:
    QRectF m_rect;
};

const QByteArray ZoomNPanProperties::propertyName = "rect";

class QtorialsZoomNPan : public IClip
{
public:
    struct Detail {
        int keyFrame;
        int transitionLength;
        QRectF detail;
    };

    QtorialsZoomNPan(PClip originClip, int width, int height,
                     int extensionColor, int defaultTransitionFrames, const char *resizeFilter,
                     const QRectF &startDetail, const QList<Detail> &details,
                     IScriptEnvironment* env)
        : m_targetVideoInfo(originClip->GetVideoInfo())
        , m_resizeFilter(resizeFilter)
        , m_extendedClip(extendedClip(originClip, extensionColor, env))
    {
        m_targetVideoInfo.width = width;
        m_targetVideoInfo.height = height;

        Detail previousDetail = { 0, 0, QRectF() };

        {
            QPropertyAnimation *start =
                    new QPropertyAnimation(&m_animationProperties, ZoomNPanProperties::propertyName);
            start->setDuration(0);
            previousDetail.detail =
                    fixedDetailRect(originClip->GetVideoInfo(), QSize(width, height), startDetail);
            start->setStartValue(previousDetail.detail);
            start->setEndValue(previousDetail.detail);
            m_animation.addAnimation(start);
        }

        foreach (const Detail &detail, details) {
            const QRectF detailRect =
                    fixedDetailRect(originClip->GetVideoInfo(), QSize(width, height), detail.detail);
            const int transitionFrames =
                    detail.transitionLength > -1 ? detail.transitionLength : defaultTransitionFrames;
            const int pauseLength = detail.keyFrame - transitionFrames - previousDetail.keyFrame;
            if (pauseLength > 0)
                m_animation.addPause(pauseLength);
            QPropertyAnimation *rectAnimation =
                    new QPropertyAnimation(&m_animationProperties, ZoomNPanProperties::propertyName);
            rectAnimation->setDuration(transitionFrames);
            rectAnimation->setStartValue(previousDetail.detail);
            rectAnimation->setEndValue(detailRect);
            rectAnimation->setEasingCurve(QEasingCurve::InOutQuad);
            m_animation.addAnimation(rectAnimation);

            previousDetail.keyFrame = detail.keyFrame;
            previousDetail.detail = detailRect;
        }

        m_animation.addPause(m_targetVideoInfo.num_frames - previousDetail.keyFrame);

        m_animation.start();
        m_animation.pause();
    }

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env)
    {
        Q_UNUSED(env)
        m_animation.setCurrentTime(n);
        QRectF rect = m_animationProperties.rect();
        if (rect != m_resizedRect) {
            int target_width = m_targetVideoInfo.width;
            int target_height = m_targetVideoInfo.height;
            if (rect.size() == QSizeF(target_width, target_height))
                rect = rect.toRect(); // If native resolution, do not offset at fraction coordinate.
            float src_left = rect.left();
            float src_top = rect.top();
            float src_width = rect.width();
            float src_height = rect.height();
            AVSValue resizedParams[] = { m_extendedClip, target_width, target_height, src_left, src_top, src_width, src_height };
            m_resizedClip = env->Invoke( m_resizeFilter, AVSValue(resizedParams, sizeof resizedParams / sizeof resizedParams[0])).AsClip();
        }
        return m_resizedClip->GetFrame(n, env);
    }
    bool __stdcall GetParity(int n) { Q_UNUSED(n) return false; }
    const VideoInfo& __stdcall GetVideoInfo() { return m_targetVideoInfo; }
    void __stdcall SetCacheHints(int cachehints, int frame_range) { Q_UNUSED(cachehints) Q_UNUSED(frame_range) }
    void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env)
    { Q_UNUSED(buf) Q_UNUSED(start) Q_UNUSED(count) Q_UNUSED(env) }


    static AVSValue __cdecl CreateZoomNPan(AVSValue args, void* user_data, IScriptEnvironment* env)
    {
        Q_UNUSED(user_data)
        static const int valuesPerDetail = 6;

        if (!env->FunctionExists(args[5].AsString()))
            env->ThrowError("QtorialsZoomNPan: Invalid resize filter '%s'.", args[5].AsString());

        const AVSValue &detailValues = args[10];
        if (detailValues.ArraySize() % valuesPerDetail != 0)
            env->ThrowError("QtorialsZoomNPan: Mismatching number of arguments.\n"
                            "They need to be %d per detail.", valuesPerDetail);

        const QRectF start(args[6].AsInt(), args[7].AsInt(), args[8].AsInt(), args[9].AsInt());

        QList<QtorialsZoomNPan::Detail> details;
        for (int i = 0; i < detailValues.ArraySize(); i += valuesPerDetail) {
            const int keyFrame = detailValues[i+0].AsInt();
            const int transitionLength = detailValues[1].AsInt();
            const QRectF rect(detailValues[i+2].AsFloat(), detailValues[i+3].AsFloat(),
                              detailValues[i+4].AsFloat(), detailValues[i+5].AsFloat());
            const QtorialsZoomNPan::Detail detail =
                {keyFrame, transitionLength, rect};
            details.append(detail);
        }

        return new QtorialsZoomNPan(args[0].AsClip(),
                                    args[1].AsInt(Tools::defaultClipWidth),
                                    args[2].AsInt(Tools::defaultClipHeight),
                                    args[3].AsInt(0xffffff),
                                    args[4].AsInt(15),
                                    args[5].AsString(),
                                    start,
                                    details,
                                    env);
    }

protected:
    static const PClip extendedClip(const PClip &originClip, int extensionColor, IScriptEnvironment* env)
    {
        AVSValue extensionParams[] =
            { originClip, m_extensionWidth, m_extensionWidth, m_extensionWidth, m_extensionWidth, extensionColor };
        return env->Invoke("AddBorders",
                           AVSValue(extensionParams, sizeof extensionParams / sizeof extensionParams[0])).AsClip();
    }

    static QRectF fixedDetailRect(const VideoInfo &originVideoInfo,
                                  const QSize &detailClipSize,
                                  const QRectF &specifiedDetailRect)
    {
        QRectF result = specifiedDetailRect;
        if (result.left() == -1 || result.top() == -1) {
            // Fullscreen
            QSizeF zoomNPanSize(detailClipSize);
            zoomNPanSize.scale(originVideoInfo.width, originVideoInfo.height,
                               Qt::KeepAspectRatioByExpanding);
            result.setSize(zoomNPanSize);
            result.moveLeft((originVideoInfo.width - result.width()) / 2);
            result.moveTop((originVideoInfo.height - result.height()) / 2);
        } else if (result.width() == -1 || result.height() == -1) {
            // Native resolution
            result.setSize(detailClipSize);
        }
        return result.translated(m_extensionWidth, m_extensionWidth);
    }

    static const int m_extensionWidth = 16;
    VideoInfo m_targetVideoInfo;
    QByteArray m_resizeFilter;
    PClip m_extendedClip;
    QSequentialAnimationGroup m_animation;
    ZoomNPanProperties m_animationProperties;
    PClip m_resizedClip;
    QRectF m_resizedRect;
};

class SvgAnimationProperties : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal scale READ scale WRITE setScale);
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity);
    Q_ENUMS(Blending);

public:
    enum Blending {
        immediate,
        fade,
        fadeandscale
    };

    struct Data
    {
        QString svgElement;
        int startFrame;
        int endFrame;
        Blending blendingIn;
        Blending blendingOut;
    };

    SvgAnimationProperties(const Data &data)
        : QObject()
        , m_data(data)
        , m_scale(scaleStart)
        , m_opacity(opacityStart)
    {
    }

    QString svgElement() const
    {
        return m_data.svgElement;
    }

    qreal scale() const
    {
        return m_scale;
    }

    void setScale(qreal scale)
    {
        m_scale = scale;
    }

    qreal opacity() const
    {
        return m_opacity;
    }

    void setOpacity(qreal opacity)
    {
        m_opacity = opacity;
    }

    static const QByteArray scalePropertyName;
    static const QByteArray opacityPropertyName;

    static const int scaleDuration;
    static const qreal scaleStart;
    static const qreal scaleEnd;
    static const int opacityDuration;
    static const qreal opacityStart;
    static const qreal opacityEnd;

protected:
    Data m_data;
    qreal m_scale;
    qreal m_opacity;
};

const QByteArray SvgAnimationProperties::scalePropertyName = "scale";
const QByteArray SvgAnimationProperties::opacityPropertyName = "opacity";

const int SvgAnimationProperties::scaleDuration = 5;
const qreal SvgAnimationProperties::scaleStart = 0.2;
const qreal SvgAnimationProperties::scaleEnd = 1.0;
const int SvgAnimationProperties::opacityDuration = SvgAnimationProperties::scaleDuration;
const qreal SvgAnimationProperties::opacityStart = 0.0;
const qreal SvgAnimationProperties::opacityEnd = 1.0;

class QtorialsSvgAnimation : public IClip
{
public:
    QtorialsSvgAnimation(int width, int height,
                         const QString &svgFile, const QList<SvgAnimationProperties::Data> &dataSets,
                         IScriptEnvironment* env)
        : m_svgFile(svgFile)
    {
        Q_UNUSED(env)
        memset(&m_videoInfo, 0, sizeof(VideoInfo));
        m_videoInfo.width = width;
        m_videoInfo.height = height;
        m_videoInfo.fps_numerator = 25;
        m_videoInfo.fps_denominator = 1;
        m_videoInfo.pixel_type = VideoInfo::CS_BGR32;

        foreach(const SvgAnimationProperties::Data &dataSet, dataSets) {
            SvgAnimationProperties *properties = new SvgAnimationProperties(dataSet);
            m_properties.append(properties);

            QSequentialAnimationGroup *scaleSequence = new QSequentialAnimationGroup;
            QSequentialAnimationGroup *opacitySequence = new QSequentialAnimationGroup;
            QEasingCurve scaleInEasingCurve(QEasingCurve::OutBack);
            scaleInEasingCurve.setOvershoot(2.5);

            const bool immediateIn = (dataSet.blendingIn == SvgAnimationProperties::immediate);
            const bool immediateOut = (dataSet.blendingOut == SvgAnimationProperties::immediate);
            const struct Animation {
                QSequentialAnimationGroup *sequence;
                const QByteArray &propertyName;
                int pauseBefore;
                int duration;
                QEasingCurve easingCurve;
                qreal startValue;
                qreal endValue;
            } animations[] = {
                {
                    scaleSequence,
                    SvgAnimationProperties::scalePropertyName,
                    dataSet.startFrame - SvgAnimationProperties::scaleDuration,
                    SvgAnimationProperties::scaleDuration,
                    scaleInEasingCurve,
                    (dataSet.blendingIn == SvgAnimationProperties::fadeandscale) ?
                         SvgAnimationProperties::scaleStart : SvgAnimationProperties::scaleEnd,
                    SvgAnimationProperties::scaleEnd
                }, {
                    scaleSequence,
                    SvgAnimationProperties::scalePropertyName,
                    dataSet.endFrame - dataSet.startFrame,
                    SvgAnimationProperties::scaleDuration,
                    QEasingCurve::InCubic,
                    SvgAnimationProperties::scaleEnd,
                    (dataSet.blendingOut == SvgAnimationProperties::fadeandscale) ?
                         SvgAnimationProperties::scaleStart : SvgAnimationProperties::scaleEnd
                }, {
                    opacitySequence,
                    SvgAnimationProperties::opacityPropertyName,
                    dataSet.startFrame - (immediateIn ? 1 : SvgAnimationProperties::opacityDuration),
                    (immediateIn ? 1 : SvgAnimationProperties::opacityDuration),
                    QEasingCurve::Linear,
                    SvgAnimationProperties::opacityStart,
                    SvgAnimationProperties::opacityEnd
                }, {
                    opacitySequence,
                    SvgAnimationProperties::opacityPropertyName,
                    dataSet.endFrame - dataSet.startFrame,
                    (immediateOut ? 1 : SvgAnimationProperties::opacityDuration),
                    QEasingCurve::Linear,
                    SvgAnimationProperties::opacityEnd,
                    SvgAnimationProperties::opacityStart,
                }
            };

            for (int i = 0; i < int(sizeof animations / sizeof animations[0]); ++i) {
                const struct Animation &a = animations[i];
                a.sequence->addPause(a.pauseBefore);
                QPropertyAnimation *animation =
                        new QPropertyAnimation(properties, a.propertyName);
                animation->setDuration(a.duration);
                animation->setEasingCurve(a.easingCurve);
                animation->setStartValue(a.startValue);
                animation->setEndValue(a.endValue);
                a.sequence->addAnimation(animation);
            }

            scaleSequence->addPause(10000);
            opacitySequence->addPause(10000);

            m_animation.addAnimation(scaleSequence);
            m_animation.addAnimation(opacitySequence);

            m_videoInfo.num_frames = qMax(dataSet.endFrame + qMax(SvgAnimationProperties::opacityDuration, SvgAnimationProperties::scaleDuration) + 1 // +1, so that we have a clear frame at the end
                                          , m_videoInfo.num_frames);
        }

        m_animation.start();
        m_animation.pause();
    }

    ~QtorialsSvgAnimation()
    {
        qDeleteAll(m_properties);
    }

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env)
    {
        Q_UNUSED(env)
        PVideoFrame frame = env->NewVideoFrame(m_videoInfo);
        unsigned char* frameBits = frame->GetWritePtr();
        QImage image(frameBits, m_videoInfo.width, m_videoInfo.height, QImage::Format_ARGB32);
        image.fill(0);
        QPainter p(&image);
        p.scale(1, -1);
        p.translate(0, -image.height());
        m_animation.setCurrentTime(n);
        foreach (const SvgAnimationProperties *properties, m_properties) {
            if (properties->opacity() > SvgAnimationProperties::opacityStart)
                Filters::paintBlendedSvgElement(&p,
                                                m_svgFile,
                                                properties->svgElement(),
                                                properties->opacity(),
                                                properties->scale(),
                                                image.rect()
                                                );
        }

        return frame;
    }
    bool __stdcall GetParity(int n) { Q_UNUSED(n) return false; }
    const VideoInfo& __stdcall GetVideoInfo() { return m_videoInfo; }
    void __stdcall SetCacheHints(int cachehints, int frame_range) { Q_UNUSED(cachehints) Q_UNUSED(frame_range) }
    void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env)
    { Q_UNUSED(buf) Q_UNUSED(start) Q_UNUSED(count) Q_UNUSED(env) }

    static SvgAnimationProperties::Blending blendingForStringOrThrow(const char *blendingKey, IScriptEnvironment* env)
    {
        static const int enumIndex = SvgAnimationProperties::staticMetaObject.indexOfEnumerator("Blending");
        Q_ASSERT(enumIndex);
        static const QMetaEnum &blendEnum = SvgAnimationProperties::staticMetaObject.enumerator(enumIndex);
        const int blending = blendEnum.keysToValue(blendingKey);
        if (blending == -1)
            env->ThrowError("QtorialsSvgAnimation: Invalid blending type '%s'.", blendingKey);
        return SvgAnimationProperties::Blending(blending);
    }

    static AVSValue __cdecl CreateSvgAnimation(AVSValue args, void* user_data, IScriptEnvironment* env)
    {
        Q_UNUSED(user_data)
        static const int valuesPerDetail = 5;

        const AVSValue &detailValues = args[3];
        if (detailValues.ArraySize() % valuesPerDetail != 0)
            env->ThrowError("QtorialsSvgAnimation: Mismatching number of arguments.\n"
                            "They need to be %d per keyframe.", valuesPerDetail);

        const QString svgFileName =
                Tools::cleanFileName(QLatin1String(args[0].AsString()));

        QList<SvgAnimationProperties::Data> details;
        for (int i = 0; i < detailValues.ArraySize(); i += valuesPerDetail) {
            const SvgAnimationProperties::Data animationDetail = {
                QLatin1String(detailValues[i].AsString()),
                detailValues[i+1].AsInt(),
                detailValues[i+2].AsInt(),
                blendingForStringOrThrow(detailValues[i+3].AsString(), env),
                blendingForStringOrThrow(detailValues[i+4].AsString(), env)
            };
            Tools::CheckSvgAndThrow(svgFileName, animationDetail.svgElement, env);
            details.append(animationDetail);
        }

        return new QtorialsSvgAnimation(args[1].AsInt(),
                                        args[2].AsInt(),
                                        svgFileName,
                                        details,
                                        env);
    }

protected:
    QString m_svgFile;
    VideoInfo m_videoInfo;
    QParallelAnimationGroup m_animation;
    QList<SvgAnimationProperties*> m_properties;
};

extern "C" __declspec(dllexport)
const char* __stdcall AvisynthPluginInit2(IScriptEnvironment* env)
{
    env->AddFunction("QtorialsTitle",
                     "[text]s[textcolor]i[width]i[height]i[frames]i",
                     StillImage::CreateTitle, 0);
    env->AddFunction("QtorialsElements", "[elements]s[width]i[height]i[frames]i",
                     StillImage::CreateElements, 0);
    env->AddFunction("QtorialsSvg", "[svgfile]s[elements]s[width]i[height]i[frames]i",
                     StillImage::CreateSvg, 0);
    env->AddFunction("QtorialsSubtitle", "[width]i[height]i.*",
                     Subtitle::CreateSubtitle, 0);
    env->AddFunction("QtorialsZoomNPan",
                     "[clip]c[width]i[height]i[extensioncolor]i[defaulttransitionframes]i[resizefiter]s"
                     "[startleft]i[starttop]i[startwidth]i[startheight]i[details]i*",
                     QtorialsZoomNPan::CreateZoomNPan, 0);
    env->AddFunction("QtorialsSvgAnimation", "[svgfile]s[width]i[height]i.*",
                     QtorialsSvgAnimation::CreateSvgAnimation, 0);
    return "`QtAviSynth' QtAviSynth plugin";
}

#include "qtavisynth.moc"
