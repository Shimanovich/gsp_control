#include "mocstatusdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QDateTime>
#include <QtEndian>
#include <QScrollArea>
#include <QFont>
#include <QSizePolicy>
#include <QResizeEvent>
#include <QShowEvent>
#include <QScreen>

MocStatusDialog::MocStatusDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Телеметрия (ТМ)"));
    setMinimumSize(900, 480);
    resize(1400, 650);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* root = new QVBoxLayout(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* content = new QWidget;
    auto* columns = new QHBoxLayout(content);
    columns->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    columns->setSpacing(12);

    auto addColumn = [&](const QString& title, auto setup) {
        auto* grp = new QGroupBox(this);
        grp->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        grp->setMinimumWidth(140);
        grp->setTitle(QString()); // no overlapping title
        auto* lay = new QVBoxLayout(grp);

        auto* header = new QLabel(title, grp);
        header->setAlignment(Qt::AlignCenter);
        header->setWordWrap(true);
        header->setObjectName("colHeader");
        header->setStyleSheet(QStringLiteral("font-weight: bold;"));
        lay->addWidget(header);

        setup(lay);
        columns->addWidget(grp);
    };

    // ---- МОС (33, Л) ----
    addColumn(QStringLiteral("МОС (33)\nПрил. Л"), [&](QVBoxLayout* lay) {
        auto* grid = new QGridLayout;
        int row = 0;
        auto addLed = [&](const QString& name, QLabel*& led) {
            grid->addWidget(new QLabel(name), row, 0);
            led = makeLed();
            grid->addWidget(led, row, 1);
            ++row;
        };
        addLed(QStringLiteral("RS485_ON"), m_ledRs485On);
        addLed(QStringLiteral("GSP_ONLY"), m_ledGspOnly);
        addLed(QStringLiteral("AURORA_LANE"), m_ledAuroraLane);
        addLed(QStringLiteral("AURORA_CHAN"), m_ledAuroraChan);
        addLed(QStringLiteral("TV_RUN"), m_ledTvRun);
        addLed(QStringLiteral("TPV_RUN"), m_ledTpvRun);
        addLed(QStringLiteral("TV_LVDS"), m_ledTvLvdsRx);
        addLed(QStringLiteral("TV_STREAM"), m_ledTvStream);
        addLed(QStringLiteral("TPV_LVDS"), m_ledTpvLvdsRx);
        addLed(QStringLiteral("TPV_STREAM"), m_ledTpvStream);
        addLed(QStringLiteral("TPV_ERR"), m_ledTpvLvdsErr);
        lay->addLayout(grid);

        auto* vals = new QGridLayout;
        int r = 0;
        auto addVal = [&](const QString& name, QLabel*& lbl) {
            vals->addWidget(new QLabel(name), r, 0);
            lbl = new QLabel("—");
            lbl->setMinimumWidth(80);
            vals->addWidget(lbl, r, 1);
            ++r;
        };
        addVal(QStringLiteral("TV_FPS"), m_lblTvFps);
        addVal(QStringLiteral("TPV_FPS"), m_lblTpvFps);
        addVal(QStringLiteral("PKT_CNT"), m_lblPktCnt);
        addVal(QStringLiteral("PKT_ERR"), m_lblPktErrCnt);
        addVal(QStringLiteral("ID1_PKT"), m_lblId1PktCnt);
        addVal(QStringLiteral("ID2_PKT"), m_lblId2PktCnt);
        addVal(QStringLiteral("ID1_FR"), m_lblId1FrameCnt);
        addVal(QStringLiteral("ID2_FR"), m_lblId2FrameCnt);
        lay->addLayout(vals);
    });

    // ---- МОЭ (15, Г) ----
    addColumn(QStringLiteral("МОЭ (15)\nПрил. Г"), [&](QVBoxLayout* lay) {
        auto* grid = new QGridLayout;
        int row = 0;
        auto addLed = [&](const QString& name, QLabel*& led) {
            grid->addWidget(new QLabel(name), row, 0);
            led = makeLed();
            grid->addWidget(led, row, 1);
            ++row;
        };
        addLed(QStringLiteral("TV_LVDS_RX"), m_ledMoeTvLvds);
        addLed(QStringLiteral("TV_STREAM"), m_ledMoeTvStream);
        addLed(QStringLiteral("TPV_LVDS_RX"), m_ledMoeTpvLvds);
        addLed(QStringLiteral("TPV_STREAM"), m_ledMoeTpvStream);
        addLed(QStringLiteral("TPV_LVDS_ERR"), m_ledMoeTpvErr);
        lay->addLayout(grid);
        lay->addStretch();
    });

    // ---- ПУПэ (21, Д) ----
    addColumn(QStringLiteral("ПУПэ (21)\nПрил. Д"), [&](QVBoxLayout* lay) {
        auto* grid = new QGridLayout;
        int row = 0;
        auto addLed = [&](const QString& name, QLabel*& led) {
            grid->addWidget(new QLabel(name), row, 0);
            led = makeLed();
            grid->addWidget(led, row, 1);
            ++row;
        };
        addLed(QStringLiteral("2.5V"), m_ledPupe25);
        addLed(QStringLiteral("3.3V"), m_ledPupe33);
        addLed(QStringLiteral("5V_TPV"), m_ledPupe5Tpv);
        addLed(QStringLiteral("5V_FPGA"), m_ledPupe5Fpga);
        addLed(QStringLiteral("12V_TV"), m_ledPupe12Tv);
        addLed(QStringLiteral("12V_LAZER"), m_ledPupe12Lazer);
        lay->addLayout(grid);
        lay->addStretch();
    });

    // ---- Тн (22, Е) ----
    addColumn(QStringLiteral("Тн (22)\nПрил. Е"), [&](QVBoxLayout* lay) {
        auto* grid = new QGridLayout;
        int r = 0;
        auto addVal = [&](const QString& name, QLabel*& lbl) {
            grid->addWidget(new QLabel(name), r, 0);
            lbl = new QLabel("—");
            lbl->setMinimumWidth(90);
            grid->addWidget(lbl, r, 1);
            ++r;
        };
        addVal(QStringLiteral("U_12V"), m_lblTn12);
        addVal(QStringLiteral("U_12V_L"), m_lblTn12Lazer);
        addVal(QStringLiteral("U_12V_TV"), m_lblTn12Tv);
        addVal(QStringLiteral("U_5V"), m_lblTn5);
        addVal(QStringLiteral("U_5V_FPGA"), m_lblTn5Fpga);
        addVal(QStringLiteral("U_2.5V"), m_lblTn25);
        lay->addLayout(grid);
        lay->addStretch();
    });

    // ---- Тт (23, Ж) ----
    addColumn(QStringLiteral("Тт (23)\nПрил. Ж"), [&](QVBoxLayout* lay) {
        auto* grid = new QGridLayout;
        int r = 0;
        auto addVal = [&](const QString& name, QLabel*& lbl) {
            grid->addWidget(new QLabel(name), r, 0);
            lbl = new QLabel("—");
            lbl->setMinimumWidth(90);
            grid->addWidget(lbl, r, 1);
            ++r;
        };
        addVal(QStringLiteral("I_12V"), m_lblTt12);
        addVal(QStringLiteral("I_12V_L"), m_lblTt12Lazer);
        addVal(QStringLiteral("I_12V_TV"), m_lblTt12Tv);
        addVal(QStringLiteral("I_5V"), m_lblTt5);
        addVal(QStringLiteral("I_5V_FPGA"), m_lblTt5Fpga);
        addVal(QStringLiteral("I_2.5V"), m_lblTt25);
        lay->addLayout(grid);
        lay->addStretch();
    });

    // ---- Твт (24, З) ----
    addColumn(QStringLiteral("Твт (24)\nПрил. З"), [&](QVBoxLayout* lay) {
        auto* grid = new QGridLayout;
        int r = 0;
        auto addVal = [&](const QString& name, QLabel*& lbl) {
            grid->addWidget(new QLabel(name), r, 0);
            lbl = new QLabel("—");
            lbl->setMinimumWidth(90);
            grid->addWidget(lbl, r, 1);
            ++r;
        };
        addVal(QStringLiteral("TEMP"), m_lblTvtTemp);
        addVal(QStringLiteral("HUM"), m_lblTvtHum);
        lay->addLayout(grid);
        lay->addStretch();
    });

    // ---- ПУПс (31, И) ----
    addColumn(QStringLiteral("ПУПс (31)\nПрил. И"), [&](QVBoxLayout* lay) {
        auto* grid = new QGridLayout;
        int row = 0;
        auto addLed = [&](const QString& name, QLabel*& led) {
            grid->addWidget(new QLabel(name), row, 0);
            led = makeLed();
            grid->addWidget(led, row, 1);
            ++row;
        };
        addLed(QStringLiteral("1.8V"), m_ledPups18);
        addLed(QStringLiteral("2.5V"), m_ledPups25);
        addLed(QStringLiteral("3.3V"), m_ledPups33);
        addLed(QStringLiteral("5V_JETSON"), m_ledPups5Jetson);
        addLed(QStringLiteral("5V_FPGA"), m_ledPups5Fpga);
        addLed(QStringLiteral("5V"), m_ledPups5);
        addLed(QStringLiteral("12V"), m_ledPups12);
        lay->addLayout(grid);
        lay->addStretch();
    });

    // ---- Тнтвт (32, К) ----
    addColumn(QStringLiteral("Тнтвт (32)\nПрил. К"), [&](QVBoxLayout* lay) {
        auto* grid = new QGridLayout;
        int r = 0;
        auto addVal = [&](const QString& name, QLabel*& lbl) {
            grid->addWidget(new QLabel(name), r, 0);
            lbl = new QLabel("—");
            lbl->setMinimumWidth(90);
            grid->addWidget(lbl, r, 1);
            ++r;
        };
        addVal(QStringLiteral("U_VIN"), m_lblTntVin);
        addVal(QStringLiteral("U_12V"), m_lblTnt12);
        addVal(QStringLiteral("U_5V"), m_lblTnt5);
        addVal(QStringLiteral("U_5V_FPGA"), m_lblTnt5Fpga);
        addVal(QStringLiteral("U_5V_JET"), m_lblTnt5Jetson);
        addVal(QStringLiteral("I_VIN"), m_lblTntIVin);
        addVal(QStringLiteral("I_12V"), m_lblTntI12);
        addVal(QStringLiteral("I_5V"), m_lblTntI5);
        addVal(QStringLiteral("I_5V_FPGA"), m_lblTntI5Fpga);
        addVal(QStringLiteral("I_5V_JET"), m_lblTntI5Jetson);
        addVal(QStringLiteral("TEMP"), m_lblTntTemp);
        addVal(QStringLiteral("HUM"), m_lblTntHum);
        lay->addLayout(grid);
        lay->addStretch();
    });

    columns->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll);

    m_lblLastUpdate = new QLabel(QStringLiteral("Последнее обновление: —"), this);
    QFont updateFont = font();
    updateFont.setPointSize(10);
    m_lblLastUpdate->setFont(updateFont);
    root->addWidget(m_lblLastUpdate);

    auto* legend = new QLabel(QStringLiteral("Зелёный — активно/включено/достоверно, красный — выкл/ошибка/недостоверно. Фикс. точка 1s7d8f. Опрос 5 с."), this);
    legend->setObjectName("legend");
    legend->setStyleSheet(QStringLiteral("color: gray; font-size: 9pt;"));
    root->addWidget(legend);

    // Initial scale based on starting size
    applyScale(1.0);
}

