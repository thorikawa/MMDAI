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
#include <vpvl2/extensions/icu4c/Encoding.h>
#include <vpvl2/extensions/Pose.h>
#include <vpvl2/extensions/World.h>
#include <vpvl2/extensions/XMLProject.h>

#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btStaticPlaneShape.h>
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <LinearMath/btIDebugDraw.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <QtCore>
#include <QtGui>
#include <QApplication>
#include <QUndoGroup>
#include <QUndoStack>

#include "BaseKeyframeRefObject.h"
#include "BoneKeyframeRefObject.h"
#include "BoneMotionTrack.h"
#include "BoneRefObject.h"
#include "CameraMotionTrack.h"
#include "CameraRefObject.h"
#include "LabelRefObject.h"
#include "LightMotionTrack.h"
#include "LightRefObject.h"
#include "ModelProxy.h"
#include "MorphKeyframeRefObject.h"
#include "MorphRefObject.h"
#include "MorphMotionTrack.h"
#include "MotionProxy.h"
#include "ProjectProxy.h"
#include "Util.h"

using namespace vpvl2;
using namespace vpvl2::extensions;

namespace {

struct ProjectDelegate : public XMLProject::IDelegate {
    ProjectDelegate(ProjectProxy *proxy)
        : m_projectRef(proxy)
    {
    }
    ~ProjectDelegate() {
        m_projectRef = 0;
    }
    const std::string toStdFromString(const IString *value) const {
        return Util::toQString(value).toStdString();
    }
    const IString *toStringFromStd(const std::string &value) const {
        return new icu4c::String(Util::fromQString(QString::fromStdString(value)));
    }
    bool loadModel(const XMLProject::UUID &uuid, const XMLProject::StringMap &settings, IModel::Type /* type */, IModel *&model, IRenderEngine *&engine, int &priority) {
        const std::string &uri = settings.value(XMLProject::kSettingURIKey);
        if (ModelProxy *modelProxy = m_projectRef->loadModel(QUrl::fromLocalFile(QString::fromStdString(uri)), QUuid(QString::fromStdString(uuid)), true)) {
            m_projectRef->addModel(modelProxy, settings.value("selected") == "true");
            priority = XMLProject::toIntFromString(settings.value(XMLProject::kSettingOrderKey));;
            /* upload render engine later */
            model = modelProxy->data();
        }
        engine = 0;
        return model;
    }
    ProjectProxy *m_projectRef;
};

class LoadingModelTask : public QRunnable {
public:
    LoadingModelTask(const Factory *factoryRef, const QUrl &fileUrl)
        : m_factoryRef(factoryRef),
          m_fileUrl(fileUrl),
          m_result(false),
          m_running(true)
    {
        setAutoDelete(false);
    }

    IModel *takeModel() { return m_model.take(); }
    QString errorString() const { return m_errorString; }
    bool isRunning() const { return m_running; }

private:
    void run() {
        QFile file(m_fileUrl.toLocalFile());
        if (file.open(QFile::ReadOnly)) {
            const QByteArray &bytes = file.readAll();
            const uint8_t *ptr = reinterpret_cast<const uint8_t *>(bytes.constData());
            m_model.reset(m_factoryRef->createModel(ptr, file.size(), m_result));
            if (m_result) {
                /* set filename of the model if the name of the model is null such as asset */
                if (!m_model->name(IEncoding::kDefaultLanguage)) {
                    const icu4c::String s(Util::fromQString(QFileInfo(file.fileName()).fileName()));
                    m_model->setName(&s, IEncoding::kDefaultLanguage);
                }
            }
            else {
                m_errorString = QApplication::tr("Cannot load model %1: %2").arg(m_fileUrl.toDisplayString()).arg(m_model->error());
            }
        }
        else {
            m_errorString = QApplication::tr("Cannot open model %1: %2").arg(m_fileUrl.toDisplayString()).arg(file.errorString());
        }
        m_running = false;
    }

    const Factory *m_factoryRef;
    const QUrl m_fileUrl;
    QScopedPointer<IModel> m_model;
    QString m_errorString;
    bool m_result;
    volatile bool m_running;
};

class LoadingMotionTask : public QRunnable {
public:
    LoadingMotionTask(const ModelProxy *modelProxy, const Factory *factoryRef, const QUrl &fileUrl)
        : m_modelProxy(modelProxy),
          m_factoryRef(factoryRef),
          m_fileUrl(fileUrl),
          m_result(false),
          m_running(true)
    {
        setAutoDelete(false);
    }

    IMotion *takeMotion() { return m_motion.take(); }
    QString errorString() const { return m_errorString; }
    bool isRunning() const { return m_running; }

private:
    void run() {
        if (m_modelProxy) {
            QFile file(m_fileUrl.toLocalFile());
            if (file.open(QFile::ReadOnly)) {
                const QByteArray &bytes = file.readAll();
                const uint8_t *ptr = reinterpret_cast<const uint8_t *>(bytes.constData());
                m_motion.reset(m_factoryRef->createMotion(ptr, file.size(), m_modelProxy->data(), m_result));
                if (!m_result) {
                    m_errorString = QApplication::tr("Cannot load motion %1").arg(m_fileUrl.toDisplayString());
                }
            }
            else {
                m_errorString = QApplication::tr("Cannot open motion %1: %2").arg(m_fileUrl.toDisplayString()).arg(file.errorString());
            }
        }
        else {
            m_errorString = QApplication::tr("Current model is not set. You should select the model to load a motion.");
        }
        m_running = false;
    }

