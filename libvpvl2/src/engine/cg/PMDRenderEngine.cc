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

#include "vpvl2/cg/PMDRenderEngine.h"
#include "vpvl2/vpvl2.h"

#ifdef VPVL2_ENABLE_OPENCL
#include "vpvl2/cl/Context.h"
#include "vpvl2/cl/PMDAccelerator.h"
#endif

using namespace vpvl;

namespace vpvl2
{
namespace cg
{

PMDRenderEngine::PMDRenderEngine(IRenderDelegate *delegate,
                                 const Scene *scene,
                                 CGcontext effectContext,
                                 cl::PMDAccelerator *accelerator,
                                 pmd::Model *model)
#ifdef VPVL_LINK_QT
    : QGLFunctions(),
      #else
    :
      #endif /* VPVL_LINK_QT */
      m_delegate(delegate),
      m_scene(scene),
      m_accelerator(accelerator),
      m_model(model),
      m_context(effectContext),
      m_textures(0),
      m_cullFaceState(true),
      m_isVertexShaderSkinning(false)
{
}

PMDRenderEngine::~PMDRenderEngine()
{
    glDeleteBuffers(kVertexBufferObjectMax, m_vertexBufferObjects);
    if (m_textures) {
        const vpvl::MaterialList &modelMaterials = m_model->ptr()->materials();
        const int nmaterials = modelMaterials.count();
        for (int i = 0; i < nmaterials; i++) {
            MaterialTextures &materialPrivate = m_textures[i];
            glDeleteTextures(1, &materialPrivate.mainTextureID);
            glDeleteTextures(1, &materialPrivate.subTextureID);
        }
        delete[] m_textures;
        m_textures = 0;
    }
#ifdef VPVL2_ENABLE_OPENCL
    delete m_accelerator;
    m_accelerator = 0;
#endif
    m_delegate = 0;
    m_scene = 0;
    m_model = 0;
    m_context = 0;
    m_cullFaceState = false;
    m_isVertexShaderSkinning = false;
}

IModel *PMDRenderEngine::model() const
{
    return m_model;
}

bool PMDRenderEngine::upload(const IString *dir)
{
    void *context = 0;
    m_delegate->allocateContext(m_model, context);
#ifdef VPVL2_LINK_QT
    const QGLContext *glContext = QGLContext::currentContext();
    initializeGLFunctions(glContext);
#endif
    IString *source = m_delegate->loadShaderSource(IRenderDelegate::kModelEffectTechniques, m_model, dir, context);
    CGeffect effect;
    cgSetErrorHandler(&PMDRenderEngine::handleError, this);
    if (source)
        effect = cgCreateEffect(m_context, reinterpret_cast<const char *>(source->toByteArray()), 0);
    delete source;
    if (!cgIsEffect(effect)) {
        log0(context, IRenderDelegate::kLogWarning, "CG effect compile error\n%s", cgGetLastListing(m_context));
        return false;
    }
    m_effect.attachEffect(effect);
    m_effect.useToon.setValue(true);
    m_effect.parthf.setValue(false);
    m_effect.transp.setValue(false);
    m_effect.opadd.setValue(false);
    PMDModel *model = m_model->ptr();
    glGenBuffers(kVertexBufferObjectMax, m_vertexBufferObjects);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_vertexBufferObjects[kEdgeIndices]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, model->edgeIndicesCount() * model->strideSize(PMDModel::kEdgeIndicesStride),
                 model->edgeIndicesPointer(), GL_STATIC_DRAW);
    log0(context, IRenderDelegate::kLogInfo,
         "Binding edge indices to the vertex buffer object (ID=%d)",
         m_vertexBufferObjects[kEdgeIndices]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_vertexBufferObjects[kModelIndices]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, model->indices().count() * model->strideSize(PMDModel::kIndicesStride),
                 model->indicesPointer(), GL_STATIC_DRAW);
    log0(context, IRenderDelegate::kLogInfo,
         "Binding indices to the vertex buffer object (ID=%d)",
         m_vertexBufferObjects[kModelIndices]);
    const int nvertices = model->vertices().count();
    m_effect.vertexCount.setValue(nvertices);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBufferObjects[kModelVertices]);
    glBufferData(GL_ARRAY_BUFFER, nvertices * model->strideSize(PMDModel::kVerticesStride),
                 model->verticesPointer(), GL_DYNAMIC_DRAW);
    log0(context, IRenderDelegate::kLogInfo,
         "Binding model vertices to the vertex buffer object (ID=%d)",
         m_vertexBufferObjects[kModelVertices]);
    if (m_isVertexShaderSkinning)
        m_model->getSkinningMeshes(m_mesh);
    const MaterialList &materials = model->materials();
    const int nmaterials = materials.count();
    GLuint textureID = 0;
    MaterialTextures *textures = m_textures = new MaterialTextures[nmaterials];
    m_effect.subsetCount.setValue(nmaterials);
    for (int i = 0; i < nmaterials; i++) {
        const Material *material = materials[i];
        IString *primary = m_delegate->toUnicode(material->mainTextureName());
        IString *second = m_delegate->toUnicode(material->subTextureName());
        MaterialTextures &texture = textures[i];
        texture.mainTextureID = 0;
        texture.subTextureID = 0;
        if (m_delegate->uploadTexture(context, primary, dir, &textureID, false)) {
            texture.mainTextureID = textureID;
            log0(context, IRenderDelegate::kLogInfo, "Binding the texture as a primary texture (ID=%d)", textureID);
        }
        if (m_delegate->uploadTexture(context, second, dir, &textureID, false)) {
            texture.subTextureID = textureID;
            log0(context, IRenderDelegate::kLogInfo, "Binding the texture as a secondary texture (ID=%d)", textureID);
        }
        delete primary;
        delete second;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    m_delegate->getToonColor(context, "toon0.bmp", dir, m_toonTextureColors[0]);
    for (int i = 0; i < PMDModel::kCustomTextureMax - 1; i++) {
        const uint8_t *name = model->toonTexture(i);
        m_delegate->getToonColor(context, reinterpret_cast<const char *>(name), dir, m_toonTextureColors[i + 1]);
    }
    setMatrixParameters();
#ifdef VPVL2_ENABLE_OPENCL
    if (m_accelerator && m_accelerator->isAvailable())
        m_accelerator->uploadModel(m_model, m_vertexBufferObjects[kModelVertices], context);
#endif
    model->updateImmediate();
    update();
    IString *modelName = m_delegate->toUnicode(model->name());
    log0(context, IRenderDelegate::kLogInfo, "Created the model: %s", modelName->toByteArray());
    delete modelName;
    m_delegate->releaseContext(m_model, context);
    return true;
}

void PMDRenderEngine::update()
{
    if (!m_model->isVisible() || !m_effect.isAttached())
        return;
    PMDModel *model = m_model->ptr();
    model->setLightPosition(-m_scene->light()->direction());
    int nvertices = model->vertices().count();
    size_t strideSize = model->strideSize(PMDModel::kVerticesStride);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBufferObjects[kModelVertices]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, nvertices * strideSize, model->verticesPointer());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (m_isVertexShaderSkinning)
        m_model->updateSkinningMeshes(m_mesh);
#ifdef VPVL2_ENABLE_OPENCL
    if (m_accelerator && m_accelerator->isAvailable())
        m_accelerator->updateModel(m_model);
#endif
    const Scene::ILight *light = m_scene->light();
    const Vector3 kOne(1, 1, 1);
    m_effect.ambient.setLightColor(kOne);
    m_effect.diffuse.setLightColor(kOne);
    m_effect.emissive.setLightColor(kZeroV3);
    m_effect.emissive.setGeometryColor(kZeroV3);
    m_effect.specular.setLightColor(kOne);
    m_effect.edgeColor.setLightColor(kZeroV3);
    const Vector3 &lightDirection = light->direction();
    m_effect.position.setLightValue(-lightDirection);
    m_effect.direction.setLightValue(lightDirection);
    const Scene::ICamera *camera = m_scene->camera();
    m_effect.position.setCameraValue(camera->position());
    m_effect.direction.setCameraValue(kZeroV3); // TODO: set camera direction
    m_effect.controlObject.update(m_delegate, m_model);
}

