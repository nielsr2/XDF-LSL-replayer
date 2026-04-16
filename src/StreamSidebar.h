#ifndef STREAMSIDEBAR_H
#define STREAMSIDEBAR_H

#include <QWidget>
#include <QTreeWidget>
#include <QCheckBox>
#include <vector>

struct XdfStream;

class StreamSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit StreamSidebar(QWidget *parent = nullptr);

    void setStreams(const std::vector<XdfStream> &streams);
    void clear();

    std::vector<bool> streamVisibility() const;

signals:
    void streamToggled(int streamIndex, bool visible);
    void streamSelected(int streamIndex);

private:
    QTreeWidget *m_tree = nullptr;
};

#endif // STREAMSIDEBAR_H
