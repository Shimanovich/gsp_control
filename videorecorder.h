#ifndef VIDEORECORDER_H
#define VIDEORECORDER_H

#include <QString>
#include <QFile>
#include <QMutex>
#include <cstdint>

class VideoRecorder
{
public:
    static constexpr qint64 kMaxFileBytes = qint64(1024) * 1024 * 1024; // 1 ГБ

    VideoRecorder() = default;
    ~VideoRecorder();

    bool start(const QString& baseDir);
    void stop();
    bool isRecording() const;

    void write(const uint8_t* data, int size);

    QString directory() const;
    QString currentFileName() const;
    QString currentFilePath() const;
    int fileIndex() const;

private:
    bool openNextFile();

    mutable QMutex m_mutex;
    QString m_baseDir;
    QString m_sessionStamp;
    QString m_dirPath;
    QFile   m_file;
    qint64  m_fileBytes = 0;
    int     m_index = 0;
    bool    m_recording = false;
};

#endif
