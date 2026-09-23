#pragma once

// H.264 over RTP/UDP — тот же путь, что
//   udpsrc ! application/x-rtp,payload=96 ! rtph264depay ! avdec_h264
// Без avformat/SDP: свой сокет + depay + libavcodec.

#include <QObject>
#include <QDebug>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <queue>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using UdpSocket = SOCKET;
#else
using UdpSocket = int;
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

class udpDec : public QObject
{
    Q_OBJECT

public:
    struct PlayerInitStructure {
        int  udpport      = 5000;
        int  udptimeout   = 50;
        char adapterName[64] = {0};
        char bindAddress[64] = {0};     // как udpsrc address=, пусто = 0.0.0.0
        int  imageWidth   = 0;
        int  imageHeight  = 0;
        std::queue<AVFrame>* pFrameOutQueue = nullptr;
        std::mutex *phframeMutex = nullptr;
    };

    explicit udpDec(PlayerInitStructure* param, QObject* parent = nullptr);
    ~udpDec() override;

    void stopThread();
    bool on();
    void off();
    bool state() const { return m_enable.load(); }

    bool startListening();

    int incomingWidth()  const { return m_winWidth; }
    int incomingHeight() const { return m_winHeight; }

signals:
    void incomingResolutionChanged(int width, int height);

private:
    uint16_t             m_recudpport = 0;
    char                 m_adapterName[64] = {0};
    char                 m_bindAddress[64] = {0};
    int                  m_winWidth  = 0;
    int                  m_winHeight = 0;
    std::queue<AVFrame>* m_frameQueue = nullptr;
    std::mutex*          m_phframeMutex = nullptr;

    uint16_t m_seiCapX = 0;
    uint16_t m_seiCapY = 0;
    bool     m_seiCapValid = false;

    std::atomic<bool> m_active{true};
    std::atomic<bool> m_enable{false};
    std::atomic<bool> m_opened{false};

    std::thread m_decodeThread;
    std::mutex  m_openMutex;

    AVCodecContext*     codec_ctx   = nullptr;
    const AVCodec*      codec       = nullptr;
    AVFrame*            frame_yuv   = nullptr;
    AVPacket*           packet      = nullptr;
    SwsContext*         convert_ctx = nullptr;
    AVFrame             dst{};
    AVPixelFormat       src_pixfmt  = AV_PIX_FMT_NONE;
    AVPixelFormat       dst_pixfmt  = AV_PIX_FMT_BGR24;

    UdpSocket           m_sock = static_cast<UdpSocket>(-1);
    std::vector<uint8_t> m_au;
    std::vector<uint8_t> m_pendingAu;
    bool                m_havePendingAu = false;
    uint32_t            m_rtpTs = 0;
    bool                m_haveTs = false;
    bool                m_fuActive = false;
    uint16_t            m_lastSeq = 0;
    bool                m_haveSeq = false;

    void decodeLoop();
    bool openInput();
    void closeInput();
    void closeInputUnlocked();
    int  drainSocket();
    bool handleRtpPacket(const uint8_t* data, int size);
    bool appendNal(const uint8_t* nal, int size);
    void finishAccessUnit();
    bool decodeAccessUnit(const uint8_t* data, int size);
    bool emitDecodedFrames();
    void resetRtpState();
    void dropCurrentAu();
    void tryParseSeiTime(const uint8_t* data, int size);
    static int decodeInterruptCb(void* opaque);

    static AVFrame deepCopyFrame(const AVFrame& src);
    static void    freeFrameData(AVFrame& f);
    static bool    isUsableCaptureXY(uint16_t x, uint16_t y);
};
