#include "udpReceiveAndDecode.h"

#include <chrono>
#include <thread>
#include <mutex>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

namespace {

constexpr uint8_t kSeiTimeSig[] = { 0x06, 0x04, 0x20, 0x54, 0x49, 0x4D, 0x45 };
constexpr int     kSeiTimeSigLen = 7;
constexpr int     kSeiTimeFields = (24+2);
constexpr int     kMaxUdp = 2048;
constexpr int     kMaxAu  = 2 * 1024 * 1024;

uint16_t rd_be16(const uint8_t* p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

#ifdef _WIN32
bool sockValid(SOCKET s) { return s != INVALID_SOCKET; }
void sockClose(SOCKET s) { if (s != INVALID_SOCKET) closesocket(s); }
int  sockErr() { return WSAGetLastError(); }
#else
bool sockValid(int s) { return s >= 0; }
void sockClose(int s) { if (s >= 0) close(s); }
int  sockErr() { return errno; }
#endif

} // namespace

bool udpDec::isUsableCaptureXY(uint16_t x, uint16_t y)
{
    return x != 0x0000 && x != 0xFFFF && y != 0x0000 && y != 0xFFFF;
}

void udpDec::tryParseSeiTime(const uint8_t* data, int size)
{
    if (!data || size < kSeiTimeSigLen + kSeiTimeFields)
        return;

    const int last = size - (kSeiTimeSigLen + kSeiTimeFields);
    for (int i = 0; i <= last; ++i) {
        if (memcmp(data + i, kSeiTimeSig, kSeiTimeSigLen) != 0)
            continue;

        const uint8_t* fields = data + i + kSeiTimeSigLen;
        const uint16_t x = rd_be16(fields + 20);
        const uint16_t y = rd_be16(fields + 22);
        m_seiCapX = x;
        m_seiCapY = y;
        m_seiCapValid = isUsableCaptureXY(x, y);
        return;
    }
}

udpDec::udpDec(PlayerInitStructure* param, QObject* parent)
    : QObject(parent)
{
    if (!param) {
        qDebug() << "udpDec: null param";
        return;
    }

    m_frameQueue   = param->pFrameOutQueue;
    m_winHeight    = param->imageHeight;
    m_winWidth     = param->imageWidth;
    m_recudpport   = static_cast<uint16_t>(param->udpport);
    m_phframeMutex = param->phframeMutex;

    strncpy(m_adapterName, param->adapterName, sizeof(m_adapterName) - 1);
    m_adapterName[sizeof(m_adapterName) - 1] = '\0';
    strncpy(m_bindAddress, param->bindAddress, sizeof(m_bindAddress) - 1);
    m_bindAddress[sizeof(m_bindAddress) - 1] = '\0';

    m_enable = false;
    m_active = true;
    m_opened = false;
    m_au.reserve(256 * 1024);

    av_log_set_level(AV_LOG_ERROR);

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    av_register_all();
    avcodec_register_all();
#endif

    frame_yuv = av_frame_alloc();
    packet    = av_packet_alloc();

    if (!frame_yuv || !packet) {
        qDebug() << "udpDec: cannot allocate frame/packet";
        return;
    }

    m_decodeThread = std::thread(&udpDec::decodeLoop, this);
    qDebug() << "udpDec: constructed (RTP depay mode, bind"
             << m_bindAddress << "port" << m_recudpport << ")";
}

bool udpDec::on()
{
    if (m_enable.load())
        return true;

    if (!openInput()) {
        qDebug() << "udpDec: openInput failed";
        return false;
    }
    m_enable = true;
    qDebug() << "udpDec: on() — receiving";
    return true;
}

void udpDec::off()
{
    m_enable = false;
    for (int i = 0; i < 80 && m_opened.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    qDebug() << "udpDec: off() — stop requested, opened=" << m_opened.load();
}

bool udpDec::startListening()
{
    return true;
}

void udpDec::stopThread()
{
    m_enable = false;
    m_active = false;
    if (m_decodeThread.joinable())
        m_decodeThread.join();
    closeInput();
}

udpDec::~udpDec()
{
    stopThread();

    if (convert_ctx) {
        sws_freeContext(convert_ctx);
        convert_ctx = nullptr;
    }
    if (dst.data[0]) {
        av_free(dst.data[0]);
        dst.data[0] = nullptr;
    }
    if (frame_yuv) {
        av_frame_free(&frame_yuv);
        frame_yuv = nullptr;
    }
    if (packet) {
        av_packet_free(&packet);
        packet = nullptr;
    }
}

void udpDec::resetRtpState()
{
    m_au.clear();
    m_haveTs = false;
    m_rtpTs = 0;
    m_fuActive = false;
    m_seiCapValid = false;
}

bool udpDec::openInput()
{
    std::lock_guard<std::mutex> lock(m_openMutex);

    if (m_opened.load())
        closeInputUnlocked();

#ifdef _WIN32
    static std::once_flag wsaOnce;
    std::call_once(wsaOnce, []() {
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
    });
#endif

    m_sock =
#ifdef _WIN32
        socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
        socket(AF_INET, SOCK_DGRAM, 0);
#endif
    if (!sockValid(m_sock)) {
        qDebug() << "udpDec: socket failed" << sockErr();
        return false;
    }

    int yes = 1;
    setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&yes), sizeof(yes));

