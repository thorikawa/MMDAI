/* ----------------------------------------------------------------- */
/*                                                                   */
/*  Copyright (c) 2010-2011  hkrn                                    */
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

#include "SceneLoader.h"
#include "util.h"

#include <vpvl/vpvl.h>
#include <vpvl/gl/Renderer.h>

SceneLoader::SceneLoader(vpvl::gl::Renderer *renderer)
    : m_renderer(renderer),
      m_camera(0)
{
}

SceneLoader::~SceneLoader()
{
    delete m_camera;
    m_camera = 0;
    QHashIterator<vpvl::PMDModel *, QList<vpvl::VMDMotion *> > iterator(m_motions);
    while (iterator.hasNext()) {
        iterator.next();
        vpvl::PMDModel *model = iterator.key();
        QList<vpvl::VMDMotion *> motions = iterator.value();
        foreach (vpvl::VMDMotion *motion, motions) {
            model->removeMotion(motion);
            delete motion;
        }
    }
    foreach (vpvl::PMDModel *model, m_models) {
        m_renderer->unloadModel(model);
        delete model;
    }
    m_models.clear();
    m_motions.clear();
    m_assets.clear();
}

bool SceneLoader::deleteModel(vpvl::PMDModel *model)
{
    if (!model)
        return false;
    const QString &key = m_models.key(model);
    if (!key.isNull()) {
        deleteModelMotion(model);
        m_renderer->scene()->removeModel(model);
        m_renderer->unloadModel(model);
        m_renderer->setSelectedModel(0);
        m_models.remove(key);
        delete model;
        return true;
    }
    return false;
}

bool SceneLoader::deleteModelMotion(vpvl::PMDModel *model)
{
    if (!model)
        return false;
    if (m_motions.contains(model)) {
        QList<vpvl::VMDMotion *> motions = m_motions.value(model);
        foreach (vpvl::VMDMotion *motion, motions) {
            model->removeMotion(motion);
            delete motion;
        }
        motions.clear();
        m_motions.remove(model);
        return true;
    }
    return false;
}

bool SceneLoader::deleteModelMotion(vpvl::VMDMotion *motion, vpvl::PMDModel *model)
{
    if (m_motions.contains(model)) {
        QList<vpvl::VMDMotion *> motions = m_motions.value(model);
        if (motions.contains(motion)) {
            model->removeMotion(motion);
            motions.removeOne(motion);
            delete motion;
        }
        return true;
    }
    return false;
}

vpvl::PMDModel *SceneLoader::findModel(const QString &name) const
{
    return m_models.value(name);
}

QList<vpvl::VMDMotion *> SceneLoader::findModelMotions(vpvl::PMDModel *model) const
{
    return m_motions.value(model);
}

vpvl::Asset *SceneLoader::loadAsset(const QString &baseName, const QDir &dir)
{
    QFile file(dir.absoluteFilePath(baseName));
    vpvl::Asset *asset = 0;
    if (file.open(QFile::ReadOnly)) {
        QByteArray data = file.readAll();
        asset = new vpvl::Asset();
        if (asset->load(reinterpret_cast<const uint8_t *>(data.constData()), data.size())) {
            QString key = baseName;
            if (m_assets.contains(key)) {
                int i = 0;
                while (true) {
                    QString tmpKey = QString("%1%2").arg(key).arg(i);
                    if (!m_assets.contains(tmpKey))
                        key = tmpKey;
                    i++;
                }
            }
            const QByteArray &assetName = baseName.toUtf8();
            char *name = new char[assetName.size() + 1];
            memcpy(name, assetName.constData(), assetName.size());
            name[assetName.size()] = 0;
            asset->setName(name);
            m_renderer->loadAsset(asset, std::string(dir.absolutePath().toLocal8Bit()));
            m_assets[key] = asset;
            m_renderer->scene()->seekMotion(0.0f);
        }
        else {
            delete asset;
            asset = 0;
        }
    }
    return asset;
}

vpvl::VMDMotion *SceneLoader::loadCameraMotion(const QString &path)
{
    QFile file(path);
    vpvl::VMDMotion *motion = 0;
    if (file.open(QFile::ReadOnly)) {
        QByteArray data = file.readAll();
        motion = new vpvl::VMDMotion();
        if (motion->load(reinterpret_cast<const uint8_t *>(data.constData()), data.size())) {
            delete m_camera;
            m_camera = motion;
            m_renderer->scene()->setCameraMotion(motion);
        }
        else {
            delete motion;
            motion = 0;
        }
    }
    return motion;
}

vpvl::PMDModel *SceneLoader::loadModel(const QString &baseName, const QDir &dir, vpvl::VMDMotion *&nullMotion)
{
    const QString &path = dir.absoluteFilePath(baseName);
    QFile file(path);
    vpvl::PMDModel *model = 0;
    if (file.open(QFile::ReadOnly)) {
        QByteArray data = file.readAll();
        model = new vpvl::PMDModel();
        if (model->load(reinterpret_cast<const uint8_t *>(data.constData()), data.size())) {
            m_renderer->loadModel(model, std::string(dir.absolutePath().toLocal8Bit()));
            m_renderer->scene()->addModel(model);
            QString key = internal::toQString(model);
            qDebug() << key << baseName;
            if (m_models.contains(key)) {
                int i = 0;
                while (true) {
                    QString tmpKey = QString("%1%2").arg(key).arg(i);
                    if (!m_models.contains(tmpKey))
                        key = tmpKey;
                    i++;
                }
            }
            nullMotion = new vpvl::VMDMotion();
            nullMotion->setEnableSmooth(false);
            model->addMotion(nullMotion);
            insertModel(model, key);
            insertMotion(nullMotion, model);
            // force to render an added model
            m_renderer->scene()->seekMotion(0.0f);
        }
        else {
            delete model;
            model = 0;
        }
    }
    return model;
}

vpvl::VMDMotion *SceneLoader::loadModelMotion(const QString &path)
{
    QFile file(path);
    vpvl::VMDMotion *motion = 0;
    if (file.open(QFile::ReadOnly)) {
        QByteArray data = file.readAll();
        motion = new vpvl::VMDMotion();
        if (!motion->load(reinterpret_cast<const uint8_t *>(data.constData()), data.size())) {
            delete motion;
            motion = 0;
        }
    }
    return motion;
}

vpvl::VMDMotion *SceneLoader::loadModelMotion(const QString &path, QList<vpvl::PMDModel *> &models)
{
    vpvl::VMDMotion *motion = loadModelMotion(path);
    if (motion) {
        foreach (vpvl::PMDModel *model, m_models) {
            setModelMotion(motion, model);
            models.append(model);
        }
    }
    return motion;
}

vpvl::VMDMotion *SceneLoader::loadModelMotion(const QString &path, vpvl::PMDModel *model)
{
    vpvl::VMDMotion *motion = loadModelMotion(path);
    if (motion)
        setModelMotion(motion, model);
    return motion;
}

void SceneLoader::setModelMotion(vpvl::VMDMotion *motion, vpvl::PMDModel *model)
{
    motion->setEnableSmooth(false);
    model->addMotion(motion);
    insertMotion(motion, model);
}

void SceneLoader::insertModel(vpvl::PMDModel *model, const QString &name)
{
    m_models.insert(name, model);
}

void SceneLoader::insertMotion(vpvl::VMDMotion *motion, vpvl::PMDModel *model)
{
    QList<vpvl::VMDMotion *> motions = m_motions.value(model);
    motions.append(motion);
    m_motions.insert(model, motions);
}
