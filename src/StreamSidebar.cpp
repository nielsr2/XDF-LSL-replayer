#include "StreamSidebar.h"
#include "XdfLoader.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QFont>

StreamSidebar::StreamSidebar(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QLabel("  Streams");
    header->setStyleSheet(
        "font-size: 13px; font-weight: 600; color: #8890a0; "
        "background: #1a1a24; padding: 10px 8px; border-bottom: 1px solid #2a2a3a;"
    );
    layout->addWidget(header);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setAnimated(true);
    m_tree->setIndentation(16);
    m_tree->setStyleSheet(
        "QTreeWidget { background: #16161e; border: none; color: #c0c0d0; font-size: 12px; }"
        "QTreeWidget::item { padding: 4px 2px; }"
        "QTreeWidget::item:selected { background: #2a3a5a; }"
        "QTreeWidget::item:hover { background: #1e2030; }"
        "QTreeWidget::branch { background: #16161e; }"
        "QTreeWidget::indicator:checked { image: none; background: #508cff; border: 1px solid #508cff; border-radius: 3px; width: 14px; height: 14px; }"
        "QTreeWidget::indicator:unchecked { image: none; background: #2a2a3a; border: 1px solid #3a3a4a; border-radius: 3px; width: 14px; height: 14px; }"
    );

    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        // If it's a top-level stream item
        QTreeWidgetItem *topItem = item;
        if (item->parent())
            topItem = item->parent();
        int idx = m_tree->indexOfTopLevelItem(topItem);
        if (idx >= 0)
            emit streamSelected(idx);
    });

    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int) {
        int idx = m_tree->indexOfTopLevelItem(item);
        if (idx >= 0)
            emit streamToggled(idx, item->checkState(0) == Qt::Checked);
    });

    layout->addWidget(m_tree, 1);
    setMinimumWidth(220);
    setMaximumWidth(320);
}

void StreamSidebar::setStreams(const std::vector<XdfStream> &streams)
{
    m_tree->blockSignals(true);
    m_tree->clear();

    static const QString typeIcons[] = {
        "\xF0\x9F\xA7\xA0",  // 🧠 EEG
        "\xF0\x9F\x93\x8D",  // 📍 Markers
        "\xF0\x9F\x8E\xAE",  // 🎮 Control
        "\xF0\x9F\x93\x8A",  // 📊 Data
    };

    for (size_t i = 0; i < streams.size(); ++i) {
        const auto &s = streams[i];

        QString icon = "\xF0\x9F\x93\x8A"; // default 📊
        QString typeLower = QString::fromStdString(s.type).toLower();
        if (typeLower.contains("eeg") || typeLower.contains("brain"))
            icon = "\xF0\x9F\xA7\xA0";
        else if (typeLower.contains("marker") || typeLower.contains("event") || s.channelFormat == "string")
            icon = "\xF0\x9F\x93\x8D";
        else if (typeLower.contains("control") || typeLower.contains("input"))
            icon = "\xF0\x9F\x8E\xAE";

        auto *streamItem = new QTreeWidgetItem;
        streamItem->setText(0, QString("%1 %2").arg(icon, QString::fromStdString(s.name)));
        streamItem->setCheckState(0, s.hasData() ? Qt::Checked : Qt::Unchecked);
        if (!s.hasData())
            streamItem->setFlags(streamItem->flags() & ~Qt::ItemIsEnabled);

        // Metadata children
        auto addChild = [&](const QString &label, const QString &value) {
            auto *child = new QTreeWidgetItem(streamItem);
            child->setText(0, QString("  %1: %2").arg(label, value));
            child->setFlags(child->flags() & ~Qt::ItemIsUserCheckable);
            child->setForeground(0, QColor(100, 110, 140));
        };

        addChild("Type", QString::fromStdString(s.type));
        addChild("Format", QString::fromStdString(s.channelFormat));
        addChild("Channels", QString::number(s.channelCount));
        addChild("Sample Rate", s.nominalSrate > 0
                     ? QString("%1 Hz").arg(s.nominalSrate, 0, 'f', 1)
                     : "irregular");
        addChild("Samples", QString::number(s.sampleCount));

        if (s.hasData()) {
            double dur = s.maxTime() - s.minTime();
            addChild("Duration", QString("%1s").arg(dur, 0, 'f', 1));
        } else {
            addChild("Data", "no samples");
        }

        // Channel labels (collapsed by default)
        if (!s.channelLabels.empty() && s.channelCount > 0) {
            auto *chGroup = new QTreeWidgetItem(streamItem);
            chGroup->setText(0, QString("  Channels (%1)").arg(s.channelCount));
            chGroup->setForeground(0, QColor(100, 110, 140));

            int maxShow = std::min(s.channelCount, 32);
            for (int ch = 0; ch < maxShow; ++ch) {
                auto *chItem = new QTreeWidgetItem(chGroup);
                QString label = (ch < static_cast<int>(s.channelLabels.size()) && !s.channelLabels[ch].empty())
                                    ? QString::fromStdString(s.channelLabels[ch])
                                    : QString("Ch %1").arg(ch);
                chItem->setText(0, QString("    %1").arg(label));
                chItem->setForeground(0, QColor(80, 90, 120));
            }
            if (s.channelCount > 32) {
                auto *moreItem = new QTreeWidgetItem(chGroup);
                moreItem->setText(0, QString("    ... +%1 more").arg(s.channelCount - 32));
                moreItem->setForeground(0, QColor(80, 90, 120));
            }
        }

        m_tree->addTopLevelItem(streamItem);
    }

    // Expand top-level items (not channel lists)
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        m_tree->topLevelItem(i)->setExpanded(true);
    }

    m_tree->blockSignals(false);
}

void StreamSidebar::clear()
{
    m_tree->clear();
}

std::vector<bool> StreamSidebar::streamVisibility() const
{
    std::vector<bool> vis;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        vis.push_back(m_tree->topLevelItem(i)->checkState(0) == Qt::Checked);
    }
    return vis;
}