void PMDRenderEngine::renderModel()
{
    if (!m_model->isVisible() || !m_effect.isAttached())
        return;
    setMatrixParameters();
    PMDModel *model = m_model->ptr();
    const MaterialList &materials = model->materials();
    const size_t indexStride = model->strideSize(vpvl::PMDModel::kIndicesStride);
    const Scalar &modelOpacity = m_model->opacity();
    const bool hasModelTransparent = !btFuzzyZero(modelOpacity - 1.0),
            hasShadowMap = false; //light->depthTexture() ? true : false;
    const int nmaterials = materials.count();
    Color diffuse;
    size_t offset = 0;
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBufferObjects[kModelVertices]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_vertexBufferObjects[kModelIndices]);
    glVertexPointer(3, GL_FLOAT, model->strideSize(PMDModel::kVerticesStride),
                    reinterpret_cast<const GLvoid *>(model->strideOffset(PMDModel::kVerticesStride)));
    glEnableClientState(GL_VERTEX_ARRAY);
    glNormalPointer(GL_FLOAT, model->strideSize(PMDModel::kNormalsStride),
                    reinterpret_cast<const GLvoid *>(model->strideOffset(PMDModel::kNormalsStride)));
    glEnableClientState(GL_NORMAL_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, model->strideSize(PMDModel::kTextureCoordsStride),
                      reinterpret_cast<const GLvoid *>(model->strideOffset(PMDModel::kTextureCoordsStride)));
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    m_effect.edgeColor.setGeometryColor(m_model->edgeColor());
    for (int i = 0; i < nmaterials; i++) {
        const Material *material = materials[i];
        const MaterialTextures &texture = m_textures[i];
        const Scalar &materialOpacity = material->opacity();
        const Color &toonColor = m_toonTextureColors[material->toonID()];
        diffuse = material->diffuse();
        diffuse.setW(diffuse.w() * materialOpacity);
        m_effect.ambient.setGeometryColor(material->ambient());
        m_effect.diffuse.setGeometryColor(diffuse);
        m_effect.specular.setGeometryColor(material->specular());
        m_effect.specularPower.setGeometryValue(btMax(material->shiness(), 1.0f));
        m_effect.toonColor.setGeometryColor(toonColor);
        bool hasSphereMap = false;
        bool hasMainTexture = texture.mainTextureID > 0;
        if (hasMainTexture) {
            if (material->isMainSphereAdd()) {
                m_effect.materialSphereMap.setTexture(texture.mainTextureID);
                m_effect.spadd.setValue(true);
                m_effect.useTexture.setValue(true);
                hasSphereMap = true;
            }
            else if (material->isSubSphereModulate()) {
                m_effect.materialSphereMap.setTexture(texture.mainTextureID);
                m_effect.spadd.setValue(false);
                m_effect.useTexture.setValue(false);
                hasSphereMap = true;
            }
            else {
                m_effect.materialTexture.setTexture(texture.mainTextureID);
                m_effect.spadd.setValue(false);
                m_effect.useTexture.setValue(true);
            }
        }
        else {
            m_effect.useTexture.setValue(false);
        }
        if (!hasSphereMap) {
            if (material->isSubSphereAdd()) {
                m_effect.materialSphereMap.setTexture(texture.subTextureID);
                m_effect.spadd.setValue(true);
                hasSphereMap = true;
            }
            else if (material->isSubSphereModulate()) {
                m_effect.materialSphereMap.setTexture(texture.subTextureID);
                m_effect.spadd.setValue(false);
                hasSphereMap = true;
            }
        }
        if ((hasModelTransparent && m_cullFaceState) ||
                (!btFuzzyZero(materialOpacity - 1.0f) && m_cullFaceState)) {
            glDisable(GL_CULL_FACE);
            m_cullFaceState = false;
        }
        else if (!m_cullFaceState) {
            glEnable(GL_CULL_FACE);
            m_cullFaceState = true;
        }
        const int nindices = material->countIndices();
        const char *target = hasShadowMap ? "object_ss" : "object";
        CGtechnique technique = m_effect.findTechnique(target, i, nmaterials, hasMainTexture, hasSphereMap, true);
        if (cgIsTechnique(technique)) {
            CGpass pass = cgGetFirstPass(technique);
            while (pass) {
                cgSetPassState(pass);
                glDrawElements(GL_TRIANGLES, nindices, GL_UNSIGNED_SHORT, reinterpret_cast<const GLvoid *>(offset));
                cgResetPassState(pass);
                pass = cgGetNextPass(pass);
            }
        }
        offset += nindices * indexStride;
    }
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    if (!m_cullFaceState) {
        glEnable(GL_CULL_FACE);
        m_cullFaceState = true;
    }
}