    int rcvbuf = 1024 * 1024;
    setsockopt(m_sock, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&rcvbuf), sizeof(rcvbuf));

#ifdef _WIN32
    DWORD to = 5;
    setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&to), sizeof(to));
#else
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 5000;
    setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_recudpport);
    if (m_bindAddress[0] == '\0' || strcmp(m_bindAddress, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
#ifdef _WIN32
        addr.sin_addr.s_addr = inet_addr(m_bindAddress);
        if (addr.sin_addr.s_addr == INADDR_NONE) {
            qDebug() << "udpDec: bad bind address" << m_bindAddress;
            sockClose(m_sock);
            m_sock = static_cast<UdpSocket>(-1);
            return false;
        }
#else
        if (inet_pton(AF_INET, m_bindAddress, &addr.sin_addr) != 1) {
            qDebug() << "udpDec: bad bind address" << m_bindAddress;
            sockClose(m_sock);
            m_sock = static_cast<UdpSocket>(-1);
            return false;
        }
#endif
    }

    if (bind(m_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        qDebug() << "udpDec: bind failed" << m_bindAddress << m_recudpport << sockErr();
        sockClose(m_sock);
        m_sock = static_cast<UdpSocket>(-1);
        return false;
    }

    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        qDebug() << "udpDec: H.264 decoder not found";
        sockClose(m_sock);
        m_sock = static_cast<UdpSocket>(-1);
        return false;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        sockClose(m_sock);
        m_sock = static_cast<UdpSocket>(-1);
        return false;
    }

    codec_ctx->flags  |= AV_CODEC_FLAG_LOW_DELAY;
    codec_ctx->flags2 |= AV_CODEC_FLAG2_CHUNKS | AV_CODEC_FLAG2_SHOW_ALL | AV_CODEC_FLAG2_FAST;
    codec_ctx->thread_count = 1;
    codec_ctx->thread_type  = FF_THREAD_SLICE;
    codec_ctx->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;
    codec_ctx->err_recognition   = AV_EF_IGNORE_ERR;
    codec_ctx->delay = 0;
    codec_ctx->has_b_frames = 0;

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        qDebug() << "udpDec: avcodec_open2 failed";
        avcodec_free_context(&codec_ctx);
        sockClose(m_sock);
        m_sock = static_cast<UdpSocket>(-1);
        return false;
    }

    resetRtpState();
    m_opened = true;
    qDebug() << "udpDec: listening" << (m_bindAddress[0] ? m_bindAddress : "0.0.0.0")
             << ":" << m_recudpport;
    return true;
}

void udpDec::closeInput()
{
    std::lock_guard<std::mutex> lock(m_openMutex);
    closeInputUnlocked();
}

void udpDec::closeInputUnlocked()
{
    if (sockValid(m_sock)) {
        sockClose(m_sock);
        m_sock = static_cast<UdpSocket>(-1);
    }
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }
    resetRtpState();
    m_opened = false;

    if (convert_ctx) {
        sws_freeContext(convert_ctx);
        convert_ctx = nullptr;
    }
    if (dst.data[0]) {
        av_free(dst.data[0]);
        dst.data[0] = nullptr;
    }
    src_pixfmt = AV_PIX_FMT_NONE;
    m_winWidth = 0;
    m_winHeight = 0;
}

int udpDec::decodeInterruptCb(void* opaque)
{
    auto* self = static_cast<udpDec*>(opaque);
    if (!self)
        return 1;
    return (!self->m_enable.load() || !self->m_active.load()) ? 1 : 0;
}

bool udpDec::appendNal(const uint8_t* nal, int size)
{
    if (!nal || size <= 0)
        return false;
    if (static_cast<int>(m_au.size()) + 4 + size > kMaxAu) {
        m_au.clear();
        m_fuActive = false;
        return false;
    }
    static const uint8_t sc[4] = {0, 0, 0, 1};
    m_au.insert(m_au.end(), sc, sc + 4);
    m_au.insert(m_au.end(), nal, nal + size);
    return true;
}

