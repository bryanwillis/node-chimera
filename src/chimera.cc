#include "chimera.h"

WebPage::WebPage(QObject *parent)
    : QWebPage(parent)
{
    m_userAgent = QWebPage::userAgentForUrl(QUrl());
}

void WebPage::javaScriptAlert(QWebFrame *originatingFrame, const QString &msg)
{
    Q_UNUSED(originatingFrame);
    std::cout << "JavaScript alert: " << qPrintable(msg) << std::endl;
}

void WebPage::javaScriptConsoleMessage(const QString &message, int lineNumber, const QString &sourceID)
{
    if (!sourceID.isEmpty())
        std::cout << qPrintable(sourceID) << ":" << lineNumber << " ";
    std::cout << qPrintable(message) << std::endl;
}

bool WebPage::shouldInterruptJavaScript()
{
    QApplication::processEvents(QEventLoop::AllEvents, 42);
    return false;
}

QString WebPage::userAgentForUrl(const QUrl &url) const
{
    Q_UNUSED(url);
    return m_userAgent;
}

Chimera::Chimera(QObject *parent)
    : QObject(parent)
    , m_returnValue(0)
{
    QPalette palette = m_page.palette();
    palette.setBrush(QPalette::Base, Qt::transparent);
    m_page.setPalette(palette);

    connect(m_page.mainFrame(), SIGNAL(javaScriptWindowObjectCleared()), SLOT(inject()));
    connect(&m_page, SIGNAL(loadFinished(bool)), this, SLOT(finish(bool)));

    m_page.settings()->setAttribute(QWebSettings::FrameFlatteningEnabled, true);
    m_page.settings()->setAttribute(QWebSettings::OfflineStorageDatabaseEnabled, true);
    m_page.settings()->setAttribute(QWebSettings::LocalStorageEnabled, true);
    m_page.settings()->setLocalStoragePath(QDesktopServices::storageLocation(QDesktopServices::DataLocation));
    m_page.settings()->setOfflineStoragePath(QDesktopServices::storageLocation(QDesktopServices::DataLocation));

    // Ensure we have document.body.
    m_page.mainFrame()->setHtml("<html><body></body></html>");

    m_page.mainFrame()->setScrollBarPolicy(Qt::Horizontal, Qt::ScrollBarAlwaysOff);
    m_page.mainFrame()->setScrollBarPolicy(Qt::Vertical, Qt::ScrollBarAlwaysOff);
}

QString Chimera::content() const
{
    return m_page.mainFrame()->toHtml();
}

void Chimera::setContent(const QString &content)
{
    m_page.mainFrame()->setHtml(content);
}

void Chimera::setEmbedScript(const QString &jscode)
{
    m_script = jscode;
}

void Chimera::callback(const QString &errorResult, const QString &result)
{
  m_error = errorResult;
  m_result = result;
  m_mutex.unlock();
  m_loading.wakeAll();
}

void Chimera::exit(int code)
{
    m_returnValue = code;
    disconnect(&m_page, SIGNAL(loadFinished(bool)), this, SLOT(finish(bool)));
    QTimer::singleShot(0, qApp, SLOT(quit()));
}

void Chimera::execute()
{
    m_mutex.lock();
    m_page.mainFrame()->evaluateJavaScript(m_script);
}

void Chimera::finish(bool success)
{
    m_loadStatus = success ? "success" : "fail";
    m_page.mainFrame()->evaluateJavaScript(m_script);
}

void Chimera::inject()
{
    m_page.mainFrame()->addToJavaScriptWindowObject("chimera", this);
}

QString Chimera::loadStatus() const
{
    return m_loadStatus;
}

void Chimera::open(const QString &address)
{
    m_page.triggerAction(QWebPage::Stop);
    m_loadStatus = "loading";
    m_mutex.lock();
    m_page.mainFrame()->setUrl(address);
}

void Chimera::wait()
{
  if (m_mutex.tryLock()) {
    m_mutex.unlock();
  } else {
    m_error = "";
    m_result = "";
    m_loading.wait(&m_mutex);
  }
}

bool Chimera::render(const QString &fileName)
{
    QFileInfo fileInfo(fileName);
    QDir dir;
    dir.mkpath(fileInfo.absolutePath());

    if (fileName.toLower().endsWith(".pdf")) {
        QPrinter printer;
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(fileName);
        m_page.mainFrame()->print(&printer);
        return true;
    }

    QSize viewportSize = m_page.viewportSize();
    QSize pageSize = m_page.mainFrame()->contentsSize();
    if (pageSize.isEmpty())
        return false;

    QImage buffer(pageSize, QImage::Format_ARGB32_Premultiplied);
    buffer.fill(Qt::transparent);
    QPainter p(&buffer);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    m_page.setViewportSize(pageSize);
    m_page.mainFrame()->render(&p);
    p.end();
    m_page.setViewportSize(viewportSize);
    return buffer.save(fileName);
}

int Chimera::returnValue() const
{
    return m_returnValue;
}

QString Chimera::getResult()
{
    return m_result;
}

QString Chimera::getError()
{
    return m_error;
}

void Chimera::sleep(int ms)
{
    QTime startTime = QTime::currentTime();
    while (true) {
        QApplication::processEvents(QEventLoop::AllEvents, 25);
        if (startTime.msecsTo(QTime::currentTime()) > ms)
            break;
    }
}

void Chimera::setState(const QString &value)
{
    m_state = value;
}

QString Chimera::state() const
{
    return m_state;
}

void Chimera::setUserAgent(const QString &ua)
{
    m_page.m_userAgent = ua;
}

QString Chimera::userAgent() const
{
    return m_page.m_userAgent;
}

void Chimera::setViewportSize(const QVariantMap &size)
{
    int w = size.value("width").toInt();
    int h = size.value("height").toInt();
    if (w > 0 && h > 0)
        m_page.setViewportSize(QSize(w, h));
}

QVariantMap Chimera::viewportSize() const
{
    QVariantMap result;
    QSize size = m_page.viewportSize();
    result["width"] = size.width();
    result["height"] = size.height();
    return result;
}