#include "udpReceiveAndDecode.h"

#include <chrono>
#include <thread>

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
    m_opened = false;

    // Приглушаем спам логов FFmpeg
    av_log_set_level(AV_LOG_ERROR);

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    av_register_all();
    avcodec_register_all();
#endif
    avformat_network_init();

    frame_yuv = av_frame_alloc();
    packet    = av_packet_alloc();

    if (!frame_yuv || !packet) {
        qDebug() << "udpDec: cannot allocate frame/packet";
        return;
    }

    // Поток запускаем сразу — он ждёт m_enable / m_opened
    m_decodeThread = std::thread(&udpDec::decodeLoop, this);

    qDebug() << "udpDec: constructed (FFmpeg demuxer mode, port" << m_recudpport << ")";
}

// ============================================================================
// on() — открыть вход и начать приём
// ============================================================================
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

// ============================================================================
// off() — остановить приём и полностью закрыть input
// (критично для корректного повторного Start после Stop)
// ============================================================================
void udpDec::off()
{
    m_enable = false;
    // Закрываем сразу: освобождаем UDP-сокет и контекст FFmpeg.
    // Иначе при следующем on() возникает гонка с av_read_frame и
    // возможна ошибка "address already in use" / зависание.
    closeInput();
    qDebug() << "udpDec: off() — stopped and closed";
}

// ============================================================================
// startListening — оставлен для совместимости API (ничего не делает)
// ============================================================================
bool udpDec::startListening()
{
    return true;
}

// ============================================================================
// stopThread — полное завершение
// ============================================================================
void udpDec::stopThread()
{
    m_enable = false;
    m_active = false;

    // Разбудить поток, если он ждёт
    // (av_read_frame может блокироваться, поэтому закрываем input)
    closeInput();

    if (m_decodeThread.joinable())
        m_decodeThread.join();
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
        frame_yuv = nullptr;
    }
    if (packet) {
        av_packet_free(&packet);
        packet = nullptr;
    }
}

// ============================================================================
// openInput — avformat_open_input("udp://@:port")
// ============================================================================
bool udpDec::openInput()
{
    std::lock_guard<std::mutex> lock(m_openMutex);

    if (m_opened.load()) {
        // Уже открыт — сначала закрываем
        closeInputUnlocked();
    }

    // Поток — RTP/H.264 без SDP.
    // Минимальный SDP приведён к виду, который работает в VLC:
    //   c=IN IP4 127.0.0.1
    //   m=video 5000 RTP/AVP 96
    //   a=rtpmap:96 H264/90000
    // c= ставим 0.0.0.0, чтобы слушать на всех интерфейсах.

    char sdp[512];
    snprintf(sdp, sizeof(sdp),
             "v=0\r\n"
             "o=- 0 0 IN IP4 127.0.0.1\r\n"
             "s=GSP\r\n"
             "c=IN IP4 0.0.0.0\r\n"
             "t=0 0\r\n"
             "m=video %u RTP/AVP 96\r\n"
             "a=rtpmap:96 H264/90000\r\n",
             static_cast<unsigned>(m_recudpport));

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "protocol_whitelist", "file,udp,rtp,tcp", 0);
    av_dict_set(&opts, "fflags", "nobuffer+discardcorrupt", 0);
    av_dict_set(&opts, "flags", "low_delay", 0);
    av_dict_set(&opts, "probesize", "32768", 0);
    av_dict_set(&opts, "analyzeduration", "500000", 0);
    av_dict_set(&opts, "max_delay", "100000", 0);

    // Открываем SDP из памяти через AVIO
    // (avformat_open_input с "sdp" + custom IO)
    fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        qDebug() << "udpDec: avformat_alloc_context failed";
        av_dict_free(&opts);
        return false;
    }

    // Буфер SDP (FFmpeg скопирует данные)
    AVIOContext* avio = nullptr;
    unsigned char* sdp_buf = static_cast<unsigned char*>(av_malloc(strlen(sdp) + 1));
    if (!sdp_buf) {
        av_dict_free(&opts);
        avformat_free_context(fmt_ctx);
        fmt_ctx = nullptr;
        return false;
    }
    memcpy(sdp_buf, sdp, strlen(sdp) + 1);

    avio = avio_alloc_context(sdp_buf, static_cast<int>(strlen(sdp)), 0,
                              nullptr, nullptr, nullptr, nullptr);
    if (!avio) {
        av_free(sdp_buf);
        av_dict_free(&opts);
        avformat_free_context(fmt_ctx);
        fmt_ctx = nullptr;
        return false;
    }
    fmt_ctx->pb = avio;

    const AVInputFormat* sdp_fmt = av_find_input_format("sdp");
    int ret = avformat_open_input(&fmt_ctx, "memory.sdp", sdp_fmt, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qDebug() << "udpDec: avformat_open_input (SDP) failed:" << errbuf;
        // avio_context и sdp_buf освободятся при close
        if (fmt_ctx) {
            if (fmt_ctx->pb) {
                av_freep(&fmt_ctx->pb->buffer);
                avio_context_free(&fmt_ctx->pb);
            }
            avformat_free_context(fmt_ctx);
            fmt_ctx = nullptr;
        }
        return false;
    }


    // Находим потоки
    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    if (ret < 0) {
        qDebug() << "udpDec: avformat_find_stream_info failed";
        avformat_close_input(&fmt_ctx);
        return false;
    }

    // Ищем видео-поток
    video_stream_index = -1;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = static_cast<int>(i);
            break;
        }
    }

    if (video_stream_index < 0) {
        // Иногда поток определяется как data — пробуем первый
        if (fmt_ctx->nb_streams > 0) {
            video_stream_index = 0;
            qDebug() << "udpDec: no explicit video stream, using stream 0";
        } else {
            qDebug() << "udpDec: no streams found";
            avformat_close_input(&fmt_ctx);
            return false;
        }
    }

    AVCodecParameters* par = fmt_ctx->streams[video_stream_index]->codecpar;

    codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        // Часто приходит как AV_CODEC_ID_NONE / data — форсируем H.264
        codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        if (!codec) {
            qDebug() << "udpDec: H.264 decoder not found";
            avformat_close_input(&fmt_ctx);
            return false;
        }
        qDebug() << "udpDec: forced H.264 decoder";
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        qDebug() << "udpDec: cannot allocate codec context";
        avformat_close_input(&fmt_ctx);
        return false;
    }

    if (avcodec_parameters_to_context(codec_ctx, par) < 0) {
        qDebug() << "udpDec: parameters_to_context failed, continuing with defaults";
    }

    codec_ctx->flags  |= AV_CODEC_FLAG_LOW_DELAY;
    codec_ctx->flags2 |= AV_CODEC_FLAG2_CHUNKS;
    codec_ctx->thread_count = 1;
    codec_ctx->thread_type  = FF_THREAD_SLICE;
    codec_ctx->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;
    codec_ctx->err_recognition   = AV_EF_CAREFUL;
    codec_ctx->delay = 0;

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        qDebug() << "udpDec: avcodec_open2 failed";
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return false;
    }

    m_opened = true;
    qDebug() << "udpDec: input opened, stream" << video_stream_index
             << "codec" << codec->name
             << "size" << codec_ctx->width << "x" << codec_ctx->height;
    return true;
}