void PMDRenderEngine::renderEdge()
{
    if (!m_model->isVisible() || !m_effect.isAttached())
        return;
    setMatrixParameters();
    setNoGeometryColorParameters();
    PMDModel *model = m_model->ptr();
    const MaterialList &materials = model->materials();
    const size_t indexStride = model->strideSize(vpvl::PMDModel::kIndicesStride);
    const int nmaterials = materials.count();
    size_t offset = 0;
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBufferObjects[kModelVertices]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_vertexBufferObjects[kModelIndices]);
    glVertexPointer(3, GL_FLOAT, model->strideSize(PMDModel::kEdgeVerticesStride),
                    reinterpret_cast<const GLvoid *>(model->strideOffset(PMDModel::kEdgeVerticesStride)));
    glEnableClientState(GL_VERTEX_ARRAY);
    glCullFace(GL_FRONT);
    for (int i = 0; i < nmaterials; i++) {
        const Material *material = materials[i];
        const int nindices = material->countIndices();
        CGtechnique technique = m_effect.findTechnique("edge", i, nmaterials, false, false, true);
        if (cgIsTechnique(technique)) {
            CGpass pass = cgGetFirstPass(technique);
            while (pass) {
                cgSetPassState(pass);
                glDrawElements(GL_TRIANGLES, nindices, GL_UNSIGNED_SHORT, reinterpret_cast<const GLvoid *>(offset));
                cgResetPassState(pass);
                pass = cgGetNextPass(pass);
            }
        }
        offset += nindices * indexStride;
    }
    glCullFace(GL_BACK);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void PMDRenderEngine::renderShadow()
{
    if (!m_model->isVisible() || !m_effect.isAttached())
        return;
}

