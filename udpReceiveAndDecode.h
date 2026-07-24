#pragma once

// Modernized H.264 RTP decoder for FFmpeg 4.0+ / 5.x / 6.x / 7.x / 8.x
// - Uses QUdpSocket (no raw Winsock)
// - std::queue of complete Annex-B NAL units between threads
// - FU-A reassembly in readyRead handler
// - New FFmpeg API: avcodec_send_packet / avcodec_receive_frame
// - Compatible with MinGW / MSVC / Clang + Qt

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QDebug>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <queue>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#ifdef _WIN32
#include <Windows.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

class udpDec : public QObject
{
    Q_OBJECT

public:
    struct PlayerInitStructure {
        int  udpport      = 5000;
        int  udptimeout   = 50;          // kept for compatibility, not used with QUdpSocket
        char adapterName[64] = {0};
        int  imageWidth   = 0;
        int  imageHeight  = 0;
        std::queue<AVFrame>* pFrameOutQueue = nullptr;
        HANDLE* pHframeMutex = nullptr;
    };

    explicit udpDec(PlayerInitStructure* param, QObject* parent = nullptr);
    ~udpDec() override;

    void stopThread();
    void on()  { m_enable = true;  }
    void off() { m_enable = false; }
    bool state() const { return m_enable; }

private slots:
    void onReadyRead();

private:
    typedef struct nalu_header {
        uint8_t type : 5;
        uint8_t nri  : 2;
        uint8_t f    : 1;
    } nalu_header_t;

    typedef struct fu_header {
        uint8_t type : 5;
        uint8_t r    : 1;
        uint8_t e    : 1;
        uint8_t s    : 1;
    } fu_header_t;

    static constexpr int    MAX_UDP_SIZE   = 1500;
    static constexpr size_t MAX_NAL_QUEUE  = 64;

    // Qt UDP
    QUdpSocket* m_socket_video = nullptr;

    // Decode thread only (receive is via Qt signal)
    std::thread          m_decodeThread;
    std::atomic<bool>    m_active{true};

    // Inter-thread: complete Annex-B NALs
    std::queue<std::vector<uint8_t>> m_nalQueue;
    std::mutex                       m_queueMutex;
    std::condition_variable          m_queueCv;

    HANDLE* m_pHframeMutex = nullptr;

    uint16_t m_recudpport = 0;
    char     m_adapterName[64] = {0};

    // FFmpeg (modern)
    const AVCodec*      codec       = nullptr;
    AVCodecContext*     context     = nullptr;
    AVFrame*            frame_yuv   = nullptr;
    AVPacket*           packet      = nullptr;
    AVFrame             dst{};
    struct SwsContext*  convert_ctx = nullptr;
    AVPixelFormat       src_pixfmt  = AV_PIX_FMT_NONE;
    AVPixelFormat       dst_pixfmt  = AV_PIX_FMT_BGR24;

    int m_winWidth  = 0;
    int m_winHeight = 0;

    std::atomic<bool> m_enable{false};

    std::queue<AVFrame>* m_frameQueue = nullptr;

    // FU-A state
    std::vector<uint8_t> m_fuBuffer;
    bool                 m_inFuA = false;

    void decodeLoop();
    bool processRtpPacket(const uint8_t* rtp, int len);

    static AVFrame deepCopyFrame(const AVFrame& src);
    static void    freeFrameData(AVFrame& f);
};
