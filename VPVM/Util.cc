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

#include <vpvl2/vpvl2.h>
#include <vpvl2/extensions/icu4c/String.h>

#include "Util.h"

#include <QCoreApplication>
#include <QDir>

using namespace vpvl2;
using namespace vpvl2::extensions::icu4c;

QString Util::toQString(const IString *value)
{
    return value ? toQString(static_cast<const String *>(value)->value()) : QString();
}

QString Util::toQString(const UnicodeString &value)
{
    const ushort *v = reinterpret_cast<const ushort *>(value.getBuffer());
    return QString::fromUtf16(v, value.length());
}

bool Util::equalsString(const QString lhs, const IString *rhs)
{
    return lhs == Util::toQString(rhs);
}

UnicodeString Util::fromQString(const QString &value)
{
    const UChar *v = reinterpret_cast<const UChar *>(value.utf16());
    return UnicodeString(v, value.size());
}

Vector3 Util::toVector3(const QVector3D &value)
{
    return Vector3(value.x(), value.y(), value.z());
}

QVector3D Util::fromVector3(const Vector3 &value)
{
    return QVector3D(value.x(), value.y(), value.z());
}

Vector3 Util::toColor(const QColor &value)
{
    return Vector3(value.redF(), value.greenF(), value.blueF());
}

QColor Util::fromColor(const Vector3 &value)
{
    return QColor::fromRgbF(value.x(), value.y(), value.z());
}

Quaternion Util::toQuaternion(const QQuaternion &value)
{
    return Quaternion(value.x(), value.y(), value.z(), value.scalar());
}

QQuaternion Util::fromQuaternion(const Quaternion &value)
{
    return QQuaternion(value.w(), value.x(), value.y(), value.z());
}

QString Util::resourcePath(const QString &basePath)
{
#if defined(Q_OS_MAC)
    if (!QDir::isAbsolutePath(basePath)) {
        return QString::fromLatin1("%1/../Resources/%2")
                .arg(QCoreApplication::applicationDirPath(), basePath);
    }
#elif defined(Q_OS_QNX)
    if (!QDir::isAbsolutePath(basePath)) {
        return QString::fromLatin1("app/native/%1").arg(basePath);
    }
#elif defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
    const QString pathInInstallDir =
            QString::fromLatin1("%1/../%2").arg(QCoreApplication::applicationDirPath(), basePath);
    if (QFileInfo(pathInInstallDir).exists()) {
        return pathInInstallDir;
    }
#endif
    return basePath;
}
