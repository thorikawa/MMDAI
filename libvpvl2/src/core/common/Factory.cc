/* ----------------------------------------------------------------- */
/*                                                                   */
/*  Copyright (c) 2010-2012  hkrn                                    */
/*                                                                   */
/* All rights reserved.                                              */
/*                                                                   */
/* Redistribution and use in source and binary forms, with or        */
/* without modification, are permitted provided that the following   */
/* conditions are met:                                               */
/*                                                                   */
/* - Redistributions of source code must retain the above copyright  */
/*   notice, this list of conditions and the following disclaimer.   */
/* - Redistributions in binary form must reproduce the above         */
/*   copyright notice, this list of conditions and the following     */
/*   disclaimer in the documentation and/or other materials provided */
/*   with the distribution.                                          */
/* - Neither the name of the MMDAI project team nor the names of     */
/*   its contributors may be used to endorse or promote products     */
/*   derived from this software without specific prior written       */
/*   permission.                                                     */
/*                                                                   */
/* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND            */
/* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,       */
/* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF          */
/* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE          */
/* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS */
/* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,          */
/* EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED   */
/* TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,     */
/* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON */
/* ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,   */
/* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY    */
/* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE           */
/* POSSIBILITY OF SUCH DAMAGE.                                       */
/* ----------------------------------------------------------------- */

#include "vpvl2/vpvl2.h"

#include "vpvl2/asset/Model.h"
#include "vpvl2/mvd/Motion.h"
#include "vpvl2/pmd/Model.h"
#include "vpvl2/pmx/Model.h"
#include "vpvl2/vmd/BoneKeyframe.h"
#include "vpvl2/vmd/CameraKeyframe.h"
#include "vpvl2/vmd/LightKeyframe.h"
#include "vpvl2/vmd/MorphKeyframe.h"
#include "vpvl2/vmd/Motion.h"

namespace vpvl2
{

struct Factory::PrivateContext
{
    PrivateContext(IEncoding *encoding)
        : encoding(encoding),
          motion(0)
    {
    }
    ~PrivateContext() {
        delete motion;
        motion = 0;
    }

    IEncoding *encoding;
    IMotion *motion;
};

Factory::Factory(IEncoding *encoding)
    : m_context(0)
{
    m_context = new PrivateContext(encoding);
}

Factory::~Factory()
{
    delete m_context;
    m_context = 0;
}

IModel *Factory::createModel(IModel::Type type) const
{
    switch (type) {
    case IModel::kAsset:
        return new asset::Model(m_context->encoding);
    case IModel::kPMD:
        return new pmd::Model(m_context->encoding);
    case IModel::kPMX:
        return new pmx::Model(m_context->encoding);
    default:
        return 0;
    }
}

IModel *Factory::createModel(const uint8_t *data, size_t size, bool &ok) const
{
    IModel *model = 0;
    if (size >= 4 && memcmp(data, "PMX ", 4) == 0) {
        model = new pmx::Model(m_context->encoding);
    }
    else if (size >= 3 && memcmp(data, "Pmd", 3) == 0) {
        model = new pmd::Model(m_context->encoding);
    }
    else {
        model = new asset::Model(m_context->encoding);
    }
    ok = model ? model->load(data, size) : false;
    return model;
}

IMotion *Factory::createMotion() const
{
    return new vmd::Motion(0, m_context->encoding);
}

IMotion *Factory::createMotion(const uint8_t *data, size_t size, IModel *model, bool &ok) const
{
    IMotion *motion = 0;
    if (size >= sizeof(vmd::Motion::kSignature) &&
            memcmp(data, vmd::Motion::kSignature, sizeof(vmd::Motion::kSignature) - 1) == 0) {
        motion = m_context->motion = new vmd::Motion(model, m_context->encoding);
        ok = motion->load(data, size);
    }
    else if (size >= sizeof(mvd::Motion::kSignature) &&
             memcmp(data, mvd::Motion::kSignature, sizeof(mvd::Motion::kSignature) - 1) == 0) {
        motion = m_context->motion = new mvd::Motion(model, m_context->encoding);
        ok = motion->load(data, size);
    }
    m_context->motion = 0;
    return motion;
}

IBoneKeyframe *Factory::createBoneKeyframe() const
{
    return new vmd::BoneKeyframe(m_context->encoding);
}

ICameraKeyframe *Factory::createCameraKeyframe() const
{
    return new vmd::CameraKeyframe();
}

ILightKeyframe *Factory::createLightKeyframe() const
{
    return new vmd::LightKeyframe();
}

IMorphKeyframe *Factory::createMorphKeyframe() const
{
    return new vmd::MorphKeyframe(m_context->encoding);
}

}
