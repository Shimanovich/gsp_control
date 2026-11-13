#include "udpReceiveAndDecode.h"
#include "videorecorder.h"

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
// payload 0x20 = "TIME"(4) + 28 байт. После сигнатуры:
// +20/+22 capture_x/y BE, +24/+26 strobe_w/h BE.
constexpr int     kSeiTimeFields = 28;
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

bool udpDec::isUsableCaptureWH(uint16_t w, uint16_t h)
{
    return w > 0 && h > 0;
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
        const uint16_t w = rd_be16(fields + 24);
        const uint16_t h = rd_be16(fields + 26);
        m_seiCapX = x;
        m_seiCapY = y;
        m_seiCapW = w;
        m_seiCapH = h;
        m_seiCapValid = isUsableCaptureXY(x, y) && isUsableCaptureWH(w, h);
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
    m_frameTimeoutMs = param->udptimeout > 0 ? param->udptimeout : 40;
    m_socketTimeoutMs = param->socketTimeoutMs > 0 ? param->socketTimeoutMs : 5;
    m_jitterMs = param->jitterMs > 0 ? param->jitterMs : 120;
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
    m_recvThread = std::thread(&udpDec::recvLoop, this);
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
    m_pktCv.notify_all();
    if (m_recvThread.joinable())
        m_recvThread.join();
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
    m_pendingAu.clear();
    m_havePendingAu = false;
    m_haveTs = false;
    m_rtpTs = 0;
    m_fuActive = false;
    m_haveSeq = false;
    m_lastSeq = 0;
    m_reorder.clear();
    m_reorderAt.clear();
    {
        std::lock_guard<std::mutex> lock(m_pktMutex);
        std::queue<std::vector<uint8_t>> empty;
        m_pktQueue.swap(empty);
    }
    m_waitIdr = false;
    m_haveParams = false;
    m_sps.clear();
    m_pps.clear();
    m_auTiming = false;
    m_seiCapValid = false;
    m_seiCapW = 0;
    m_seiCapH = 0;
}

void udpDec::dropCurrentAu()
{
    m_au.clear();
    m_fuActive = false;
    m_auTiming = false;
}

bool udpDec::auTimedOut() const
{
    if (!m_auTiming || m_frameTimeoutMs <= 0)
        return false;
    const auto elapsed = std::chrono::steady_clock::now() - m_auStart;
    return elapsed > std::chrono::milliseconds(m_frameTimeoutMs);
}

void udpDec::noteAuPacket()
{
    if (!m_auTiming) {
        m_auStart = std::chrono::steady_clock::now();
        m_auTiming = true;
    }
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

    int rcvbuf = 4 * 1024 * 1024;
    setsockopt(m_sock, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&rcvbuf), sizeof(rcvbuf));

#ifdef _WIN32
    DWORD to = static_cast<DWORD>(m_socketTimeoutMs);
    setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&to), sizeof(to));
#else
    timeval tv{};
    tv.tv_sec = m_socketTimeoutMs / 1000;
    tv.tv_usec = (m_socketTimeoutMs % 1000) * 1000;
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
    const uint16_t seq = static_cast<uint16_t>((data[2] << 8) | data[3]);
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

    if (pt != 96)
        return false;

    if (m_haveSeq) {
        const uint16_t expected = static_cast<uint16_t>(m_lastSeq + 1);
        if (seq != expected) {
            // Дырка или перестановка: кадр неполный, в декодер не отдаём.
            // Иначе получается серый кадр и дальше только IDR.
            dropCurrentAu();
            m_waitIdr = true;
        }
    }
    m_lastSeq = seq;
    m_haveSeq = true;

    const uint8_t* payload = data + off;

    if (m_haveTs && ts != m_rtpTs && !m_au.empty())
        finishAccessUnit();
    m_rtpTs = ts;
    m_haveTs = true;

    noteAuPacket();

    const uint8_t nalType = payload[0] & 0x1F;

    if (nalType == 28) {
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
                dropCurrentAu();
                return false;
            }
            static const uint8_t sc[4] = {0, 0, 0, 1};
            m_au.insert(m_au.end(), sc, sc + 4);
            m_au.push_back(hdr);
            if (payloadSize > 2)
                m_au.insert(m_au.end(), payload + 2, payload + payloadSize);
        } else if (m_fuActive && payloadSize > 2) {
            if (static_cast<int>(m_au.size()) + (payloadSize - 2) > kMaxAu) {
                dropCurrentAu();
                return false;
            }
            m_au.insert(m_au.end(), payload + 2, payload + payloadSize);
        } else if (!start && !m_fuActive) {
            return false;
        }
        if (end)
            m_fuActive = false;
    } else if (nalType == 24) {
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

    if (marker && !m_au.empty() && !m_fuActive)
        finishAccessUnit();
    return true;
}

