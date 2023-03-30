#include "packet_interface.h"
#include <sys/types.h>
#include "zlib.h"
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

struct __attribute__((__packed__)) pkt
{
    ptypes_t type;
    uint8_t tr;
    uint8_t window;
    uint8_t seqnum;
    uint16_t length;
    uint32_t timestamp;
    uint32_t crc1;
    uint32_t crc2;
    char *payload;
};

pkt_t *pkt_new()
{
    return calloc(1, sizeof(pkt_t));
}

void pkt_del(pkt_t *pkt)
{
    if (pkt)
    {
        if (pkt->payload)
        {
            free(pkt->payload);
            pkt->payload = NULL;
        }
        free(pkt);
        pkt = NULL;
    }
}

pkt_status_code pkt_decode(const char *data, const size_t len, pkt_t *pkt)
{
    if (len < 10)
    {
        return E_NOHEADER;
    }
    uint8_t type = (data[0] & 0xc0) >> 6;
    uint8_t tr = (data[0] & 0x20) >> 5;
    uint8_t window = data[0] & 0x1f;
    pkt_status_code status;
    status = pkt_set_type(pkt, type);
    if (status != PKT_OK)
    {
        return status;
    }
    status = pkt_set_tr(pkt, tr);
    if (status != PKT_OK)
    {
        return status;
    }
    status = pkt_set_window(pkt, window);
    if (status != PKT_OK)
    {
        return status;
    }

    if (type == 1)
    {
        if (len < 12)
        {
            return E_NOHEADER;
        }
        uint16_t length = ntohs(*(uint16_t *)&data[1]);
        uint8_t seqnum = data[3];
        uint32_t timestamp = *(uint32_t *)&data[4];
        status = pkt_set_length(pkt, length);
        if (status != PKT_OK)
        {
            return status;
        }
        status = pkt_set_seqnum(pkt, seqnum);
        if (status != PKT_OK)
        {
            return status;
        }
        status = pkt_set_timestamp(pkt, timestamp);
        char buf[8];
        memcpy(buf, data, 8);
        buf[0] = buf[0] & 0b11011111;
        uint32_t crc1 = ntohl(*(uint32_t *)&data[8]);
        uint32_t crc1Bis = crc32(0L, (uint8_t *)buf, 8);
        if (crc1 != crc1Bis)
        {
            return E_CRC;
        }
        status = pkt_set_crc1(pkt, crc1);
        if (!tr && pkt_get_length(pkt) > 0)
        {
            status = pkt_set_payload(pkt, &data[12], length);
            if (status != PKT_OK)
            {
                return status;
            }
            uint32_t crc2 = ntohl(*(uint32_t *)&data[12 + length]);
            uint32_t crc2Bis = crc32(0L, (uint8_t *)data + 12, length);
            if (crc2 != crc2Bis)
            {
                return E_CRC;
            }
            status = pkt_set_crc2(pkt, crc2);
            if (status != PKT_OK)
            {
                return status;
            }
        }
    }
    else
    {
        uint8_t seqnum = data[1];
        uint32_t timestamp = *(uint32_t *)&data[2];
        status = pkt_set_seqnum(pkt, seqnum);
        if (status != PKT_OK)
        {
            return status;
        }
        status = pkt_set_timestamp(pkt, timestamp);
        uint32_t crc1 = ntohl(*(uint32_t *)&data[6]);
        uint32_t crc1Bis = crc32(0L, (uint8_t *)data, 6);
        if (crc1 != crc1Bis)
        {
            return E_CRC;
        }
        status = pkt_set_crc1(pkt, crc1);
    }

    return PKT_OK;
}

