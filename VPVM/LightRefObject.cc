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

#include "LightMotionTrack.h"
#include "LightRefObject.h"
#include "MotionProxy.h"
#include "ProjectProxy.h"
#include "Util.h"

#include <vpvl2/vpvl2.h>

using namespace vpvl2;

LightRefObject::LightRefObject(ProjectProxy *project)
    : QObject(project),
      m_projectRef(project),
      m_lightRef(0),
      m_name(tr("Light")),
      m_index(0)
{
    connect(this, &LightRefObject::lightDidReset, this, &LightRefObject::colorChanged);
    connect(this, &LightRefObject::lightDidReset, this, &LightRefObject::directionChanged);
}

LightRefObject::~LightRefObject()
{
    releaseMotion();
    m_lightRef = 0;
}

void LightRefObject::reset()
{
    Q_ASSERT(m_lightRef);
    m_lightRef->resetDefault();
    emit lightDidReset();
}

void LightRefObject::releaseMotion()
{
    if (m_motion) {
        m_lightRef->setMotion(0);
        m_projectRef->projectInstanceRef()->removeMotion(m_motion->data());
        m_track.reset();
        m_motion.reset();
    }
}

void LightRefObject::assignLightRef(ILight *lightRef, MotionProxy *motionProxy)
{
    Q_ASSERT(lightRef);
    lightRef->setMotion(motionProxy->data());
    m_motion.reset(motionProxy);
    m_track.reset(new LightMotionTrack(motionProxy, this));
    motionProxy->setLightMotionTrack(m_track.data());
    m_lightRef = lightRef;
    emit motionChanged();
    refresh();
}

void LightRefObject::refresh()
{
    Q_ASSERT(m_lightRef);
    m_color = Util::fromVector3(m_lightRef->color());
    m_direction = Util::fromVector3(m_lightRef->direction());
    emit lightDidReset();
}

ProjectProxy *LightRefObject::project() const
{
    return m_projectRef;
}

MotionProxy *LightRefObject::motion() const
{
    return m_motion.data();
}

LightMotionTrack *LightRefObject::track() const
{
    return m_track.data();
}

ILight *LightRefObject::data() const
{
    return m_lightRef;
}

QVector3D LightRefObject::color() const
{
    return m_color;
}

void LightRefObject::setColor(const QVector3D &value)
{
    Q_ASSERT(m_lightRef);
    if (!qFuzzyCompare(value, color())) {
        m_lightRef->setColor(Util::toVector3(m_color));
        m_color = value;
        emit colorChanged();
    }
}

QVector3D LightRefObject::direction() const
{
    return m_direction;
}

void LightRefObject::setDirection(const QVector3D &value)
{
    Q_ASSERT(m_lightRef);
    if (!qFuzzyCompare(value, direction())) {
        m_lightRef->setDirection(Util::toVector3(m_direction));
        m_direction = value;
        emit directionChanged();
    }
}
