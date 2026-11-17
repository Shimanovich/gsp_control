#ifndef MOCSTATUSDIALOG_H
#define MOCSTATUSDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QByteArray>

class MocStatusDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MocStatusDialog(QWidget *parent = nullptr);

    void updateFromPayload(uint8_t sourceId, const QByteArray& payload);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void applyScale(double factor);
    QLabel* makeLed() const;
    void setLed(QLabel* led, bool on, bool isError = false);
    double fixedPoint1s7d8(qint16 raw) const; // 1s7d8f → double

    // МОС (33, Л)
    QLabel* m_ledRs485On = nullptr;
    QLabel* m_ledGspOnly = nullptr;
    QLabel* m_ledAuroraLane = nullptr;
    QLabel* m_ledAuroraChan = nullptr;
    QLabel* m_ledTvRun = nullptr;
    QLabel* m_ledTpvRun = nullptr;
    QLabel* m_ledTvLvdsRx = nullptr;
    QLabel* m_ledTvStream = nullptr;
    QLabel* m_ledTpvLvdsRx = nullptr;
    QLabel* m_ledTpvStream = nullptr;
    QLabel* m_ledTpvLvdsErr = nullptr;
    QLabel* m_lblTvFps = nullptr;
    QLabel* m_lblTpvFps = nullptr;
    QLabel* m_lblPktCnt = nullptr;
    QLabel* m_lblPktErrCnt = nullptr;
    QLabel* m_lblId1PktCnt = nullptr;
    QLabel* m_lblId2PktCnt = nullptr;
    QLabel* m_lblId1FrameCnt = nullptr;
    QLabel* m_lblId2FrameCnt = nullptr;

    // МОЭ (15, Г)
    QLabel* m_ledMoeTvLvds = nullptr;
    QLabel* m_ledMoeTvStream = nullptr;
    QLabel* m_ledMoeTpvLvds = nullptr;
    QLabel* m_ledMoeTpvStream = nullptr;
    QLabel* m_ledMoeTpvErr = nullptr;

    // ПУПэ (21, Д)
    QLabel* m_ledPupe25 = nullptr;
    QLabel* m_ledPupe33 = nullptr;
    QLabel* m_ledPupe5Tpv = nullptr;
    QLabel* m_ledPupe5Fpga = nullptr;
    QLabel* m_ledPupe12Tv = nullptr;
    QLabel* m_ledPupe12Lazer = nullptr;

    // Тн (22, Е) — voltages
    QLabel* m_lblTn12 = nullptr;
    QLabel* m_lblTn12Lazer = nullptr;
    QLabel* m_lblTn12Tv = nullptr;
    QLabel* m_lblTn5 = nullptr;
    QLabel* m_lblTn5Fpga = nullptr;
    QLabel* m_lblTn25 = nullptr;

    // Тт (23, Ж) — currents
    QLabel* m_lblTt12 = nullptr;
    QLabel* m_lblTt12Lazer = nullptr;
    QLabel* m_lblTt12Tv = nullptr;
    QLabel* m_lblTt5 = nullptr;
    QLabel* m_lblTt5Fpga = nullptr;
    QLabel* m_lblTt25 = nullptr;

    // Твт (24, З)
    QLabel* m_lblTvtTemp = nullptr;
    QLabel* m_lblTvtHum = nullptr;

    // ПУПс (31, И)
    QLabel* m_ledPups18 = nullptr;
    QLabel* m_ledPups25 = nullptr;
    QLabel* m_ledPups33 = nullptr;
    QLabel* m_ledPups5Jetson = nullptr;
    QLabel* m_ledPups5Fpga = nullptr;
    QLabel* m_ledPups5 = nullptr;
    QLabel* m_ledPups12 = nullptr;

    // Тнтвт (32, К)
    QLabel* m_lblTntVin = nullptr;
    QLabel* m_lblTnt12 = nullptr;
    QLabel* m_lblTnt5 = nullptr;
    QLabel* m_lblTnt5Fpga = nullptr;
    QLabel* m_lblTnt5Jetson = nullptr;
    QLabel* m_lblTntIVin = nullptr;
    QLabel* m_lblTntI12 = nullptr;
    QLabel* m_lblTntI5 = nullptr;
    QLabel* m_lblTntI5Fpga = nullptr;
    QLabel* m_lblTntI5Jetson = nullptr;
    QLabel* m_lblTntTemp = nullptr;
    QLabel* m_lblTntHum = nullptr;

    QLabel* m_lblLastUpdate = nullptr;

    QList<QLabel*> m_allLeds;
    int m_basePointSize = 11;
    int m_baseTitleSize = 10;
    int m_baseLedSize = 18;
};

#endif // MOCSTATUSDIALOG_H
