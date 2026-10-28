#include "videorecorder.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>

VideoRecorder::~VideoRecorder()
{
    stop();
}

bool VideoRecorder::start(const QString& baseDir)
{
    QMutexLocker lock(&m_mutex);
    if (m_recording)
        return true;

    m_baseDir = baseDir;
    m_sessionStamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    m_dirPath = QDir(m_baseDir).filePath(m_sessionStamp);
    m_index = 0;
    m_fileBytes = 0;

    QDir dir;
    if (!dir.mkpath(m_dirPath)) {
        qWarning() << "VideoRecorder: cannot create" << m_dirPath;
        return false;
    }
    if (!openNextFile())
        return false;

    m_recording = true;
    qDebug() << "VideoRecorder: started" << m_dirPath;
    return true;
}

void VideoRecorder::stop()
{
    QMutexLocker lock(&m_mutex);
    if (m_file.isOpen())
        m_file.close();
    m_recording = false;
    m_fileBytes = 0;
}

bool VideoRecorder::isRecording() const
{
    QMutexLocker lock(&m_mutex);
    return m_recording;
}

void VideoRecorder::write(const uint8_t* data, int size)
{
    if (!data || size <= 0)
        return;

    QMutexLocker lock(&m_mutex);
    if (!m_recording)
        return;

    if (m_fileBytes + size > kMaxFileBytes) {
        if (m_file.isOpen())
            m_file.close();
        if (!openNextFile()) {
            m_recording = false;
            return;
        }
    }

    const qint64 n = m_file.write(reinterpret_cast<const char*>(data), size);
    if (n < 0) {
        qWarning() << "VideoRecorder: write failed" << m_file.errorString();
        m_file.close();
        m_recording = false;
        return;
    }
    m_fileBytes += n;
}

QString VideoRecorder::directory() const
{
    QMutexLocker lock(&m_mutex);
    return m_dirPath;
}

QString VideoRecorder::currentFileName() const
{
    QMutexLocker lock(&m_mutex);
    return QFileInfo(m_file.fileName()).fileName();
}

QString VideoRecorder::currentFilePath() const
{
    QMutexLocker lock(&m_mutex);
    return m_file.fileName();
}

int VideoRecorder::fileIndex() const
{
    QMutexLocker lock(&m_mutex);
    return m_index;
}

bool VideoRecorder::openNextFile()
{
    ++m_index;
    const QString name = QString("%1_%2.h264")
                             .arg(m_sessionStamp)
                             .arg(m_index, 3, 10, QChar('0'));
    const QString path = QDir(m_dirPath).filePath(name);
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "VideoRecorder: cannot open" << path << m_file.errorString();
        return false;
    }
    m_fileBytes = 0;
    qDebug() << "VideoRecorder: file" << path;
    return true;
}