// ============================================================================
// closeInput helpers
// ============================================================================
void udpDec::closeInput()
{
    std::lock_guard<std::mutex> lock(m_openMutex);
    closeInputUnlocked();
}

void udpDec::closeInputUnlocked()
{
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }
    if (fmt_ctx) {
        // Мы сами создавали AVIO для SDP — освобождаем аккуратно
        if (fmt_ctx->pb) {
            if (fmt_ctx->pb->buffer)
                av_freep(&fmt_ctx->pb->buffer);
            avio_context_free(&fmt_ctx->pb);
            fmt_ctx->pb = nullptr;
        }
        avformat_close_input(&fmt_ctx);   // nullptr-safe
        fmt_ctx = nullptr;
    }
    video_stream_index = -1;
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
}


// ============================================================================
// processOnePacket — один цикл av_read_frame + decode
// ============================================================================
bool udpDec::processOnePacket()
{
    if (!fmt_ctx || !codec_ctx || !m_opened.load())
        return false;

    av_packet_unref(packet);

    int ret = av_read_frame(fmt_ctx, packet);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return false;
        // Ошибка чтения — можно попробовать продолжить
        return false;
    }

    if (packet->stream_index != video_stream_index) {
        av_packet_unref(packet);
        return false;
    }

    ret = avcodec_send_packet(codec_ctx, packet);
    av_packet_unref(packet);

    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        avcodec_flush_buffers(codec_ctx);
        return false;
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(codec_ctx, frame_yuv);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            avcodec_flush_buffers(codec_ctx);
            break;
        }

        // ---- получили кадр ----
        if (src_pixfmt == AV_PIX_FMT_NONE || !convert_ctx) {
            if (m_winHeight == 0 || m_winWidth == 0) {
                m_winHeight = frame_yuv->height;
                m_winWidth  = frame_yuv->width;
            }
            if (m_winWidth <= 0 || m_winHeight <= 0)
                continue;

            int numBytes = av_image_get_buffer_size(dst_pixfmt, m_winWidth, m_winHeight, 1);
            if (numBytes <= 0)
                continue;

            if (dst.data[0])
                av_free(dst.data[0]);
            dst.data[0] = static_cast<uint8_t*>(av_malloc(numBytes));
            av_image_fill_arrays(dst.data, dst.linesize, dst.data[0],
                                 dst_pixfmt, m_winWidth, m_winHeight, 1);

            src_pixfmt = static_cast<AVPixelFormat>(frame_yuv->format);

            convert_ctx = sws_getContext(
                frame_yuv->width, frame_yuv->height, src_pixfmt,
                m_winWidth, m_winHeight, dst_pixfmt,
                SWS_BICUBIC, nullptr, nullptr, nullptr);

            if (!convert_ctx) {
                qDebug() << "udpDec: cannot create sws context";
                continue;
            }
            qDebug() << "udpDec: first frame" << frame_yuv->width << "x" << frame_yuv->height
                     << "fmt" << src_pixfmt;
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

    return true;
}

// ============================================================================
// decodeLoop
// ============================================================================
void udpDec::decodeLoop()
{
    while (m_active.load()) {
        if (!m_enable.load() || !m_opened.load()) {
            // Ждём включения
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        // Читаем пакеты
        if (!processOnePacket()) {
            // Небольшая пауза при отсутствии данных / ошибке
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    closeInput();
}

// ============================================================================
// Deep copy / free (same as before)
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
