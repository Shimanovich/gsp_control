#include "udpReceiveAndDecode.h"

#include <chrono>

// ============================================================================
// Constructor
// ============================================================================
udpDec::udpDec(PlayerInitStructure* param)
{
    if (!param) {
        printf("udpDec: null param\n");
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

    // ---- Winsock ----
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("udpDec: WSAStartup failed %d\n", WSAGetLastError());
        return;
    }

    ReceivingSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (ReceivingSocket == INVALID_SOCKET) {
        printf("udpDec: socket() failed %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }

    memset(&ReceiverAddr, 0, sizeof(ReceiverAddr));
    ReceiverAddr.sin_family      = AF_INET;
    ReceiverAddr.sin_port        = htons(m_recudpport);
    ReceiverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    int timeout = param->udptimeout;
    setsockopt(ReceivingSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    u_int yes = 1;
    setsockopt(ReceivingSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

    int rcvbuf = 4 * 1024 * 1024;
    setsockopt(ReceivingSocket, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf));

    if (bind(ReceivingSocket, (SOCKADDR*)&ReceiverAddr, sizeof(ReceiverAddr)) == SOCKET_ERROR) {
        printf("udpDec: bind() failed %d\n", WSAGetLastError());
        closesocket(ReceivingSocket);
        ReceivingSocket = INVALID_SOCKET;
        WSACleanup();
        return;
    }
    printf("udpDec: bound to port %u\n", m_recudpport);



    // // Multicast (optional)
    // struct ip_mreq mreq;
    // mreq.imr_multiaddr.s_addr = inet_addr("232.32.32.32");
    // mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    // if (setsockopt(ReceivingSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq)) < 0) {
    //     printf("udpDec: multicast join failed (ok for unicast)\n");
    // }



    // ---- FFmpeg (modern API) ----
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    av_register_all();
    avcodec_register_all();
#endif
    avformat_network_init();

    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        printf("udpDec: H.264 decoder not found\n");
        return;
    }

    context = avcodec_alloc_context3(codec);
    if (!context) {
        printf("udpDec: cannot allocate codec context\n");
        return;
    }

    context->flags  |= AV_CODEC_FLAG_LOW_DELAY;
    context->flags2 |= AV_CODEC_FLAG2_CHUNKS;
    context->thread_count = 1;
    context->thread_type  = FF_THREAD_SLICE;
    context->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    context->delay = 0;

    if (avcodec_open2(context, codec, nullptr) < 0) {
        printf("udpDec: cannot open codec\n");
        return;
    }

    frame_yuv = av_frame_alloc();
    if (!frame_yuv) {
        printf("udpDec: cannot allocate frame\n");
        return;
    }

    packet = av_packet_alloc();
    if (!packet) {
        printf("udpDec: cannot allocate packet\n");
        return;
    }

    m_recvThread   = std::thread(&udpDec::receiveLoop, this);
    m_decodeThread = std::thread(&udpDec::decodeLoop, this);

    printf("udpDec: threads started (modern FFmpeg API)\n");
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
    if (ReceivingSocket != INVALID_SOCKET) {
        closesocket(ReceivingSocket);
        ReceivingSocket = INVALID_SOCKET;
    }
    WSACleanup();
}

// ============================================================================
// Graceful stop
// ============================================================================
void udpDec::stopThread()
{
    m_active = false;
    m_queueCv.notify_all();

    if (ReceivingSocket != INVALID_SOCKET) {
        closesocket(ReceivingSocket);
        ReceivingSocket = INVALID_SOCKET;
    }

    if (m_recvThread.joinable())   m_recvThread.join();
    if (m_decodeThread.joinable()) m_decodeThread.join();
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
// Receive thread
// ============================================================================
void udpDec::receiveLoop()
{
    while (m_active) {
        int n = recvfrom(ReceivingSocket,
                         reinterpret_cast<char*>(m_rtpBuf),
                         MAX_UDP_SIZE,
                         0, nullptr, nullptr);

        if (n <= 0) {
            if (!m_active) break;
            continue;
        }

        if (!m_enable) continue;

        processRtpPacket(m_rtpBuf, n);
    }
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

                printf("udpDec: first frame %dx%d\n", frame_yuv->width, frame_yuv->height);

                convert_ctx = sws_getContext(
                    frame_yuv->width, frame_yuv->height, src_pixfmt,
                    m_winWidth, m_winHeight, dst_pixfmt,
                    SWS_BICUBIC, nullptr, nullptr, nullptr);

                if (!convert_ctx) {
                    printf("udpDec: cannot create sws context\n");
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