bool udpDec::handleRtpPacket(const uint8_t* data, int size)
{
    if (!data || size < 13)
        return false;

    const int version = (data[0] >> 6) & 0x03;
    if (version != 2)
        return false;

    const bool padding = (data[0] & 0x20) != 0;
    const bool extension = (data[0] & 0x10) != 0;
    const int cc = data[0] & 0x0F;
    const bool marker = (data[1] & 0x80) != 0;
    const int pt = data[1] & 0x7F;
    const uint32_t ts = (uint32_t(data[4]) << 24) | (uint32_t(data[5]) << 16)
                      | (uint32_t(data[6]) << 8) | uint32_t(data[7]);

    int off = 12 + cc * 4;
    if (off >= size)
        return false;
    if (extension) {
        if (off + 4 > size)
            return false;
        const int extWords = (data[off + 2] << 8) | data[off + 3];
        off += 4 + extWords * 4;
    }
    int payloadSize = size - off;
    if (padding && payloadSize > 0) {
        const int pad = data[size - 1];
        if (pad > 0 && pad <= payloadSize)
            payloadSize -= pad;
    }
    if (payloadSize <= 0 || off < 0)
        return false;

    // Как caps GStreamer: payload=96. Другой PT игнорируем.
    if (pt != 96)
        return false;

    const uint8_t* payload = data + off;

    if (m_haveTs && ts != m_rtpTs && !m_au.empty()) {
        decodeAccessUnit();
        m_au.clear();
        m_fuActive = false;
    }
    m_rtpTs = ts;
    m_haveTs = true;

    const uint8_t nalType = payload[0] & 0x1F;

    if (nalType == 28) { // FU-A — как rtph264depay
        if (payloadSize < 2)
            return false;
        const uint8_t fu = payload[1];
        const bool start = (fu & 0x80) != 0;
        const bool end   = (fu & 0x40) != 0;
        const uint8_t type = fu & 0x1F;
        if (start) {
            m_fuActive = true;
            uint8_t hdr = static_cast<uint8_t>((payload[0] & 0xE0) | type);
            if (static_cast<int>(m_au.size()) + 4 + 1 + (payloadSize - 2) > kMaxAu) {
                m_au.clear();
                m_fuActive = false;
                return false;
            }
            static const uint8_t sc[4] = {0, 0, 0, 1};
            m_au.insert(m_au.end(), sc, sc + 4);
            m_au.push_back(hdr);
            if (payloadSize > 2)
                m_au.insert(m_au.end(), payload + 2, payload + payloadSize);
        } else if (m_fuActive && payloadSize > 2) {
            if (static_cast<int>(m_au.size()) + (payloadSize - 2) > kMaxAu) {
                m_au.clear();
                m_fuActive = false;
                return false;
            }
            m_au.insert(m_au.end(), payload + 2, payload + payloadSize);
        }
        if (end)
            m_fuActive = false;
    } else if (nalType == 24) { // STAP-A
        int p = 1;
        while (p + 2 <= payloadSize) {
            const int nsz = (payload[p] << 8) | payload[p + 1];
            p += 2;
            if (nsz <= 0 || p + nsz > payloadSize)
                break;
            appendNal(payload + p, nsz);
            p += nsz;
        }
    } else if (nalType > 0 && nalType < 24) {
        appendNal(payload, payloadSize);
    }

    if (marker && !m_au.empty()) {
        decodeAccessUnit();
        m_au.clear();
        m_fuActive = false;
        return true;
    }
    return false;
}

bool udpDec::decodeAccessUnit()
{
    if (m_au.empty() || !codec_ctx || !packet)
        return false;

    m_seiCapValid = false;
    tryParseSeiTime(m_au.data(), static_cast<int>(m_au.size()));

    av_packet_unref(packet);
    if (av_new_packet(packet, static_cast<int>(m_au.size())) < 0)
        return false;
    memcpy(packet->data, m_au.data(), m_au.size());
    packet->pts = AV_NOPTS_VALUE;
    packet->dts = AV_NOPTS_VALUE;

    int ret = avcodec_send_packet(codec_ctx, packet);
    av_packet_unref(packet);
    if (ret < 0 && ret != AVERROR(EAGAIN))
        return false;

    return emitDecodedFrames();
}

