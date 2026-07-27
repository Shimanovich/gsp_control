#pragma once

// H.264 over RTP/UDP decoder using FFmpeg demuxer (no manual NAL extraction)
// - avformat_open_input("udp://@:port") + av_read_frame
// - FFmpeg handles RTP sequence, FU-A, STAP, parameter sets
// - Same public interface as previous version (compatible with MainWindow)
// - Low-latency flags, error concealment, last-frame keep via queue

#include <QObject>
#include <QDebug>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <queue>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

class udpDec : public QObject
{
    Q_OBJECT

public:
    struct PlayerInitStructure {
        int  udpport      = 5000;
        int  udptimeout   = 50;          // kept for compatibility
        char adapterName[64] = {0};
        int  imageWidth   = 0;
        int  imageHeight  = 0;
        std::queue<AVFrame>* pFrameOutQueue = nullptr;
        HANDLE* pHframeMutex = nullptr;
    };

    explicit udpDec(PlayerInitStructure* param, QObject* parent = nullptr);
    ~udpDec() override;

    void stopThread();
    void on();                          // open input + start decode loop
    void off();                         // stop reading, keep thread alive
    bool state() const { return m_enable.load(); }

    bool startListening();              // kept for API compatibility (now no-op / internal)

private:
    // ---- parameters from MainWindow ----
    uint16_t             m_recudpport = 0;
    char                 m_adapterName[64] = {0};
    int                  m_winWidth  = 0;
    int                  m_winHeight = 0;
    std::queue<AVFrame>* m_frameQueue = nullptr;
    HANDLE*              m_pHframeMutex = nullptr;

    // ---- control ----
    std::atomic<bool> m_active{true};   // thread lifetime
    std::atomic<bool> m_enable{false};  // receiving/decoding enabled
    std::atomic<bool> m_opened{false};  // avformat successfully opened

    std::thread m_decodeThread;
    std::mutex  m_openMutex;            // protect open/close of format context

    // ---- FFmpeg ----
    AVFormatContext*    fmt_ctx     = nullptr;
    AVCodecContext*     codec_ctx   = nullptr;
    const AVCodec*      codec       = nullptr;
    AVFrame*            frame_yuv   = nullptr;
    AVPacket*           packet      = nullptr;
    SwsContext*         convert_ctx = nullptr;
    AVFrame             dst{};
    AVPixelFormat       src_pixfmt  = AV_PIX_FMT_NONE;
    AVPixelFormat       dst_pixfmt  = AV_PIX_FMT_BGR24;
    int                 video_stream_index = -1;

    void decodeLoop();
    bool openInput();                   // avformat_open_input + find stream + open codec
    void closeInput();                  // safe close under lock
    void closeInputUnlocked();          // internal, without lock
    bool processOnePacket();            // av_read_frame + decode + push frame

    static AVFrame deepCopyFrame(const AVFrame& src);
    static void    freeFrameData(AVFrame& f);
};