void PMDRenderEngine::renderZPlot()
{
    if (!m_model->isVisible() || !m_effect.isAttached())
        return;
    setMatrixParameters();
    setNoGeometryColorParameters();
    PMDModel *model = m_model->ptr();
    const MaterialList &materials = model->materials();
    const size_t indexStride = model->strideSize(vpvl::PMDModel::kIndicesStride);
    const int nmaterials = materials.count();
    size_t offset = 0;
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBufferObjects[kModelVertices]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_vertexBufferObjects[kModelIndices]);
    glVertexPointer(3, GL_FLOAT, model->strideSize(PMDModel::kVerticesStride),
                    reinterpret_cast<const GLvoid *>(model->strideOffset(PMDModel::kVerticesStride)));
    glEnableClientState(GL_VERTEX_ARRAY);
    glCullFace(GL_FRONT);
    for (int i = 0; i < nmaterials; i++) {
        const Material *material = materials[i];
        const int nindices = material->countIndices();
        if (!btFuzzyZero(material->opacity() - 0.98)) {
            CGtechnique technique = m_effect.findTechnique("zplot", i, nmaterials, false, false, true);
            if (cgIsTechnique(technique)) {
                CGpass pass = cgGetFirstPass(technique);
                while (pass) {
                    cgSetPassState(pass);
                    glDrawElements(GL_TRIANGLES, nindices, GL_UNSIGNED_SHORT, reinterpret_cast<const GLvoid *>(offset));
                    cgResetPassState(pass);
                    pass = cgGetNextPass(pass);
                }
            }
        }
        offset += nindices * indexStride;
    }
    glCullFace(GL_BACK);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void PMDRenderEngine::log0(void *context, IRenderDelegate::LogLevel level, const char *format ...)
{
    va_list ap;
    va_start(ap, format);
    m_delegate->log(context, level, format, ap);
    va_end(ap);
}

void PMDRenderEngine::setMatrixParameters()
{
    m_effect.world.setMatrices(m_delegate, m_model);
    m_effect.view.setMatrices(m_delegate, m_model);
    m_effect.projection.setMatrices(m_delegate, m_model);
    m_effect.worldView.setMatrices(m_delegate, m_model);
    m_effect.viewProjection.setMatrices(m_delegate, m_model);
    m_effect.worldViewProjection.setMatrices(m_delegate, m_model);
}

void PMDRenderEngine::setNoGeometryColorParameters()
{
    m_effect.edgeColor.setGeometryColor(m_model->edgeColor());
    m_effect.toonColor.setGeometryColor(kZeroV3);
    m_effect.ambient.setGeometryColor(kZeroV3);
    m_effect.diffuse.setGeometryColor(kZeroV3);
    m_effect.specular.setGeometryColor(kZeroV3);
    m_effect.specularPower.setGeometryValue(0);
    m_effect.materialTexture.setTexture(0);
    m_effect.materialSphereMap.setTexture(0);
    m_effect.spadd.setValue(false);
    m_effect.useTexture.setValue(false);
}

void PMDRenderEngine::handleError(CGcontext context, CGerror error, void *data)
{
    PMDRenderEngine *engine = static_cast<PMDRenderEngine *>(data);
    Q_UNUSED(context)
    engine->log0(0, IRenderDelegate::kLogWarning, "CGerror: %s", cgGetErrorString(error));
}

} /* namespace gl2 */
} /* namespace vpvl2 */