bool udpDec::emitDecodedFrames()
{
    if (!codec_ctx || !frame_yuv)
        return false;

    bool got = false;
    while (true) {
        int ret = avcodec_receive_frame(codec_ctx, frame_yuv);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            avcodec_flush_buffers(codec_ctx);
            break;
        }

        const int fw = frame_yuv->width;
        const int fh = frame_yuv->height;
        if (fw <= 0 || fh <= 0)
            continue;

        const AVPixelFormat curFmt = static_cast<AVPixelFormat>(frame_yuv->format);
        const bool needRebuild =
            !convert_ctx ||
            src_pixfmt == AV_PIX_FMT_NONE ||
            fw != m_winWidth ||
            fh != m_winHeight ||
            curFmt != src_pixfmt;

        if (needRebuild) {
            if (convert_ctx) {
                sws_freeContext(convert_ctx);
                convert_ctx = nullptr;
            }
            if (dst.data[0]) {
                av_free(dst.data[0]);
                dst.data[0] = nullptr;
            }

            const int prevW = m_winWidth;
            m_winWidth  = fw;
            m_winHeight = fh;
            src_pixfmt  = curFmt;

            int numBytes = av_image_get_buffer_size(dst_pixfmt, m_winWidth, m_winHeight, 1);
            if (numBytes <= 0)
                continue;
            dst.data[0] = static_cast<uint8_t*>(av_malloc(numBytes));
            if (!dst.data[0])
                continue;
            av_image_fill_arrays(dst.data, dst.linesize, dst.data[0],
                                 dst_pixfmt, m_winWidth, m_winHeight, 1);

            convert_ctx = sws_getContext(
                fw, fh, src_pixfmt,
                m_winWidth, m_winHeight, dst_pixfmt,
                SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
            if (!convert_ctx)
                continue;
            qDebug() << "udpDec: frame size" << fw << "x" << fh
                     << (prevW == 0 ? "(first)" : "(changed)");
            emit incomingResolutionChanged(fw, fh);
        }

        sws_scale(convert_ctx,
                  frame_yuv->data, frame_yuv->linesize,
                  0, fh,
                  dst.data, dst.linesize);

        dst.width  = m_winWidth;
        dst.height = m_winHeight;
        dst.crop_left   = m_seiCapX;
        dst.crop_top    = m_seiCapY;
        dst.crop_right  = m_seiCapValid ? 1 : 0;
        dst.crop_bottom = 0;

        if (m_enable && m_frameQueue) {
            AVFrame copy = deepCopyFrame(dst);
            auto pushLatest = [&]() {
                while (!m_frameQueue->empty()) {
                    AVFrame old = m_frameQueue->front();
                    m_frameQueue->pop();
                    freeFrameData(old);
                }
                m_frameQueue->push(copy);
            };
            if (m_phframeMutex) {
                std::lock_guard<std::mutex> lock(*m_phframeMutex);
                pushLatest();
            } else {
                pushLatest();
            }
        }
        got = true;
    }
    return got;
}

bool udpDec::processOnePacket()
{
    if (!m_enable.load() || !m_opened.load() || !sockValid(m_sock))
        return false;

    uint8_t buf[kMaxUdp];
    const int n = recvfrom(m_sock, reinterpret_cast<char*>(buf), kMaxUdp, 0, nullptr, nullptr);
    if (n <= 0)
        return false;
    return handleRtpPacket(buf, n);
}

void udpDec::decodeLoop()
{
    while (m_active.load()) {
        if (!m_enable.load()) {
            if (m_opened.load())
                closeInput();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (!m_opened.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        if (!processOnePacket())
            std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    closeInput();
}

AVFrame udpDec::deepCopyFrame(const AVFrame& src)
{
    AVFrame dstf;
    memset(&dstf, 0, sizeof(dstf));
    dstf.width  = src.width;
    dstf.height = src.height;
    dstf.format = src.format;
    dstf.crop_left   = src.crop_left;
    dstf.crop_top    = src.crop_top;
    dstf.crop_right  = src.crop_right;
    dstf.crop_bottom = src.crop_bottom;

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_BGR24, src.width, src.height, 1);
    if (numBytes <= 0 || !src.data[0])
        return dstf;

    dstf.data[0] = static_cast<uint8_t*>(av_malloc(numBytes));
    if (!dstf.data[0])
        return dstf;

    memcpy(dstf.data[0], src.data[0], static_cast<size_t>(numBytes));
    dstf.linesize[0] = src.linesize[0] ? src.linesize[0] : src.width * 3;
    return dstf;
}

void udpDec::freeFrameData(AVFrame& f)
{
    if (f.data[0]) {
        av_free(f.data[0]);
        f.data[0] = nullptr;
    }
    memset(&f, 0, sizeof(f));
}
