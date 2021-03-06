#ifndef NS_COVERAGEONLINE_PACKET_H
#define NS_COVERAGEONLINE_PACKET_H

#define HDR_COVERAGEONLINE(p)	hdr_coverageonline::access(p)

struct hdr_coverageonline
{
    triangle start_unit;
    triangle prev_unit;

    inline int size()
    {
        return 2 * sizeof(triangle);
    }

    static int offset_;
    inline static int& offset()
    {
        return offset_;
    }
    inline static struct hdr_coverageonline* access(const Packet *p)
    {
        return (struct hdr_coverageonline*) p->access(offset_);
    }
};

#endif //NS_COVERAGEONLINE_PACKET_H
