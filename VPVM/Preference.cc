/**

 Copyright (c) 2010-2013  hkrn

 All rights reserved.

 Redistribution and use in source and binary forms, with or
 without modification, are permitted provided that the following
 conditions are met:

 - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
 - Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided
   with the distribution.
 - Neither the name of the MMDAI project team nor the names of
   its contributors may be used to endorse or promote products
   derived from this software without specific prior written
   permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.

*/

#include "Preference.h"

#include <QApplication>
#include <QScreen>

Preference::Preference(QObject *parent)
    : QObject(parent),
      m_settings(QSettings::IniFormat, QSettings::UserScope, "MMDAI", "VPVM")
{
}

Preference::~Preference()
{
}

void Preference::sync()
{
    m_settings.sync();
}

void Preference::clear()
{
    m_settings.clear();
}

QRect Preference::windowRect() const
{
    if (m_settings.contains("windowRect")) {
        return m_settings.value("windowRect").toRect();
    }
    else {
        const QSize windowSize(960, 620), &margin = (qApp->primaryScreen()->availableSize() - windowSize) / 2;
        const QPoint windowPosition(margin.width(), margin.height());
        return QRect(windowPosition, windowSize);
    }
}

void Preference::setWindowRect(const QRect &value)
{
    if (value != windowRect()) {
        m_settings.setValue("windowRect", value);
        emit windowRectChanged();
    }
}

QString Preference::fontFamily() const
{
#if defined(Q_OS_MACX)
    return m_settings.value("fontFamily", "Osaka").toString();
#elif defined(Q_OS_WIN32)
    return m_settings.value("fontFamily", "Meiryo").toString();
#else
    return m_settings.value("fontFamily").toString();
#endif
}

void Preference::setFontFamily(const QString &value)
{
    if (value != fontFamily()) {
        m_settings.setValue("fontFamily", value);
        emit fontFamilyChanged();
    }
}

int Preference::samples() const
{
    return qMax(m_settings.value("samples", 4).toInt(), 0);
}

void Preference::setSamples(int value)
{
    if (value != samples()) {
        m_settings.setValue("samples", value);
        emit samplesChanged();
    }
}
