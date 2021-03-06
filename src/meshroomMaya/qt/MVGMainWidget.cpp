#include "meshroomMaya/qt/MVGMainWidget.hpp"
#include "meshroomMaya/qt/QmlInstantCoding.hpp"
//#include "meshroomMaya/qt/QWheelArea.hpp"
#include "meshroomMaya/maya/MVGMayaUtil.hpp"
#include "meshroomMaya/qt/MVGCameraWrapper.hpp"
#include "meshroomMaya/qt/MVGCameraSetWrapper.hpp"
#include <QFocusEvent>
#include <QQuickWidget>
#include <QQmlEngine>
#include <QQmlContext>

namespace meshroomMaya
{

MVGMainWidget::MVGMainWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("mvgMainWidget");

    qmlRegisterType<MVGCameraWrapper>();
    qmlRegisterType<QObjectListModel>();
    qmlRegisterType<MVGCameraSetWrapper>();

    _view = new QQuickWidget(parent);

    _projectWrapper.loadExistingProject();
    QString importDirectory = QString(MVGMayaUtil::getModulePath().asChar()) + "/qml";

    // QtDesktop Components
    _view->engine()->addPluginPath(importDirectory);
    _view->engine()->addImportPath(importDirectory);

    // Expose Project to QML
    _view->rootContext()->setContextProperty("_project", &_projectWrapper);

    // Qml source
    const char* instantCoding = std::getenv("MESHROOMMAYA_USE_QMLINSTANTCODING");
    QString mainQml = importDirectory + "/mvg/main.qml";
    if(instantCoding)
    {
        QDir qmlFolder = QFileInfo(__FILE__).dir();
        qmlFolder.cd("qml");
        mainQml = QFileInfo(qmlFolder, "main.qml").absoluteFilePath();
        
        QmlInstantCoding* qic = new QmlInstantCoding(_view, true);
        qic->addFilesFromDirectory(qmlFolder.absolutePath(), true);
    }
    _view->setSource(QUrl::fromLocalFile(mainQml));
    _view->setResizeMode(QQuickWidget::SizeRootObjectToView);
}

MVGMainWidget::~MVGMainWidget()
{
    _projectWrapper.clearAndUnloadImageCache();
}

void MVGMainWidget::focusOutEvent(QFocusEvent* event)
{
    event->accept();
    _view->clearFocus();
}

QWidget* MVGMainWidget::getView() const
{
    return _view;
}

} // namespace
