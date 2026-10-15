// SPDX-License-Identifier: MIT
// jack-panel — phone-style status/launcher panel for the PYNQ-Z2 HDMI display.
//
// Qt Widgets only (no QML), rendered in software via the linuxfb platform
// plugin onto /dev/fb0 — the DRM fbdev of the PL display pipeline.
// Layout: clock header, live status tiles (FPGA, CPU load, memory, display
// IRQ rate), action buttons. Tiles refresh from /proc and /sys on a timer.

#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QTime>
#include <QFile>
#include <QProcess>

static QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("n/a");
    return QString::fromUtf8(f.readAll()).trimmed();
}

// One "phone tile": big value on top, small caption underneath.
class Tile : public QWidget {
public:
    explicit Tile(const QString &caption, QWidget *parent = nullptr)
        : QWidget(parent), value(new QLabel("--", this))
    {
        auto *cap = new QLabel(caption, this);
        value->setObjectName("tileValue");
        cap->setObjectName("tileCaption");
        value->setAlignment(Qt::AlignCenter);
        cap->setAlignment(Qt::AlignCenter);

        auto *lay = new QVBoxLayout(this);
        lay->addStretch();
        lay->addWidget(value);
        lay->addWidget(cap);
        lay->addStretch();
        setObjectName("tile");
    }
    void set(const QString &text) { value->setText(text); }
private:
    QLabel *value;
};

class Panel : public QWidget {
public:
    Panel()
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(24, 16, 24, 16);
        root->setSpacing(14);

        clock = new QLabel(this);
        clock->setObjectName("clock");
        clock->setAlignment(Qt::AlignCenter);
        root->addWidget(clock);

        auto *grid = new QGridLayout();
        grid->setSpacing(14);
        fpga = new Tile("FPGA", this);
        load = new Tile("CPU load", this);
        mem  = new Tile("free RAM", this);
        irq  = new Tile("display IRQ/s", this);
        grid->addWidget(fpga, 0, 0);
        grid->addWidget(load, 0, 1);
        grid->addWidget(mem,  1, 0);
        grid->addWidget(irq,  1, 1);
        root->addLayout(grid, /*stretch*/ 1);

        auto *buttons = new QGridLayout();
        buttons->setSpacing(14);
        auto *console = new QPushButton("Console", this);
        auto *reboot  = new QPushButton("Reboot", this);
        buttons->addWidget(console, 0, 0);
        buttons->addWidget(reboot, 0, 1);
        root->addLayout(buttons);

        // "Console" hands the screen back to fbcon (rebind the VT console);
        // the panel keeps running underneath and fbcon draws over it.
        connect(console, &QPushButton::clicked, [] {
            QFile f("/sys/class/vtcon/vtcon1/bind");
            if (f.open(QIODevice::WriteOnly)) f.write("1\n");
        });
        connect(reboot, &QPushButton::clicked, [] {
            QProcess::startDetached("systemctl", {"reboot"});
        });

        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &Panel::refresh);
        timer->start(1000);
        refresh();
    }

private:
    void refresh()
    {
        clock->setText(QTime::currentTime().toString("HH:mm:ss"));
        fpga->set(readAll("/sys/class/fpga_manager/fpga0/state"));
        load->set(readAll("/proc/loadavg").section(' ', 0, 0));

        // MemAvailable line -> MiB
        for (const QString &line : readAll("/proc/meminfo").split('\n'))
            if (line.startsWith("MemAvailable:"))
                mem->set(QString::number(
                    line.section(' ', -2, -2).toLongLong() / 1024) + " MiB");

        // Frame-done IRQ delta over the 1 s tick = display refresh rate.
        // xilinx_frmbuf requests its IRQ under this name; matching by name
        // survives xlconcat rewiring (the hwirq number moves with the BD).
        for (const QString &line : readAll("/proc/interrupts").split('\n'))
            if (line.contains("xilinx_framebuffer")) {
                qlonglong total = 0;
                const QStringList cols =
                    line.simplified().split(' ').mid(1, 2); // per-CPU counts
                for (const QString &c : cols) total += c.toLongLong();
                irq->set(lastIrq < 0 ? QStringLiteral("--")
                                     : QString::number(total - lastIrq));
                lastIrq = total;
                break;
            }
    }

    QLabel *clock;
    Tile *fpga, *load, *mem, *irq;
    qlonglong lastIrq = -1;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setStyleSheet(R"(
        * { color: #e8eaed; font-family: 'DejaVu Sans'; }
        QWidget { background-color: #101418; }
        #clock { font-size: 42px; font-weight: 300; }
        #tile { background-color: #1b2128; border-radius: 16px; }
        #tileValue { font-size: 30px; font-weight: 600;
                     background: transparent; }
        #tileCaption { font-size: 13px; color: #8a94a0;
                       background: transparent; }
        QPushButton { background-color: #2563eb; border: none;
                      border-radius: 14px; font-size: 20px;
                      padding: 18px; }
        QPushButton:pressed { background-color: #1d4ed8; }
    )");

    Panel panel;
    panel.showFullScreen();
    return app.exec();
}