void udpDec::finishAccessUnit()
{
    if (m_au.empty())
        return;
    m_pendingAu.swap(m_au);
    m_havePendingAu = true;
    m_au.clear();
    m_fuActive = false;
    m_auTiming = false;
}

bool udpDec::decodeAccessUnit(const uint8_t* data, int size)
{
    if (!data || size <= 0 || !codec_ctx || !packet)
        return false;

    m_seiCapValid = false;
    m_seiCapW = 0;
    m_seiCapH = 0;
    tryParseSeiTime(data, size);

    if (m_recorder)
        m_recorder->write(data, size);

    auto nalTypeAt = [](const uint8_t* p, int i, int n) -> int {
        int sc = 0;
        if (i + 3 < n && p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1)
            sc = 3;
        else if (i + 4 < n && p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 0 && p[i + 3] == 1)
            sc = 4;
        if (!sc)
            return -1;
        return p[i + sc] & 0x1F;
    };

    bool hasSps = false, hasPps = false, hasIdr = false;
    for (int i = 0; i + 4 < size; ++i) {
        const int t = nalTypeAt(data, i, size);
        if (t == 7) hasSps = true;
        else if (t == 8) hasPps = true;
        else if (t == 5) hasIdr = true;
    }

    if (hasSps || hasPps) {
        for (int i = 0; i + 4 < size; ) {
            const int t = nalTypeAt(data, i, size);
            if (t < 0) { ++i; continue; }
            int sc = (data[i + 2] == 1) ? 3 : 4;
            int j = i + sc + 1;
            while (j + 3 < size && !(data[j] == 0 && data[j + 1] == 0 && (data[j + 2] == 1 || (data[j + 2] == 0 && j + 3 < size && data[j + 3] == 1))))
                ++j;
            if (t == 7)
                m_sps.assign(data + i, data + j);
            else if (t == 8)
                m_pps.assign(data + i, data + j);
            i = j;
        }
        if (!m_sps.empty() && !m_pps.empty())
            m_haveParams = true;
    }

    if (!m_haveParams)
        return false;

    if (m_waitIdr && !hasIdr)
        return false;

    if (m_waitIdr) {
        avcodec_flush_buffers(codec_ctx);
        m_waitIdr = false;
    }

    std::vector<uint8_t> feed;
    const uint8_t* feedData = data;
    int feedSize = size;
    if (!hasSps && !hasPps && !m_sps.empty() && !m_pps.empty()) {
        feed.reserve(m_sps.size() + m_pps.size() + static_cast<size_t>(size));
        feed.insert(feed.end(), m_sps.begin(), m_sps.end());
        feed.insert(feed.end(), m_pps.begin(), m_pps.end());
        feed.insert(feed.end(), data, data + size);
        feedData = feed.data();
        feedSize = static_cast<int>(feed.size());
    }

    av_packet_unref(packet);
    if (av_new_packet(packet, feedSize) < 0)
        return false;
    memcpy(packet->data, feedData, static_cast<size_t>(feedSize));
    packet->pts = AV_NOPTS_VALUE;
    packet->dts = AV_NOPTS_VALUE;

    int ret = avcodec_send_packet(codec_ctx, packet);
    if (ret == AVERROR(EAGAIN)) {
        emitDecodedFrames();
        ret = avcodec_send_packet(codec_ctx, packet);
    }
    av_packet_unref(packet);
    if (ret < 0) {
        m_waitIdr = true;
        avcodec_flush_buffers(codec_ctx);
        return emitDecodedFrames();
    }

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
        if (ret < 0)
            break;

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
        dst.crop_right  = m_seiCapValid ? m_seiCapW : 0;
        dst.crop_bottom = m_seiCapValid ? m_seiCapH : 0;

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

int udpDec::drainSocket()
{
    if (!m_enable.load() || !m_opened.load() || !sockValid(m_sock))
        return 0;

    int got = 0;
    uint8_t buf[kMaxUdp];
    for (;;) {
        const int n = recvfrom(m_sock, reinterpret_cast<char*>(buf), kMaxUdp, 0, nullptr, nullptr);
        if (n <= 0)
            break;
        m_bytesReceived.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
        enqueueRtp(buf, n);
        ++got;
        if (got >= 256)
            break;
    }
    return got;
}

static int seqDiff(uint16_t a, uint16_t b)
{
    return static_cast<int16_t>(a - b);
}

void udpDec::enqueueRtp(const uint8_t* data, int size)
{
    if (!data || size < 4)
        return;
    const uint16_t seq = static_cast<uint16_t>((data[2] << 8) | data[3]);
    if (m_reorder.count(seq))
        return;
    m_reorder.emplace(seq, std::vector<uint8_t>(data, data + size));
    m_reorderAt.emplace(seq, std::chrono::steady_clock::now());
    if (m_reorder.size() > 512) {
        m_reorder.erase(m_reorder.begin());
        m_reorderAt.erase(m_reorderAt.begin());
    }
}

void udpDec::flushReorder()
{
    if (m_reorder.empty())
        return;

    const auto now = std::chrono::steady_clock::now();
    const auto hold = std::chrono::milliseconds(m_jitterMs);

    if (!m_haveSeq) {
        auto it = m_reorder.begin();
        if (now - m_reorderAt[it->first] < hold && m_reorder.size() < 8)
            return;
        handleRtpPacket(it->second.data(), static_cast<int>(it->second.size()));
        m_reorderAt.erase(it->first);
        m_reorder.erase(it);
    }

    while (!m_reorder.empty()) {
        uint16_t next = static_cast<uint16_t>(m_lastSeq + 1);
        auto it = m_reorder.find(next);
        if (it != m_reorder.end()) {
            handleRtpPacket(it->second.data(), static_cast<int>(it->second.size()));
            m_reorderAt.erase(it->first);
            m_reorder.erase(it);
            continue;
        }
        auto oldest = m_reorder.begin();
        for (auto j = m_reorder.begin(); j != m_reorder.end(); ++j) {
            if (m_reorderAt[j->first] < m_reorderAt[oldest->first])
                oldest = j;
        }
        if (now - m_reorderAt[oldest->first] < hold)
            break;
        if (seqDiff(oldest->first, next) > 0) {
            dropCurrentAu();
            m_waitIdr = true;
            m_lastSeq = static_cast<uint16_t>(oldest->first - 1);
            m_haveSeq = true;
        }
        handleRtpPacket(oldest->second.data(), static_cast<int>(oldest->second.size()));
        m_reorderAt.erase(oldest->first);
        m_reorder.erase(oldest);
    }
}

void udpDec::recvLoop()
{
    uint8_t buf[kMaxUdp];
    while (m_active.load()) {
        if (!m_enable.load() || !m_opened.load() || !sockValid(m_sock)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        const int n = recvfrom(m_sock, reinterpret_cast<char*>(buf), kMaxUdp, 0, nullptr, nullptr);
        if (n <= 0)
            continue;
        m_bytesReceived.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(m_pktMutex);
            if (m_pktQueue.size() < 2048)
                m_pktQueue.emplace(buf, buf + n);
        }
        m_pktCv.notify_one();
    }
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

        int got = 0;
        {
            std::unique_lock<std::mutex> lock(m_pktMutex);
            m_pktCv.wait_for(lock, std::chrono::milliseconds(m_socketTimeoutMs > 0 ? m_socketTimeoutMs : 5), [this] {
                return !m_pktQueue.empty() || !m_active.load();
            });
            while (!m_pktQueue.empty() && got < 256) {
                enqueueRtp(m_pktQueue.front().data(), static_cast<int>(m_pktQueue.front().size()));
                m_pktQueue.pop();
                ++got;
            }
        }
        flushReorder();
        if (m_havePendingAu) {
            decodeAccessUnit(m_pendingAu.data(), static_cast<int>(m_pendingAu.size()));
            m_pendingAu.clear();
            m_havePendingAu = false;
        } else if (got == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
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