pkt_status_code pkt_encode(const pkt_t *pkt, char *buf, size_t *len)
{
    size_t sizePkt = predict_header_length(pkt) + 4;    
    if (pkt_get_type(pkt) == 1 && pkt_get_length(pkt) > 0)
    {
        sizePkt += pkt_get_length(pkt) + 4;
    }
    if (sizePkt > * len)
    {
        return E_NOMEM;
    }
    buf[0] = pkt_get_type(pkt) << 6;
    buf[0] |= pkt_get_tr(pkt) << 5;
    buf[0] |= pkt_get_window(pkt);
    if (pkt_get_tr(pkt) == 0 && pkt_get_type(pkt) == 1)
    {
        uint16_t length = pkt_get_length(pkt);
        if (length > 512)
        {
            return E_LENGTH;
        }
        uint16_t networkLen = htons(length);
        memcpy(&buf[1], &networkLen, 2);
        uint8_t seqnum = pkt_get_seqnum(pkt);
        memcpy(&buf[3], &seqnum, 1);
        uint32_t timestamp = pkt_get_timestamp(pkt);
        memcpy(&buf[4], &timestamp, 4);
        uint32_t crc1 = htonl(crc32(0L, (uint8_t *)buf, 8));
        memcpy(&buf[8], &crc1, 4);
        const char *pay = pkt_get_payload(pkt);
        memcpy(&buf[12], pay, length);
        uint32_t crc2 = htonl(crc32(0L, (uint8_t *)pay, length));
        memcpy(&buf[12 + length], &crc2, 4);
    }

    else if (pkt_get_tr(pkt) == 0 && (pkt_get_type(pkt) == 2 || pkt_get_type(pkt) == 3))
    {
        uint8_t seqnum = pkt_get_seqnum(pkt);
        memcpy(&buf[1], &seqnum, 1);

        uint32_t timestamp = pkt_get_timestamp(pkt);
        memcpy(&buf[2], &timestamp, 4);

        uint32_t crc1 = htonl(crc32(0L, (uint8_t *)buf, 6));
        memcpy(&buf[6], &crc1, 4);
    }
    *len = sizePkt;
    return PKT_OK;
}

ptypes_t pkt_get_type(const pkt_t *pkt)
{
    return pkt->type;
}

uint8_t pkt_get_tr(const pkt_t *pkt)
{
    return pkt->tr;
}

uint8_t pkt_get_window(const pkt_t *pkt)
{
    return pkt->window;
}

uint8_t pkt_get_seqnum(const pkt_t *pkt)
{
    return pkt->seqnum;
}

uint16_t pkt_get_length(const pkt_t *pkt)
{
    return pkt->length;
}

uint32_t pkt_get_timestamp(const pkt_t *pkt)
{
    return pkt->timestamp;
}

uint32_t pkt_get_crc1(const pkt_t *pkt)
{
    return pkt->crc1;
}

uint32_t pkt_get_crc2(const pkt_t *pkt)
{
    return pkt->crc2;
}

const char *pkt_get_payload(const pkt_t *pkt)
{
    return pkt->payload;
}

pkt_status_code pkt_set_type(pkt_t *pkt, const ptypes_t type)
{
    if (type > 0 && type < 4)
    {
        pkt->type = type;
        return PKT_OK;
    }
    else
    {
        return E_TYPE;
    }
}

pkt_status_code pkt_set_tr(pkt_t *pkt, const uint8_t tr)
{
    if (tr == 0 || tr == 1)
    {
        pkt->tr = tr;
        return PKT_OK;
    }
    else
    {
        return E_TR;
    }
}

pkt_status_code pkt_set_window(pkt_t *pkt, const uint8_t window)
{
    if (window < 32)
    {
        pkt->window = window;
        return PKT_OK;
    }
    else
    {
        return E_WINDOW;
    }
}

pkt_status_code pkt_set_seqnum(pkt_t *pkt, const uint8_t seqnum)
{
    pkt->seqnum = seqnum;
    return PKT_OK;
}

pkt_status_code pkt_set_length(pkt_t *pkt, const uint16_t length)
{
    if (length < 513)
    {
        pkt->length = length;
        return PKT_OK;
    }
    else
    {
        return E_LENGTH;
    }
}

pkt_status_code pkt_set_timestamp(pkt_t *pkt, const uint32_t timestamp)
{
    pkt->timestamp = timestamp;
    return PKT_OK;
}

pkt_status_code pkt_set_crc1(pkt_t *pkt, const uint32_t crc1)
{
    pkt->crc1 = crc1;
    return PKT_OK;
}

pkt_status_code pkt_set_crc2(pkt_t *pkt, const uint32_t crc2)
{
    pkt->crc2 = crc2;
    return PKT_OK;
}

pkt_status_code pkt_set_payload(pkt_t *pkt, const char *data, const uint16_t length)
{
    if (length > 512)
    {
        return E_LENGTH;
    }
    pkt->length = length;
    pkt->payload = calloc(length, sizeof(char));
    if (pkt->payload == NULL)
    {
        return E_NOMEM;
    }
    memcpy(pkt->payload, data, (size_t) length);
    return PKT_OK;
}

size_t predict_header_length(const pkt_t *pkt)
{
    if (pkt->type == 1)
    {
        return 8;
    }
    if (pkt->type == 2 || pkt->type == 3)
    {
        return 6;
    }
    return 0;
}