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

#include "LicenseWidget.h"

#include <QtGui/QtGui>

LicenseWidget::LicenseWidget(QWidget *parent) :
    QWidget(parent),
    m_text(0)
{
    QVBoxLayout *layout = new QVBoxLayout();
    QLabel *copyrightLabel = new QLabel();
    copyrightLabel->setText(QString("<h3>%1 %2+alpha</h3>"
                                    "Copyright (C) 2009-2011 Nagoya Institute of Technology Department of Computer Science (MMDAgent)<br>"
                                    "Copyright (C) 2010-2011 hkrn (MMDAI and VPVM) All rights reserved.<br><br>%3<br><br>%4")
                            .arg(qApp->applicationName())
                            .arg(qApp->applicationVersion())
                            .arg(tr("MMDAI2 (will be VPVM) is an application to edit or create a motion compatible with MMD ("
                                    "<a href='http://www.geocities.jp/higuchuu4/index.htm'>MikuMikuDance</a> "
                                    "created by Yuu Higuchi). This doesn't intend to be the successor of MMD."))
                            .arg(tr("Below table is a list of libraries MMDAI2 uses. "
                                    "Double click a row to show the license text or open the website")));
    copyrightLabel->setWordWrap(true);
    layout->addWidget(copyrightLabel);
    QTreeView *tree = new QTreeView();
    QAbstractItemModel *model = new QStandardItemModel(0, 3);
    model->setHeaderData(0, Qt::Horizontal, tr("Name"));
    model->setHeaderData(1, Qt::Horizontal, tr("License"));
    model->setHeaderData(2, Qt::Horizontal, tr("Website"));
    connect(tree, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(handleDoubleClick(QModelIndex)));
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setModel(model);
    tree->setEditTriggers(QTreeView::NoEditTriggers);
    layout->addWidget(tree);
    QLabel *aboutIconLabel = new QLabel;
    aboutIconLabel->setText(
                tr("%1's icon is based on <a href='http://innoce.nobody.jp/'>Lat's Miku model</a>.<br>"
                   "MIKU Hatsune and other CV series are product of CRYPTON FUTURE MEDIA, INC.<br>"
                   "VOCALOID is the trademark of YAMAHA Corporation."
                   "").arg(qApp->applicationName()));
    aboutIconLabel->setWordWrap(true);
    aboutIconLabel->setOpenExternalLinks(true);
    aboutIconLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    layout->addWidget(aboutIconLabel);
    setWindowTitle(QString(tr("About %1")).arg(qApp->applicationName()));
    resize(600, 450);
    setLayout(layout);
    m_model = model;

    addLibrary("OpenCV", "New BSD", "http://opencv.willowgarage.com", "OpenCV");
    addLibrary("Open Asset Import Library", "New BSD", "http://assimp.sf.net", "Assimp");
    addLibrary("OpenGL Extension Wranger", "New BSD", "http://glew.sf.net", "GLEW");
    addLibrary("BulletPhysics", "zlib", "http://bulletphysics.org/wordpress/", "Bullet");
    addLibrary("MMDAgent", "New BSD", "http://mmdagent.jp", "MMDAgent");
    addLibrary("libvpvl", "New BSD", "https://github.com/hkrn/MMDAI/", "libvpvl");
}

LicenseWidget::~LicenseWidget()
{
}

void LicenseWidget::addLibrary(const QString &name,
                               const QString &license,
                               const QString &website,
                               const QString &path)
{
    m_path.insert(name, path);
    m_model->insertRow(0);
    m_model->setData(m_model->index(0, 0), name);
    m_model->setData(m_model->index(0, 1), license);
    m_model->setData(m_model->index(0, 2), website);
}

void LicenseWidget::handleDoubleClick(const QModelIndex &index)
{
    QVariant value;
    switch (index.column()) {
    case 0: // name
    case 1: // license
    {
        QString license = m_model->data(index).toString();
        QString name = m_model->data(m_model->index(index.row(), 0)).toString();
        QFile file(QString(":/licenses/%1").arg(m_path[name]));
        if (file.exists() && file.open(QFile::ReadOnly | QFile::Text)) {
            QTextStream stream(&file);
            if (!m_text)
                m_text = new QTextEdit;
            m_text->setWindowTitle(tr("%1's license").arg(name));
            m_text->setReadOnly(true);
            m_text->setHtml(QString("<pre>%1</pre>").arg(stream.readAll()));
            m_text->resize(600, 500);
            m_text->setLineWrapMode(QTextEdit::NoWrap);
            m_text->show();
        }
        break;
    }
    case 2: // website
        value = m_model->data(index);
        QDesktopServices::openUrl(value.toString());
        break;
    }
}

