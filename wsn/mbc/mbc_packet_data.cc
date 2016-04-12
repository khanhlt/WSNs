#include <config.h>
#include <wsn/geomathhelper/geo_math_helper.h>
#include "mbc_packet_data.h"

MbcPacketData::MbcPacketData() : AppData(MBC_DATA)
{
    data_ = NULL;
    data_len_ = 0;
    element_size_ = sizeof(nsaddr_t) + 4 * sizeof(double);
}

MbcPacketData::MbcPacketData(MbcPacketData &d) : AppData(d)
{
    element_size_ = sizeof(nsaddr_t) + 4 * sizeof(double);
    data_len_ = d.data_len_;

    if (data_len_ > 0)
    {
        data_ = new unsigned char[data_len_];
        memcpy(data_, d.data_, data_len_);
    }
    else
    {
        data_ = NULL;
    }
}

void MbcPacketData::add(nsaddr_t id, double x, double y, double x_node, double y_node)
{
    unsigned char* temp = data_;
    data_ = new unsigned char[data_len_ + element_size_];

    memcpy(data_, temp, data_len_);
    memcpy(data_ + data_len_, &id, sizeof(nsaddr_t));
    memcpy(data_ + data_len_ + sizeof(nsaddr_t), &x, sizeof(double));
    memcpy(data_ + data_len_ + sizeof(nsaddr_t) + sizeof(double), &y, sizeof(double));
    memcpy(data_ + data_len_ + sizeof(nsaddr_t) + sizeof(double)*2, &x_node, sizeof(double));
    memcpy(data_ + data_len_ + sizeof(nsaddr_t) + sizeof(double)*3, &y_node, sizeof(double));

    data_len_ += element_size_;
}

void MbcPacketData::dump() {
    FILE *fp = fopen("CoverageBoundHole.tr", "a+");

    for (int i = 0; i < data_len_ / element_size_; i++)
    {
        node n = get_intersect_data(i);
        fprintf(fp, "%d\t%f\t%f\n", n.id_, n.x_, n.y_);
    }
    node n = get_intersect_data(0);
    fprintf(fp, "%d\t%f\t%f\n", n.id_, n.x_, n.y_);
    fprintf(fp, "\n");

    fclose(fp);
}

node MbcPacketData::get_intersect_data(int index)
{
    node re;
    int offset = index * element_size_;

    memcpy(&re.id_, data_ + offset,  sizeof(nsaddr_t));
    memcpy(&re.x_,  data_ + offset + sizeof(nsaddr_t),  sizeof(double));
    memcpy(&re.y_,  data_ + offset + sizeof(nsaddr_t) + sizeof(double), sizeof(double));

    return re;
}

node MbcPacketData::get_node_data(int index) {
    node re;
    int offset = index * element_size_;

    memcpy(&re.id_, data_ + offset,  sizeof(nsaddr_t));
    memcpy(&re.x_,  data_ + offset + sizeof(nsaddr_t) + sizeof(double)*2,  sizeof(double));
    memcpy(&re.y_,  data_ + offset + sizeof(nsaddr_t) + sizeof(double)*3, sizeof(double));

    return re;
}

int MbcPacketData::indexOf(node no)
{
    return indexOf(no.id_, no.x_, no.y_);
}

int MbcPacketData::indexOf(nsaddr_t id, double x, double y)
{
    node n;
    for (int i = 0; i < element_size_; i++)
    {
        n = get_intersect_data(i);
        if (n.id_ == id && n.x_ == x && n.y_ == y)
            return i;
    }

    return -1;
}

void MbcPacketData::rmv_data(int index)
{
    if (index >= data_len_ / element_size_ || index < 0) return;

    int offset = index * element_size_;

    unsigned char * temp = data_;
    data_ = new unsigned char [data_len_ - element_size_];

    memcpy(data_, temp, offset);
    memcpy(data_ + offset, temp + offset + element_size_, data_len_ - offset - element_size_);

    data_len_ -= element_size_;
}

AppData* MbcPacketData::copy()
{
    return new MbcPacketData(*this);
}

int MbcPacketData::size() const
{
    return data_len_ / element_size_;
}