    const ModelProxy *m_modelProxy;
    const Factory *m_factoryRef;
    const QUrl m_fileUrl;
    QScopedPointer<IMotion> m_motion;
    QString m_errorString;
    bool m_result;
    volatile bool m_running;
};

class SynchronizedBoneMotionState : public btMotionState {
public:
    SynchronizedBoneMotionState(const IBone *boneRef)
        : m_boneRef(boneRef)
    {
    }
    ~SynchronizedBoneMotionState() {
        m_boneRef = 0;
    }

    void getWorldTransform(btTransform &worldTrans) const {
        worldTrans = m_boneRef->worldTransform();
    }
    void setWorldTransform(const btTransform & /* worldTrans */) {
    }

private:
    const IBone *m_boneRef;
};

}

ProjectProxy::ProjectProxy(QObject *parent)
    : QObject(parent),
      m_encoding(new icu4c::Encoding(&m_dictionary)),
      m_factory(new Factory(m_encoding.data())),
      m_sceneWorld(new World()),
      m_modelWorld(new World()),
      m_delegate(new ProjectDelegate(this)),
      m_project(new XMLProject(m_delegate.data(), m_factory.data(), false)),
      m_cameraRefObject(new CameraRefObject(this)),
      m_lightRefObject(new LightRefObject(this)),
      m_undoGroup(new QUndoGroup()),
      m_currentModelRef(0),
      m_currentMotionRef(0),
      m_nullLabel(new QObject(this)),
      m_currentTimeIndex(0),
      m_language(DefaultLauguage),
      m_enablePhysicsSimulation(false)
{
    QMap<QString, IEncoding::ConstantType> str2const;
    str2const.insert("arm", IEncoding::kArm);
    str2const.insert("asterisk", IEncoding::kAsterisk);
    str2const.insert("center", IEncoding::kCenter);
    str2const.insert("elbow", IEncoding::kElbow);
    str2const.insert("finger", IEncoding::kFinger);
    str2const.insert("left", IEncoding::kLeft);
    str2const.insert("leftknee", IEncoding::kLeftKnee);
    str2const.insert("opacity", IEncoding::kOpacityMorphAsset);
    str2const.insert("right", IEncoding::kRight);
    str2const.insert("rightknee", IEncoding::kRightKnee);
    str2const.insert("root", IEncoding::kRootBone);
    str2const.insert("scale", IEncoding::kScaleBoneAsset);
    str2const.insert("spaextension", IEncoding::kSPAExtension);
    str2const.insert("sphextension", IEncoding::kSPHExtension);
    str2const.insert("wrist", IEncoding::kWrist);
    QMapIterator<QString, IEncoding::ConstantType> it(str2const);
    QSettings settings(":data/words.dic", QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    while (it.hasNext()) {
        it.next();
        const QVariant &value = settings.value("constants." + it.key());
        m_dictionary.insert(it.value(), new icu4c::String(Util::fromQString(value.toString())));
    }
    assignCamera();
    assignLight();
    connect(this, &ProjectProxy::currentModelChanged, this, &ProjectProxy::updateParentBindingModel);
    connect(this, &ProjectProxy::parentBindingDidUpdate, this, &ProjectProxy::availableParentBindingBonesChanged);
    connect(this, &ProjectProxy::parentBindingDidUpdate, this, &ProjectProxy::availableParentBindingModelsChanged);
    connect(m_undoGroup.data(), &QUndoGroup::canUndoChanged, this, &ProjectProxy::canUndoChanged);
    connect(m_undoGroup.data(), &QUndoGroup::canRedoChanged, this, &ProjectProxy::canRedoChanged);
    QScopedPointer<btStaticPlaneShape> ground(new btStaticPlaneShape(Vector3(0, 1, 0), 0));
    btRigidBody::btRigidBodyConstructionInfo info(0, 0, ground.take(), kZeroV3);
    m_groundBody.reset(new btRigidBody(info));
    m_sceneWorld->dynamicWorldRef()->addRigidBody(m_groundBody.data(), 0x10, 0);
    m_nullLabel->setProperty("name", tr("None"));
}

ProjectProxy::~ProjectProxy()
{
    m_dictionary.releaseAll();
    m_sceneWorld->removeRigidBody(m_groundBody.data());
    /* explicitly release XMLProject (Scene) instance to invalidation of Effect correctly before destorying RenderContext */
    release();
    /* explicitly release World instance to ensure release btRigidBody */
    m_sceneWorld.reset();
}

bool ProjectProxy::create()
{
    emit projectWillCreate();
    release();
    setDirty(false);
    createProjectInstance();
    assignCamera();
    assignLight();
    emit projectDidCreate();
    return true;
}

bool ProjectProxy::load(const QUrl &fileUrl)
{
    Q_ASSERT(fileUrl.isValid());
    emit projectWillLoad();
    release();
    createProjectInstance();
    bool result = m_project->load(fileUrl.toLocalFile().toUtf8().constData());
    assignCamera();
    assignLight();
    Array<IMotion *> motionRefs;
    m_project->getMotionRefs(motionRefs);
    const int nmotions = motionRefs.count();
    /* Override model motions to the project holds */
    for (int i = 0; i < nmotions; i++) {
        IMotion *motionRef = motionRefs[i];
        const XMLProject::UUID &uuid = m_project->motionUUID(motionRef);
        MotionProxy *motionProxy = resolveMotionProxy(motionRef);
        if (motionProxy) {
            /* remove previous (initial) model motion */
            deleteMotion(motionProxy);
        }
        else {
            /* call setCurrentMotion to paint timeline correctly */
            motionProxy = createMotionProxy(motionRef, QUuid(QString::fromStdString(uuid)), QUrl(), false);
            IModel *modelRef = motionRef->parentModelRef();
            ModelProxy *modelProxy = resolveModelProxy(modelRef);
            Q_ASSERT(modelProxy);
            modelProxy->setChildMotion(motionProxy);
            if (m_project->modelSetting(modelRef, "selected") == "true") {
                setCurrentMotion(motionProxy);
            }
        }
    }
    if (m_project->globalSetting("title").empty()) {
        setTitle(QFileInfo(fileUrl.toLocalFile()).fileName());
    }
    setDirty(false);
    emit projectDidLoad();
    return result;
}

bool ProjectProxy::save(const QUrl &fileUrl)
{
    Q_ASSERT(fileUrl.isValid());
    emit projectWillSave();
    bool saved = false, committed = false;
    if (m_currentModelRef) {
        m_project->setModelSetting(m_currentModelRef->data(), "selected", "true");
    }
    QTemporaryFile temp;
    if (temp.open()) {
        saved = m_project->save(temp.fileName().toUtf8().constData());
        QSaveFile saveFile(fileUrl.toLocalFile());
        if (saveFile.open(QFile::WriteOnly | QFile::Unbuffered)) {
            saveFile.write(temp.readAll());
            committed = saveFile.commit();
        }
    }
    setDirty(false);
    emit projectDidSave();
    return saved && committed;
}

ModelProxy *ProjectProxy::findModel(const QUuid &uuid)
{
    return m_uuid2ModelProxyRefs.value(uuid);
}

MotionProxy *ProjectProxy::findMotion(const QUuid &uuid)
{
    return m_uuid2MotionProxyRefs.value(uuid);
}

bool ProjectProxy::loadModel(const QUrl &fileUrl)
{
    Q_ASSERT(fileUrl.isValid());
    const QUuid &uuid = QUuid::createUuid();
    ModelProxy *modelProxy = loadModel(fileUrl, uuid, false);
    return modelProxy;
}

void ProjectProxy::addModel(ModelProxy *value, bool selected)
{
    Q_ASSERT(value);
    m_modelProxies.append(value);
    m_instance2ModelProxyRefs.insert(value->data(), value);
    m_uuid2ModelProxyRefs.insert(value->uuid(), value);
    if (selected) {
        setCurrentModel(value);
    }
    setDirty(true);
    connect(this, &ProjectProxy::modelBoneDidPick, value, &ModelProxy::selectBone);
    emit modelDidAdd(value);
    const QUuid &uuid = QUuid::createUuid();
    VPVL2_VLOG(1, "The initial motion of the model " << value->name().toStdString() << " will be allocated as " << uuid.toString().toStdString());
    if (MotionProxy *motionProxy = createMotionProxy(createInitialModelMotion(value->data()), uuid, QUrl(), false)) {
        value->setChildMotion(motionProxy);
        emit motionDidLoad(motionProxy);
    }
}

bool ProjectProxy::deleteModel(ModelProxy *value)
{
    if (value && m_instance2ModelProxyRefs.contains(value->data())) {
        emit modelWillRemove(value);
        if (m_currentModelRef == value) {
            value->resetTargets();
            setCurrentModel(0);
        }
        deleteMotion(value->childMotion());
        IModel *modelRef = value->data();
        modelRef->leaveWorld(m_sceneWorld->dynamicWorldRef());
        setDirty(true);
        m_modelProxies.removeOne(value);
        m_instance2ModelProxyRefs.remove(modelRef);
        m_uuid2ModelProxyRefs.remove(value->uuid());
        emit modelDidRemove(value);
        return true;
    }
    else {
        setErrorString(tr("Current model is not set or not found."));
        return false;
    }
}

bool ProjectProxy::loadMotion(const QUrl &fileUrl, ModelProxy *modelProxy)
{
    Q_ASSERT(fileUrl.isValid() && modelProxy);
    QScopedPointer<LoadingMotionTask> task(new LoadingMotionTask(modelProxy, m_factory.data(), fileUrl));
    QThreadPool::globalInstance()->start(task.data());
    while (task->isRunning()) {
        qApp->processEvents(QEventLoop::AllEvents);
    }
    m_errorString.clear();
    if (IMotion *motion = task->takeMotion()) {
        const QUuid &uuid = QUuid::createUuid();
        VPVL2_VLOG(1, "The motion of the model " << modelProxy->name().toStdString() << " from " << fileUrl.toString().toStdString() << " will be allocated as " << uuid.toString().toStdString());
        deleteMotion(modelProxy->childMotion());
        createMotionProxy(motion, uuid, fileUrl, true);
    }
    else {
        setErrorString(task->errorString());
    }
    return modelProxy;
}

bool ProjectProxy::loadPose(const QUrl &fileUrl, ModelProxy *modelProxy)
{
    bool result = false;
    if (modelProxy) {
        QFile file(fileUrl.toLocalFile());
        if (file.open(QFile::ReadOnly)) {
            Pose pose(m_encoding.data());
            QByteArray bytes = file.readAll();
            std::istringstream stream(bytes.constData());
            if (pose.load(stream)) {
                IModel *modelRef = modelProxy->data();
                pose.bind(modelRef);
                emit poseDidLoad(modelProxy);
                result = true;
            }
            else {
                setErrorString(tr("Cannot load pose %1").arg(fileUrl.toDisplayString()));
            }
        }
        else {
            setErrorString(tr("Cannot open pose %1: %2").arg(fileUrl.toDisplayString()).arg(file.errorString()));
        }
    }
    else {
        setErrorString(tr("Current model is not set. You should select the model to load a pose."));
    }
    return result;
}

void ProjectProxy::seek(qreal timeIndex)
{
    seekInternal(timeIndex, false);
}

void ProjectProxy::rewind()
{
    seekInternal(0, true);
}

void ProjectProxy::refresh()
{
    foreach (MotionProxy *motionProxy, m_motionProxies) {
        motionProxy->applyParentModel();
    }
    m_cameraRefObject->refresh();
}

void ProjectProxy::ray(qreal x, qreal y, int width, int height)
{
    // This implementation based on the below page.
    // http://softwareprodigy.blogspot.com/2009/08/gluunproject-for-iphone-opengl-es.html
    const glm::vec2 win(x, height - y);
    const glm::vec4 viewport(0, 0, width, height);
    const ICamera *camera = m_project->cameraRef();
    const Transform &transform = camera->modelViewTransform();
    float m[16];
    transform.getOpenGLMatrix(m);
    const glm::mat4 &worldView = glm::make_mat4(m);
    const glm::mat4 &projection = glm::perspective(camera->fov(), width / glm::mediump_float(height), 0.1f, 100000.0f);
    const glm::vec3 &cnear = glm::unProject(glm::vec3(win, 0), worldView, projection, viewport);
    const glm::vec3 &cfar = glm::unProject(glm::vec3(win, 1), worldView, projection, viewport);
    const Vector3 from(cnear.x, cnear.y, cnear.z), to(cfar.x, cfar.y, cfar.z);
    btCollisionWorld::ClosestRayResultCallback callback(from, to);
    btDiscreteDynamicsWorld *worldRef = m_modelWorld->dynamicWorldRef();
    worldRef->stepSimulation(1);
    worldRef->rayTest(from, to, callback);
    if (callback.hasHit()) {
        BoneRefObject *bone = static_cast<BoneRefObject *>(callback.m_collisionObject->getUserPointer());
        m_currentModelRef->selectBone(bone);
    }
}

void ProjectProxy::undo()
{
    if (m_undoGroup->canUndo()) {
        m_undoGroup->undo();
        emit undoDidPerform();
    }
}

void ProjectProxy::redo()
{
    if (m_undoGroup->canRedo()) {
        m_undoGroup->redo();
        emit redoDidPerform();
    }
}

IMotion *ProjectProxy::createInitialModelMotion(IModel *model)
{
    QScopedPointer<IMotion> motion(m_factory->newMotion(IMotion::kVMDMotion, model));
    Array<IBone *> boneRefs;
    model->getBoneRefs(boneRefs);
    const int nbones = boneRefs.count();
    for (int i = 0; i < nbones; i++) {
        IBone *boneRef = boneRefs[i];
        QScopedPointer<IBoneKeyframe> keyframe(m_factory->createBoneKeyframe(motion.data()));
        keyframe->setDefaultInterpolationParameter();
        keyframe->setTimeIndex(0);
        keyframe->setLocalOrientation(boneRef->localOrientation());
        keyframe->setLocalTranslation(boneRef->localTranslation());
        keyframe->setName(boneRef->name(IEncoding::kDefaultLanguage));
        motion->addKeyframe(keyframe.take());
    }
    motion->update(IKeyframe::kBoneKeyframe);
    Array<IMorph *> morphRefs;
    model->getMorphRefs(morphRefs);
    const int nmorphs = morphRefs.count();
    for (int i = 0; i < nmorphs; i++) {
        IMorph *morphRef = morphRefs[i];
        QScopedPointer<IMorphKeyframe> keyframe(m_factory->createMorphKeyframe(motion.data()));
        keyframe->setTimeIndex(0);
        keyframe->setWeight(morphRef->weight());
        keyframe->setName(morphRef->name(IEncoding::kDefaultLanguage));
        motion->addKeyframe(keyframe.take());
    }
    motion->update(IKeyframe::kMorphKeyframe);
    return motion.take();
}

void ProjectProxy::update(int flags)
{
    m_project->update(flags);
}

QList<ModelProxy *> ProjectProxy::modelProxies() const
{
    return m_modelProxies;
}

QList<MotionProxy *> ProjectProxy::motionProxies() const
{
    return m_motionProxies;
}

QQmlListProperty<ModelProxy> ProjectProxy::availableModels()
{
    return QQmlListProperty<ModelProxy>(this, m_modelProxies);
}

QQmlListProperty<MotionProxy> ProjectProxy::availableMotions()
{
    return QQmlListProperty<MotionProxy>(this, m_motionProxies);
}

QQmlListProperty<QObject> ProjectProxy::availableParentBindingModels()
{
    return QQmlListProperty<QObject>(this, m_parentModelProxyRefs);
}

QQmlListProperty<QObject> ProjectProxy::availableParentBindingBones()
{
    return QQmlListProperty<QObject>(this, m_parentModelBoneRefs);
}

QString ProjectProxy::title() const
{
    return m_title;
}

void ProjectProxy::setTitle(const QString &value)
{
    if (value != m_title) {
        m_project->setGlobalSetting("title", value.toStdString());
        m_title = value;
        emit titleChanged();
    }
}

ModelProxy *ProjectProxy::currentModel() const
{
    return m_currentModelRef;
}

void ProjectProxy::setCurrentModel(ModelProxy *value)
{
    if (value != m_currentModelRef) {
        btDiscreteDynamicsWorld *world = m_modelWorld->dynamicWorldRef();
        const int numCollidables = world->getNumCollisionObjects();
        for (int i = numCollidables - 1; i >= 0; i--) {
            btCollisionObject *object = world->getCollisionObjectArray().at(i);
            if (btRigidBody *body = btRigidBody::upcast(object)) {
                world->removeRigidBody(body);
                delete body->getMotionState();
            }
            else {
                world->removeCollisionObject(object);
            }
            delete object->getCollisionShape();
            delete object;
        }
        if (value) {
            foreach (BoneRefObject *bone, value->allBoneRefs()) {
                const IBone *boneRef = bone->data();
                if (boneRef->isInteractive()) {
                    QScopedPointer<btSphereShape> shape(new btSphereShape(0.5));
                    QScopedPointer<btMotionState> state(new SynchronizedBoneMotionState(boneRef));
                    btRigidBody::btRigidBodyConstructionInfo info(0, state.take(), shape.take(), kZeroV3);
                    QScopedPointer<btRigidBody> body(new btRigidBody(info));
                    body->setActivationState(DISABLE_DEACTIVATION);
                    body->setCollisionFlags(body->getCollisionFlags() | btRigidBody::CF_KINEMATIC_OBJECT);
                    body->setUserPointer(bone);
                    world->addRigidBody(body.take());
                }
            }
        }
        m_currentModelRef = value;
        emit currentModelChanged();
    }
}

MotionProxy *ProjectProxy::currentMotion() const
{
    return m_currentMotionRef;
}

void ProjectProxy::setCurrentMotion(MotionProxy *value)
{
    if (value != m_currentMotionRef) {
        QUndoStack *stack = m_motion2UndoStacks.value(value);
        m_undoGroup->setActiveStack(stack);
        m_currentMotionRef = value;
        emit currentMotionChanged();
    }
}

ProjectProxy::LanguageType ProjectProxy::language() const
{
    return m_language;
}

void ProjectProxy::setLanguage(LanguageType value)
{
    if (value != m_language) {
        value = m_language;
        emit languageChanged();
    }
}

bool ProjectProxy::isDirty() const
{
    return m_project->isDirty();
}

void ProjectProxy::setDirty(bool value)
{
    if (isDirty() != value) {
        m_project->setDirty(value);
        emit dirtyChanged();
    }
}

bool ProjectProxy::isPhysicsSimulationEnabled() const
{
    return m_enablePhysicsSimulation;
}

void ProjectProxy::setPhysicsSimulationEnabled(bool value)
{
    if (value != m_enablePhysicsSimulation) {
        foreach (ModelProxy *modelProxy, m_modelProxies) {
            modelProxy->data()->setPhysicsEnable(value);
        }
        m_project->setWorldRef(value ? m_sceneWorld->dynamicWorldRef() : 0);
        m_enablePhysicsSimulation = value;
        emit enablePhysicsSimulationChanged();
    }
}

bool ProjectProxy::canUndo() const
{
    return m_undoGroup->canUndo();
}

bool ProjectProxy::canRedo() const
{
    return m_undoGroup->canRedo();
}

qreal ProjectProxy::differenceTimeIndex(qreal value) const
{
    static const IKeyframe::TimeIndex kZero = 0;
    return m_project ? qMax(m_project->duration() - qMax(value, kZero), kZero) : 0;
}

qreal ProjectProxy::differenceDuration(qreal value) const
{
    return millisecondsFromTimeIndex(differenceTimeIndex(value));
}

qreal ProjectProxy::secondsFromTimeIndex(qreal value) const
{
    return value / Scene::defaultFPS();
}

qreal ProjectProxy::millisecondsFromTimeIndex(qreal value) const
{
    return value * Scene::defaultFPS();
}

void ProjectProxy::resetBone(BoneRefObject *bone, ResetBoneType type)
{
    if (bone) {
        ModelProxy *modelProxy = bone->parentLabel()->parentModel();
        if (MotionProxy *motionProxy = modelProxy->childMotion()) {
            const QVector3D &translationToReset = bone->originLocalTranslation();
            const QQuaternion &orientationToReset = bone->originLocalOrientation();
            QVector3D translation = bone->localTranslation();
            QQuaternion orientation = bone->localOrientation();
            switch (type) {
            case TranslationAxisX:
                translation.setX(translationToReset.x());
                break;
            case TranslationAxisY:
                translation.setY(translationToReset.y());
                break;
            case TranslationAxisZ:
                translation.setZ(translationToReset.z());
                break;
            case TranslationAxisXYZ:
                translation = translationToReset;
                break;
            case Orientation:
                orientation = orientationToReset;
                break;
            case AllTranslationAndOrientation:
                translation = translationToReset;
                orientation = orientationToReset;
                break;
            }
            bone->setLocalTranslation(translation);
            bone->setLocalOrientation(orientation);
            resetIKEffectorBones(bone);
            motionProxy->updateKeyframe(bone, static_cast<qint64>(m_currentTimeIndex));
            VPVL2_VLOG(2, "reset TYPE=BONE name=" << bone->name().toStdString() << " type=" << type);
        }
    }
    else if (m_currentModelRef) {
        if (MotionProxy *motionProxy = m_currentModelRef->childMotion()) {
            QScopedPointer<QUndoCommand> command(new QUndoCommand());
            foreach (BoneRefObject *boneRef, m_currentModelRef->allBoneRefs()) {
                boneRef->setLocalTranslation(boneRef->originLocalTranslation());
                boneRef->setLocalOrientation(boneRef->originLocalOrientation());
                resetIKEffectorBones(boneRef);
                motionProxy->updateKeyframe(boneRef, static_cast<qint64>(m_currentTimeIndex), command.data());
            }
            if (QUndoStack *stack = m_undoGroup->activeStack()) {
                stack->push(command.take());
                VPVL2_VLOG(2, "resetAll TYPE=BONE");
            }
        }
    }
}

void ProjectProxy::resetMorph(MorphRefObject *morph)
{
    if (morph) {
        ModelProxy *modelProxy = morph->parentLabel()->parentModel();
        if (MotionProxy *motionProxy = modelProxy->childMotion()) {
            morph->setWeight(morph->originWeight());
            motionProxy->updateKeyframe(morph, static_cast<qint64>(m_currentTimeIndex));
            VPVL2_VLOG(2, "reset TYPE=MORPH name=" << morph->name().toStdString());
        }
    }
    else if (m_currentModelRef) {
        if (MotionProxy *motionProxy = m_currentModelRef->childMotion()) {
            QScopedPointer<QUndoCommand> command(new QUndoCommand());
            foreach (MorphRefObject *morphRef, m_currentModelRef->allMorphRefs()) {
                morphRef->setWeight(morphRef->originWeight());
                motionProxy->updateKeyframe(morphRef, static_cast<qint64>(m_currentTimeIndex), command.data());
            }
            if (QUndoStack *stack = m_undoGroup->activeStack()) {
                stack->push(command.take());
                VPVL2_VLOG(2, "resetAll TYPE=MORPH");
            }
        }
    }
}

void ProjectProxy::updateParentBindingModel()
{
    m_parentModelProxyRefs.clear();
    m_parentModelProxyRefs.append(m_nullLabel);
    foreach (ModelProxy *modelProxy, m_modelProxies) {
        if (modelProxy != m_currentModelRef) {
            m_parentModelProxyRefs.append(modelProxy);
        }
    }
    m_parentModelBoneRefs.clear();
    m_parentModelBoneRefs.append(m_nullLabel);
    if (m_currentModelRef) {
        if (const ModelProxy *parentModel = m_currentModelRef->parentBindingModel()) {
            foreach (BoneRefObject *boneRef, parentModel->allBoneRefs()) {
                m_parentModelBoneRefs.append(boneRef);
            }
        }
    }
    emit parentBindingDidUpdate();
}

ModelProxy *ProjectProxy::loadModel(const QUrl &fileUrl, const QUuid &uuid, bool skipConfirm)
{
    ModelProxy *modelProxy = 0;
    QScopedPointer<LoadingModelTask> task(new LoadingModelTask(m_factory.data(), fileUrl));
    QThreadPool::globalInstance()->start(task.data());
    while (task->isRunning()) {
        qApp->processEvents(QEventLoop::AllEvents);
    }
    if (IModel *model = task->takeModel()) {
        modelProxy = createModelProxy(model, uuid, fileUrl, skipConfirm);
    }
    else {
        setErrorString(task->errorString());
    }
    return modelProxy;
}

ModelProxy *ProjectProxy::createModelProxy(IModel *model, const QUuid &uuid, const QUrl &fileUrl, bool skipConfirm)
{
    QUrl faviconUrl;
    const QFileInfo finfo(fileUrl.toLocalFile());
    QStringList filters; filters << "favicon.*";
    const QStringList &faviconLocations = finfo.absoluteDir().entryList(filters);
    if (!faviconLocations.isEmpty()) {
        faviconUrl = QUrl::fromLocalFile(finfo.absoluteDir().filePath(faviconLocations.first()));
    }
    ModelProxy *modelProxy = new ModelProxy(this, model, uuid, fileUrl, faviconUrl);
    emit modelWillLoad(modelProxy);
#if 0
    QFuture<bool> future = QtConcurrent::run(InitializeModel, modelProxy);
    while (!future.isResultReadyAt(0)) {
        qApp->processEvents(QEventLoop::AllEvents);
    }
#endif
    modelProxy->initialize();
    emit modelDidLoad(modelProxy, skipConfirm);
    return modelProxy;
}

MotionProxy *ProjectProxy::createMotionProxy(IMotion *motion, const QUuid &uuid, const QUrl &fileUrl, bool emitSignal)
{
    MotionProxy *motionProxy = 0;
    if (motion && !resolveMotionProxy(motion)) {
        QUndoStack *undoStack = new QUndoStack(m_undoGroup.data());
        motionProxy = new MotionProxy(this, motion, uuid, fileUrl, undoStack);
        if (emitSignal) {
            emit motionWillLoad(motionProxy);
        }
#if 0
        QFuture<bool> future = QtConcurrent::run(InitializeMotion, motionProxy);
        while (!future.isResultReadyAt(0)) {
            qApp->processEvents(QEventLoop::AllEvents);
        }
#endif
        motionProxy->initialize();
        setDirty(true);
        m_undoGroup->addStack(undoStack);
        m_project->addMotion(motion, uuid.toString().toStdString());
        m_motionProxies.append(motionProxy);
        m_motion2UndoStacks.insert(motionProxy, undoStack);
        m_instance2MotionProxyRefs.insert(motion, motionProxy);
        m_uuid2MotionProxyRefs.insert(uuid, motionProxy);
        if (emitSignal) {
            emit motionDidLoad(motionProxy);
        }
    }
    return motionProxy;
}

ModelProxy *ProjectProxy::resolveModelProxy(const IModel *value) const
{
    return m_instance2ModelProxyRefs.value(value);
}

MotionProxy *ProjectProxy::resolveMotionProxy(const IMotion *value) const
{
    return m_instance2MotionProxyRefs.value(value);
}

void ProjectProxy::deleteMotion(MotionProxy *value)
{
    if (value && m_uuid2MotionProxyRefs.contains(value->uuid())) {
        emit motionWillDelete(value);
        if (m_currentMotionRef == value) {
            setCurrentMotion(0);
        }
        if (ModelProxy *modelProxy = value->parentModel()) {
            modelProxy->setChildMotion(0);
            value->data()->setParentModelRef(0);
        }
        m_undoGroup->removeStack(m_motion2UndoStacks.value(value));
        m_motion2UndoStacks.remove(value);
        m_motionProxies.removeOne(value);
        m_instance2MotionProxyRefs.remove(value->data());
        m_uuid2MotionProxyRefs.remove(value->uuid());
        m_project->removeMotion(value->data());
        delete value;
    }
}

qreal ProjectProxy::currentTimeIndex() const
{
    return m_currentTimeIndex;
}

qreal ProjectProxy::durationTimeIndex() const
{
    return differenceTimeIndex(0);
}

qreal ProjectProxy::durationMilliseconds() const
{
    return differenceDuration(0);
}

QString ProjectProxy::errorString() const
{
    return m_errorString;
}

CameraRefObject *ProjectProxy::camera() const
{
    return m_cameraRefObject.data();
}

LightRefObject *ProjectProxy::light() const
{
    return m_lightRefObject.data();
}

IEncoding *ProjectProxy::encodingInstanceRef() const
{
    return m_encoding.data();
}

Factory *ProjectProxy::factoryInstanceRef() const
{
    return m_factory.data();
}

XMLProject *ProjectProxy::projectInstanceRef() const
{
    return m_project.data();
}

void ProjectProxy::createProjectInstance()
{
    m_project.reset(new XMLProject(m_delegate.data(), m_factory.data(), false));
    if (m_enablePhysicsSimulation) {
        m_project->setWorldRef(m_sceneWorld->dynamicWorldRef());
    }
}

void ProjectProxy::resetIKEffectorBones(BoneRefObject *bone)
{
    const IBone *boneRef = bone->data();
    if (boneRef->hasInverseKinematics()) {
        Array<IBone *> effectorBones;
        boneRef->getEffectorBones(effectorBones);
        const int numEffectorBones = effectorBones.count();
        for (int i = 0; i < numEffectorBones; i++) {
            IBone *effectorBone = effectorBones[i];
            effectorBone->setLocalTranslation(kZeroV3);
            effectorBone->setLocalOrientation(Quaternion::getIdentity());
        }
    }
}

void ProjectProxy::assignCamera()
{
    ICamera *cameraRef = m_project->cameraRef();
    QUuid uuid = QUuid::createUuid();
    QUndoStack *undoStack = new QUndoStack(m_undoGroup.data());
    QScopedPointer<IMotion> motion(m_factory->newMotion(IMotion::kVMDMotion, 0));
    VPVL2_VLOG(1, "The camera motion will be allocated as " << uuid.toString().toStdString());
    MotionProxy *motionProxy = new MotionProxy(this, motion.data(), uuid, QUrl(), undoStack);
    m_cameraRefObject->assignCameraRef(cameraRef, motionProxy);
    QScopedPointer<ICameraKeyframe> keyframe(m_factory->createCameraKeyframe(motion.take()));
    keyframe->setDefaultInterpolationParameter();
    keyframe->setAngle(cameraRef->angle());
    keyframe->setDistance(cameraRef->distance());
    keyframe->setFov(cameraRef->fov());
    keyframe->setLookAt(cameraRef->lookAt());
    CameraMotionTrack *track = m_cameraRefObject->track();
    track->addKeyframe(track->convertCameraKeyframe(keyframe.take()), true);
}

void ProjectProxy::assignLight()
{
    ILight *lightRef = m_project->lightRef();
    QUuid uuid = QUuid::createUuid();
    QUndoStack *undoStack = new QUndoStack(m_undoGroup.data());
    QScopedPointer<IMotion> motion(m_factory->newMotion(IMotion::kVMDMotion, 0));
    VPVL2_VLOG(1, "The light motion will be allocated as " << uuid.toString().toStdString());
    MotionProxy *motionProxy = new MotionProxy(this, motion.data(), uuid, QUrl(), undoStack);
    m_lightRefObject->assignLightRef(lightRef, motionProxy);
    QScopedPointer<ILightKeyframe> keyframe(m_factory->createLightKeyframe(motion.take()));
    keyframe->setColor(lightRef->color());
    keyframe->setDirection(lightRef->direction());
    LightMotionTrack *track = m_lightRefObject->track();
    track->addKeyframe(track->convertLightKeyframe(keyframe.take()), true);
}

void ProjectProxy::seekInternal(const qreal &timeIndex, bool forceUpdate)
{
    if (forceUpdate || !qFuzzyCompare(timeIndex, m_currentTimeIndex)) {
        m_project->seek(timeIndex, Scene::kUpdateAll);
        if (m_enablePhysicsSimulation) {
            m_sceneWorld->dynamicWorldRef()->stepSimulation(1);
        }
        if (m_currentModelRef) {
            updateOriginValues();
            foreach (BoneRefObject *bone, m_currentModelRef->allTargetBones()) {
                bone->sync();
            }
            if (MorphRefObject *morph = m_currentModelRef->firstTargetMorph()) {
                morph->sync();
            }
        }
        m_cameraRefObject->refresh();
        m_lightRefObject->refresh();
        m_currentTimeIndex = timeIndex;
        emit currentTimeIndexChanged();
    }
}

void ProjectProxy::updateOriginValues()
{
    Q_ASSERT(m_currentModelRef);
    if (MotionProxy *motionProxy = m_currentModelRef->childMotion()) {
        foreach (BoneRefObject *bone, m_currentModelRef->allBoneRefs()) {
            if (const BoneMotionTrack *track = motionProxy->findBoneMotionTrack(bone)) {
                if (BaseKeyframeRefObject *keyframe = track->findKeyframeByTimeIndex(static_cast<qint64>(m_currentTimeIndex))) {
                    BoneKeyframeRefObject *boneKeyframeRef = qobject_cast<BoneKeyframeRefObject *>(keyframe);
                    Q_ASSERT(boneKeyframeRef);
                    bone->setOriginLocalTranslation(boneKeyframeRef->localTranslation());
                    bone->setOriginLocalOrientation(boneKeyframeRef->localOrientation());
                }
            }
        }
        foreach (MorphRefObject *morph, m_currentModelRef->allMorphRefs()) {
            if (const MorphMotionTrack *track = motionProxy->findMorphMotionTrack(morph)) {
                if (BaseKeyframeRefObject *keyframe = track->findKeyframeByTimeIndex(static_cast<qint64>(m_currentTimeIndex))) {
                    MorphKeyframeRefObject *morphKeyframeRef = qobject_cast<MorphKeyframeRefObject *>(keyframe);
                    Q_ASSERT(morphKeyframeRef);
                    morph->setOriginWeight(morphKeyframeRef->weight());
                }
            }
        }
    }
}

void ProjectProxy::setErrorString(const QString &value)
{
    if (m_errorString != value) {
        m_errorString = value;
        emit errorStringChanged();
    }
}

void ProjectProxy::reset()
{
    m_currentTimeIndex = 0;
    if (m_currentModelRef) {
        m_currentModelRef->resetTargets();
    }
    setCurrentModel(0);
    setCurrentMotion(0);
}

void ProjectProxy::release()
{
    VPVL2_VLOG(1, "The project will be released");
    reset();
    m_undoGroup.reset(new QUndoGroup());
    /* copy motion proxies because m_modelProxies will be mutated using removeOne */
    QList<ModelProxy *> modelProxies = m_modelProxies;
    foreach (ModelProxy *modelProxy, modelProxies) {
        deleteModel(modelProxy);
    }
    m_modelProxies.clear();
    m_motionProxies.clear();
    m_cameraRefObject->releaseMotion();
    m_lightRefObject->releaseMotion();
    m_project->setWorldRef(0);
    connect(m_undoGroup.data(), &QUndoGroup::canUndoChanged, this, &ProjectProxy::canUndoChanged);
    connect(m_undoGroup.data(), &QUndoGroup::canRedoChanged, this, &ProjectProxy::canRedoChanged);
}
