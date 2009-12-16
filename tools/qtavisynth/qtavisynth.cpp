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
#include "stillimage.h"
#include "subtitle.h"
#include "zoomnpan.h"
#include "svganimation.h"
#include <QtPlugin>

#if !defined(QT_SHARED) && !defined(QT_DLL)
Q_IMPORT_PLUGIN(qgif)
Q_IMPORT_PLUGIN(qjpeg)
Q_IMPORT_PLUGIN(qtiff)
Q_IMPORT_PLUGIN(qsvg)
#endif

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
                     "[clip]c[width]i[height]i[extensioncolor]i"
                     "[defaulttransitionframes]i[resizefiter]s"
                     "[startleft]i[starttop]i[startwidth]i[startheight]i[details]i*",
                     ZoomNPan::CreateZoomNPan, 0);
    env->AddFunction("QtorialsSvgAnimation", "[svgfile]s[width]i[height]i.*",
                     SvgAnimation::CreateSvgAnimation, 0);
    return "`QtAviSynth' QtAviSynth plugin";
}