QLabel* MocStatusDialog::makeLed() const
{
    auto* led = new QLabel;
    led->setFixedSize(m_baseLedSize, m_baseLedSize);
    led->setStyleSheet(QStringLiteral("background-color: #888; border: 1px solid #555; border-radius: 9px;"));
    const_cast<MocStatusDialog*>(this)->m_allLeds.append(led);
    return led;
}

void MocStatusDialog::setLed(QLabel* led, bool on, bool isError)
{
    if (!led) return;
    QString color = isError
        ? (on ? "#e74c3c" : "#2ecc71")
        : (on ? "#2ecc71" : "#e74c3c");
    const int r = qMax(4, led->width() / 2);
    led->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #555; border-radius: %2px;").arg(color).arg(r));
}

double MocStatusDialog::fixedPoint1s7d8(qint16 raw) const
{
    // 1 sign + 7 integer + 8 fractional → / 256.0
    return static_cast<double>(raw) / 256.0;
}

void MocStatusDialog::updateFromPayload(uint8_t sourceId, const QByteArray& payload)
{
    auto readU16 = [&](int off) -> quint16 {
        if (payload.size() < off + 2) return 0;
        return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(payload.constData() + off));
    };
    auto readI16 = [&](int off) -> qint16 {
        if (payload.size() < off + 2) return 0;
        return qFromLittleEndian<qint16>(reinterpret_cast<const uchar*>(payload.constData() + off));
    };
    auto readU32 = [&](int off) -> quint32 {
        if (payload.size() < off + 4) return 0;
        return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(payload.constData() + off));
    };

    if (sourceId == 33 && payload.size() >= 28) {
        // МОС / Л
        const uint8_t b1 = static_cast<uint8_t>(payload.at(0));
        const uint8_t b2 = static_cast<uint8_t>(payload.at(1));
        setLed(m_ledRs485On,   b1 & 0x01);
        setLed(m_ledGspOnly,   b1 & 0x02);
        setLed(m_ledAuroraLane,b1 & 0x04);
        setLed(m_ledAuroraChan,b1 & 0x08);
        setLed(m_ledTvRun,     b2 & 0x01);
        setLed(m_ledTpvRun,    b2 & 0x02);
        setLed(m_ledTvLvdsRx,  b2 & 0x04);
        setLed(m_ledTvStream,  b2 & 0x08);
        setLed(m_ledTpvLvdsRx, b2 & 0x10);
        setLed(m_ledTpvStream, b2 & 0x20);
        setLed(m_ledTpvLvdsErr,b2 & 0x40, true);
        if (m_lblTvFps)  m_lblTvFps->setText(QString::number(static_cast<uint8_t>(payload.at(2))));
        if (m_lblTpvFps) m_lblTpvFps->setText(QString::number(static_cast<uint8_t>(payload.at(3))));
        if (m_lblPktCnt)      m_lblPktCnt->setText(QString::number(readU32(4)));
        if (m_lblPktErrCnt)   m_lblPktErrCnt->setText(QString::number(readU32(8)));
        if (m_lblId1PktCnt)   m_lblId1PktCnt->setText(QString::number(readU32(12)));
        if (m_lblId2PktCnt)   m_lblId2PktCnt->setText(QString::number(readU32(16)));
        if (m_lblId1FrameCnt) m_lblId1FrameCnt->setText(QString::number(readU32(20)));
        if (m_lblId2FrameCnt) m_lblId2FrameCnt->setText(QString::number(readU32(24)));
    }
    else if (sourceId == 15 && payload.size() >= 5) {
        // МОЭ / Г — bits in UINT32 starting at offset 1
        const quint32 bits = readU32(1);
        setLed(m_ledMoeTvLvds,   bits & 0x01);
        setLed(m_ledMoeTvStream, bits & 0x02);
        setLed(m_ledMoeTpvLvds,  bits & 0x04);
        setLed(m_ledMoeTpvStream,bits & 0x08);
        setLed(m_ledMoeTpvErr,   bits & 0x10, true);
    }
    else if (sourceId == 21 && payload.size() >= 1) {
        // ПУПэ / Д
        const uint8_t b = static_cast<uint8_t>(payload.at(0));
        setLed(m_ledPupe25,      b & 0x02);
        setLed(m_ledPupe33,      b & 0x04);
        setLed(m_ledPupe5Tpv,    b & 0x08);
        setLed(m_ledPupe5Fpga,   b & 0x10);
        setLed(m_ledPupe12Tv,    b & 0x20);
        setLed(m_ledPupe12Lazer, b & 0x40);
    }
    else if (sourceId == 22 && payload.size() >= 16) {
        // Тн / Е — voltages, validity in first UINT16, values from offset 2
        const quint16 valid = readU16(0);
        auto show = [&](QLabel* lbl, int off, bool ok) {
            if (!lbl) return;
            if (!ok) { lbl->setText("?"); lbl->setStyleSheet("color: gray;"); return; }
            const double v = fixedPoint1s7d8(readI16(off));
            lbl->setText(QString("%1 В").arg(v, 0, 'f', 2));
            lbl->setStyleSheet("");
        };
        show(m_lblTn12,      2,  valid & 0x01);
        show(m_lblTn12Lazer, 4,  valid & 0x02);
        show(m_lblTn12Tv,    6,  valid & 0x04);
        show(m_lblTn5,       8,  valid & 0x08);
        show(m_lblTn5Fpga,  10,  valid & 0x10);
        show(m_lblTn25,     12,  valid & 0x80); // VALID_2V5 is b7
    }
    else if (sourceId == 23 && payload.size() >= 16) {
        // Тт / Ж — currents
        const quint16 valid = readU16(0);
        auto show = [&](QLabel* lbl, int off, bool ok) {
            if (!lbl) return;
            if (!ok) { lbl->setText("?"); lbl->setStyleSheet("color: gray;"); return; }
            const double v = fixedPoint1s7d8(readI16(off));
            lbl->setText(QString("%1 А").arg(v, 0, 'f', 2));
            lbl->setStyleSheet("");
        };
        show(m_lblTt12,      2,  valid & 0x01);
        show(m_lblTt12Lazer, 4,  valid & 0x02);
        show(m_lblTt12Tv,    6,  valid & 0x04);
        show(m_lblTt5,       8,  valid & 0x08);
        show(m_lblTt5Fpga,  10,  valid & 0x10);
        show(m_lblTt25,     12,  valid & 0x80);
    }
    else if (sourceId == 24 && payload.size() >= 6) {
        // Твт / З
        const quint16 valid = readU16(0);
        auto show = [&](QLabel* lbl, int off, bool ok, const QString& unit) {
            if (!lbl) return;
            if (!ok) { lbl->setText("?"); lbl->setStyleSheet("color: gray;"); return; }
            const double v = fixedPoint1s7d8(readI16(off));
            lbl->setText(QString("%1 %2").arg(v, 0, 'f', 1).arg(unit));
            lbl->setStyleSheet("");
        };
        show(m_lblTvtTemp, 2, valid & 0x01, "°C");
        show(m_lblTvtHum,  4, valid & 0x02, "%");
    }
    else if (sourceId == 31 && payload.size() >= 1) {
        // ПУПс / И
        const uint8_t b = static_cast<uint8_t>(payload.at(0));
        setLed(m_ledPups18,      b & 0x01);
        setLed(m_ledPups25,      b & 0x02);
        setLed(m_ledPups33,      b & 0x04);
        setLed(m_ledPups5Jetson, b & 0x08);
        setLed(m_ledPups5Fpga,   b & 0x10);
        setLed(m_ledPups5,       b & 0x20);
        setLed(m_ledPups12,      b & 0x40);
    }
    else if (sourceId == 32 && payload.size() >= 26) {
        // Тнтвт / К
        const quint16 valid = readU16(0);
        auto show = [&](QLabel* lbl, int off, bool ok, const QString& unit) {
            if (!lbl) return;
            if (!ok) { lbl->setText("?"); lbl->setStyleSheet("color: gray;"); return; }
            const double v = fixedPoint1s7d8(readI16(off));
            lbl->setText(QString("%1 %2").arg(v, 0, 'f', 2).arg(unit));
            lbl->setStyleSheet("");
        };
        show(m_lblTntVin,      2,  valid & 0x01, "В");
        show(m_lblTnt12,       4,  valid & 0x02, "В");
        show(m_lblTnt5,        6,  valid & 0x04, "В");
        show(m_lblTnt5Fpga,    8,  valid & 0x08, "В");
        show(m_lblTnt5Jetson, 10,  valid & 0x10, "В");
        show(m_lblTntIVin,    12,  valid & 0x20, "А");
        show(m_lblTntI12,     14,  valid & 0x40, "А");
        show(m_lblTntI5,      16,  valid & 0x80, "А");
        // higher validity bits are in the high byte of the UINT16
        const quint16 validHi = valid >> 8;
        show(m_lblTntI5Fpga,  18,  validHi & 0x01, "А");
        show(m_lblTntI5Jetson,20,  validHi & 0x02, "А");
        show(m_lblTntTemp,    22,  validHi & 0x04, "°C");
        show(m_lblTntHum,     24,  validHi & 0x08, "%");
    }

    if (m_lblLastUpdate)
        m_lblLastUpdate->setText(QStringLiteral("Последнее обновление: %1")
                                     .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
}


void MocStatusDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    applyScale(1.0); // initial
}

void MocStatusDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    // Scale relative to a comfortable reference width of ~1400 px
    const double ref = 1400.0;
    double factor = width() / ref;
    factor = qBound(0.75, factor, 1.6);
    applyScale(factor);
}

void MocStatusDialog::applyScale(double factor)
{
    const int pt = qMax(8, qRound(m_basePointSize * factor));
    const int titlePt = qMax(8, qRound(m_baseTitleSize * factor));
    const int led = qMax(12, qRound(m_baseLedSize * factor));

    QFont f = font();
    f.setPointSize(pt);
    setFont(f);

    // Header labels inside columns
    QFont tf = f;
    tf.setPointSize(titlePt);
    tf.setBold(true);
    const auto headers = findChildren<QLabel*>("colHeader");
    for (auto* h : headers)
        h->setFont(tf);

    // LED sizes
    const int radius = led / 2;
    for (auto* ledLbl : m_allLeds) {
        if (!ledLbl) continue;
        ledLbl->setFixedSize(led, led);
        // keep current color, only update size-related style if needed
        QString ss = ledLbl->styleSheet();
        if (ss.contains("border-radius")) {
            // leave color, radius will be reapplied by setLed on next update
        }
    }

    // Last update label
    if (m_lblLastUpdate) {
        QFont uf = f;
        uf.setPointSize(qMax(8, pt - 1));
        m_lblLastUpdate->setFont(uf);
    }

    // Legend
    const auto labels = findChildren<QLabel*>();
    for (auto* lbl : labels) {
        if (lbl && lbl->styleSheet().contains("font-size")) {
            lbl->setStyleSheet(QStringLiteral("color: gray; font-size: %1pt;").arg(qMax(8, pt - 2)));
        }
    }
}
