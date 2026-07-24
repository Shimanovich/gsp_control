#include "udpReceiveAndDecode.h"
#include "qvariant.h"
#include <QNetworkProxy>
#include <chrono>

// ============================================================================
// Constructor
// ============================================================================
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
    m_pHframeMutex = param->pHframeMutex;

    strncpy(m_adapterName, param->adapterName, sizeof(m_adapterName) - 1);
    m_adapterName[sizeof(m_adapterName) - 1] = '\0';

    m_enable = false;
    m_active = true;

    // ---- FFmpeg (modern API) ----
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    av_register_all();
    avcodec_register_all();
#endif
    avformat_network_init();

    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        qDebug() << "udpDec: H.264 decoder not found";
        return;
    }

    context = avcodec_alloc_context3(codec);
    if (!context) {
        qDebug() << "udpDec: cannot allocate codec context";
        return;
    }

    context->flags  |= AV_CODEC_FLAG_LOW_DELAY;
    context->flags2 |= AV_CODEC_FLAG2_CHUNKS;
    context->thread_count = 1;
    context->thread_type  = FF_THREAD_SLICE;
    context->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    context->delay = 0;

    if (avcodec_open2(context, codec, nullptr) < 0) {
        qDebug() << "udpDec: cannot open codec";
        return;
    }

    frame_yuv = av_frame_alloc();
    if (!frame_yuv) {
        qDebug() << "udpDec: cannot allocate frame";
        return;
    }

    packet = av_packet_alloc();
    if (!packet) {
        qDebug() << "udpDec: cannot allocate packet";
        return;
    }

    // ---- QUdpSocket ----
    m_socket_video = new QUdpSocket(this);

    // Large receive buffer (best effort)
    // const QVariant val = 4 * 1024 * 1024;
    // m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, val);

    m_socket_video->setProxy(QNetworkProxy::NoProxy);
    if (!m_socket_video->bind(QHostAddress::AnyIPv4, m_recudpport,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qDebug() << "udpDec: bind() failed" << m_socket_video->errorString();
        return;
    }
    qDebug() << "udpDec: bound to port" << m_recudpport;

    connect(m_socket_video, &QUdpSocket::readyRead, this, &udpDec::onReadyRead);

    // Start enabled and decode thread (matches previous behaviour)

    m_decodeThread = std::thread(&udpDec::decodeLoop, this);

    qDebug() << "udpDec: decode thread started (QUdpSocket + modern FFmpeg API)";
}

// ============================================================================
// Destructor
// ============================================================================
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
    }
    if (packet) {
        av_packet_free(&packet);
    }
    if (context) {
        avcodec_free_context(&context);
    }
    // m_socket is child of this, deleted automatically
}

// ============================================================================
// Graceful stop
// ============================================================================
void udpDec::stopThread()
{
    m_active = false;
    m_queueCv.notify_all();

    if (m_socket_video) {
        m_socket_video->disconnect(this);
        m_socket_video->close();
    }

    if (m_decodeThread.joinable())
        m_decodeThread.join();
}

// ============================================================================
// Qt readyRead → RTP processing
// ============================================================================
void udpDec::onReadyRead()
{
    if (!m_socket_video)
        return;

    while (m_socket_video->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_socket_video->pendingDatagramSize()));
        m_socket_video->readDatagram(datagram.data(), datagram.size());

        if (!m_enable)
            continue;

        processRtpPacket(reinterpret_cast<const uint8_t*>(datagram.constData()),
                         datagram.size());
    }
}

// ============================================================================
// RTP → complete Annex-B NAL (with FU-A reassembly)
// ============================================================================
bool udpDec::processRtpPacket(const uint8_t* rtp, int len)
{
    if (len < 13) return false;

    const nalu_header_t* nalu = reinterpret_cast<const nalu_header_t*>(&rtp[12]);

    // ---------- Single NAL unit ----------
    if (nalu->type != 28) {
        std::vector<uint8_t> nal;
        nal.reserve(static_cast<size_t>(len - 12) + 4);

        nal.push_back(0);
        nal.push_back(0);
        nal.push_back(0);
        nal.push_back(1);

        uint8_t hdr = (nalu->type & 0x1f) | (nalu->nri << 5) | (nalu->f << 7);
        nal.push_back(hdr);
        nal.insert(nal.end(), rtp + 13, rtp + len);

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_nalQueue.size() >= MAX_NAL_QUEUE)
                m_nalQueue.pop();
            m_nalQueue.push(std::move(nal));
        }
        m_queueCv.notify_one();
        return true;
    }

    // ---------- FU-A ----------
    if (len < 14) return false;

    const fu_header_t* fu = reinterpret_cast<const fu_header_t*>(&rtp[13]);

    if (fu->s) {                                // start
        m_fuBuffer.clear();
        m_fuBuffer.reserve(4096);

        m_fuBuffer.push_back(0);
        m_fuBuffer.push_back(0);
        m_fuBuffer.push_back(0);
        m_fuBuffer.push_back(1);

        uint8_t hdr = (fu->type & 0x1f) | (nalu->nri << 5) | (nalu->f << 7);
        m_fuBuffer.push_back(hdr);
        m_fuBuffer.insert(m_fuBuffer.end(), rtp + 14, rtp + len);
        m_inFuA = true;
        return false;
    }

    if (!m_inFuA)
        return false;

    m_fuBuffer.insert(m_fuBuffer.end(), rtp + 14, rtp + len);

    if (fu->e) {                                // end
        m_inFuA = false;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_nalQueue.size() >= MAX_NAL_QUEUE)
                m_nalQueue.pop();
            m_nalQueue.push(std::move(m_fuBuffer));
        }
        m_fuBuffer.clear();
        m_queueCv.notify_one();
        return true;
    }

    return false;
}

// ============================================================================
// Decode thread  (modern FFmpeg API)
// ============================================================================
void udpDec::decodeLoop()
{
    int first_frame = 1;

    while (m_active) {
        std::vector<uint8_t> nal;

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this] {
                return !m_nalQueue.empty() || !m_active;
            });

            if (!m_active) break;

            nal = std::move(m_nalQueue.front());
            m_nalQueue.pop();
        }

        if (nal.size() < 5) continue;

        // ---- Modern send / receive API ----
        av_packet_unref(packet);
        packet->data = nal.data();
        packet->size = static_cast<int>(nal.size());

        int ret = avcodec_send_packet(context, packet);
        if (ret < 0) {
            if (ret != AVERROR(EAGAIN))
                continue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(context, frame_yuv);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0)
                break;

            // ---- got a frame ----
            if (first_frame) {
                if (m_winHeight == 0 || m_winWidth == 0) {
                    m_winHeight = frame_yuv->height;
                    m_winWidth  = frame_yuv->width;
                }

                int numBytes = av_image_get_buffer_size(dst_pixfmt, m_winWidth, m_winHeight, 1);
                dst.data[0] = static_cast<uint8_t*>(av_malloc(numBytes));
                av_image_fill_arrays(dst.data, dst.linesize, dst.data[0],
                                     dst_pixfmt, m_winWidth, m_winHeight, 1);

                src_pixfmt = static_cast<AVPixelFormat>(frame_yuv->format);
                first_frame = 0;

                qDebug() << "udpDec: first frame" << frame_yuv->width << "x" << frame_yuv->height;

                convert_ctx = sws_getContext(
                    frame_yuv->width, frame_yuv->height, src_pixfmt,
                    m_winWidth, m_winHeight, dst_pixfmt,
                    SWS_BICUBIC, nullptr, nullptr, nullptr);

                if (!convert_ctx) {
                    qDebug() << "udpDec: cannot create sws context";
                    continue;
                }
            }

            sws_scale(convert_ctx,
                      frame_yuv->data, frame_yuv->linesize,
                      0, frame_yuv->height,
                      dst.data, dst.linesize);

            dst.width  = frame_yuv->width;
            dst.height = frame_yuv->height;

            if (m_enable && m_frameQueue) {
                AVFrame copy = deepCopyFrame(dst);

                if (m_pHframeMutex)
                    WaitForSingleObject(*m_pHframeMutex, INFINITE);

                while (m_frameQueue->size() >= 2) {
                    AVFrame old = m_frameQueue->front();
                    m_frameQueue->pop();
                    freeFrameData(old);
                }
                m_frameQueue->push(copy);

                if (m_pHframeMutex)
                    ReleaseMutex(*m_pHframeMutex);
            }
        }
    }

    if (convert_ctx) {
        sws_freeContext(convert_ctx);
        convert_ctx = nullptr;
    }
}

// ============================================================================
// Deep copy / free
// ============================================================================
AVFrame udpDec::deepCopyFrame(const AVFrame& src)
{
    AVFrame dstf;
    memset(&dstf, 0, sizeof(dstf));
    dstf.width  = src.width;
    dstf.height = src.height;
    dstf.format = src.format;

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
