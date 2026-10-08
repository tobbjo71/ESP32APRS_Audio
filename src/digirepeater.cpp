#include "digirepeater.h"
#include "main.h"

RTC_DATA_ATTR digiTLMType digiLog;
RTC_DATA_ATTR uint8_t digiCount = 0;

extern Configuration config;

// ---- Duplicate suppression (as Dire Wolf DEDUPE): same src, dst and info
// repeated within DIGI_DEDUPE_MS is not repeated again. Path is ignored.
#define DIGI_DEDUPE_MS 30000UL
#define DIGI_DEDUPE_N 32
static struct
{
    uint32_t hash;
    uint32_t t;
} digiDedupe[DIGI_DEDUPE_N];
static uint8_t digiDedupeIdx = 0;

static uint32_t fnv1a(uint32_t h, const uint8_t *p, size_t n)
{
    while (n--)
    {
        h ^= *p++;
        h *= 16777619UL;
    }
    return h;
}

static uint32_t digiPktHash(const AX25Msg &Packet)
{
    uint32_t h = 2166136261UL;
    h = fnv1a(h, (const uint8_t *)Packet.src.call, strnlen(Packet.src.call, sizeof(Packet.src.call)));
    h = fnv1a(h, &Packet.src.ssid, 1);
    h = fnv1a(h, (const uint8_t *)Packet.dst.call, strnlen(Packet.dst.call, sizeof(Packet.dst.call)));
    h = fnv1a(h, &Packet.dst.ssid, 1);
    size_t n = Packet.len;
    if (n > sizeof(Packet.info))
        n = sizeof(Packet.info);
    return fnv1a(h, Packet.info, n);
}

bool digiDupeSeen(const AX25Msg &Packet)
{
    uint32_t h = digiPktHash(Packet), now = millis();
    for (int i = 0; i < DIGI_DEDUPE_N; i++)
        if (digiDedupe[i].t != 0 && digiDedupe[i].hash == h && (uint32_t)(now - digiDedupe[i].t) < DIGI_DEDUPE_MS)
            return true;
    return false;
}

void digiDupeRemember(const AX25Msg &Packet)
{
    uint32_t now = millis();
    digiDedupe[digiDedupeIdx].hash = digiPktHash(Packet);
    digiDedupe[digiDedupeIdx].t = now ? now : 1;
    digiDedupeIdx = (digiDedupeIdx + 1) % DIGI_DEDUPE_N;
}

// "WIDEn" with n = 1..7 -> n, anything else -> 0
static int wideN(const char *call)
{
    if (strncmp(call, "WIDE", 4) != 0)
        return 0;
    if (call[4] < '1' || call[4] > '7' || call[5] != 0)
        return 0;
    return call[4] - '0';
}

// Replace path entry idx with MYCALL-SSID and mark it as repeated
static void setMyCallUsed(AX25Msg &Packet, int idx)
{
    strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
    Packet.rpt_list[idx].ssid = config.digi_ssid;
    Packet.rpt_flags |= (1 << idx);
}

// Shift path entries idx..end one step right (caller checks rpt_count < AX25_MAX_RPT)
static void insertSlot(AX25Msg &Packet, int idx)
{
    for (int k = Packet.rpt_count; k > idx; k--)
    {
        memcpy(&Packet.rpt_list[k], &Packet.rpt_list[k - 1], sizeof(AX25Call));
        if (Packet.rpt_flags & (1 << (k - 1)))
            Packet.rpt_flags |= (1 << k);
        else
            Packet.rpt_flags &= ~(1 << k);
    }
    Packet.rpt_count += 1;
}

int digiProcess(AX25Msg &Packet)
{
    int idx, j;
    uint8_t ctmp;
    // if(!DIGI) return;
    // if(rx_data) return;
    // if(digi_timeout<aprs_delay) return;
    // digi_timeout = 65530;
    // aprs_delay = 65535;

    j = 0;
    if (Packet.len < 5)
    {
        digiLog.ErPkts++;
        return 0; // NO DST
    }

    if (!strncmp(&Packet.src.call[0], "NOCALL", 6))
    {
        digiLog.DropRx++;
        return 0;
    }
    if (!strncmp(&Packet.src.call[0], "MYCALL", 6))
    {
        digiLog.DropRx++;
        return 0;
    }

    // Destination SSID Trace
    if (Packet.dst.ssid > 0)
    {
        uint8_t ctmp = Packet.dst.ssid & 0x1E; // Check DSSID

        if (ctmp > 15)
            ctmp = 0;
        if (ctmp < 8)
        { // Edit PATH Change to TRACEn-N
            if (ctmp > 0)
                ctmp--;
            Packet.dst.ssid = ctmp;
            if (Packet.rpt_count > 0)
            {
                for (idx = 0; idx < Packet.rpt_count; idx++)
                {
                    if (!strcmp(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0])) // Is path same callsign
                    {
                        if (Packet.rpt_list[idx].ssid == config.digi_ssid) // IS path same SSID
                        {
                            if (Packet.rpt_flags & (1 << idx))
                            {
                                digiLog.DropRx++;
                                return 0; // bypass flag *
                            }
                            Packet.rpt_flags |= (1 << idx);
                            return 1;
                        }
                    }
                    if (Packet.rpt_flags & (1 << idx))
                        continue;
                    for (j = idx; j < Packet.rpt_count; j++)
                    {
                        if (Packet.rpt_flags & (1 << j))
                            break;
                    }
                    // Move current part to next part
                    for (; j >= idx; j--)
                    {
                        int n = j + 1;
                        strcpy(&Packet.rpt_list[n].call[0], &Packet.rpt_list[j].call[0]);
                        Packet.rpt_list[n].ssid = Packet.rpt_list[j].ssid;
                        if (Packet.rpt_flags & (1 << j))
                            Packet.rpt_flags |= (1 << n);
                        else
                            Packet.rpt_flags &= ~(1 << n);
                    }

                    // Add new part
                    Packet.rpt_count += 1;
                    strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
                    Packet.rpt_list[idx].ssid = config.digi_ssid;
                    Packet.rpt_flags |= (1 << idx);
                    return 2;
                    // j = 1;
                    // break;
                }
            }
            else
            {
                idx = 0;
                strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
                Packet.rpt_list[idx].ssid = config.digi_ssid;
                Packet.rpt_flags |= (1 << idx);
                Packet.rpt_count += 1;
                return 2;
            }
        }
        else
        {
            digiLog.DropRx++;
            return 0; // NO PATH
        }
    }

    for (idx = 0; idx < Packet.rpt_count; idx++)
    {
        if (!strncmp(&Packet.rpt_list[idx].call[0], "qA", 2))
        {
            digiLog.DropRx++;
            return 0;
        }
    }

    for (idx = 0; idx < Packet.rpt_count; idx++)
    {
        if (!strncmp(&Packet.rpt_list[idx].call[0], "TCP", 3))
        {
            digiLog.DropRx++;
            return 0;
        }
    }

    // Loop protection: this digi has already repeated the packet
    for (idx = 0; idx < Packet.rpt_count; idx++)
    {
        if ((Packet.rpt_flags & (1 << idx)) && !strcmp(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]) && (Packet.rpt_list[idx].ssid & 0x0F) == config.digi_ssid)
        {
            digiLog.DropRx++;
            return 0;
        }
    }

    for (idx = 0; idx < Packet.rpt_count; idx++)
    {
        if (Packet.rpt_flags & (1 << idx))
        {
            if (idx == (Packet.rpt_count - 1))
                digiCount++;
            continue; // bypass flag *
        }
        int wn = wideN(&Packet.rpt_list[idx].call[0]);
        if (wn > 0)
        {
            // New-N paradigm (WB4APR, aprs.org/fix14439.html), traceable WIDEn-N.
            // Mechanics as Dire Wolf digipeat_match():
            //   N == 1      : replace WIDEn-1 with MYCALL, mark used        WIDE2-1      -> MYCALL*
            //   N  > 1      : insert MYCALL (used) ahead, decrement N         WIDE2-2      -> MYCALL*,WIDE2-1
            //                 (no insert if path already holds 8 digis)
            //   n > maxhop  : trap, repeat once and terminate             WIDE5-5      -> MYCALL*
            //   fill-in mode: only WIDE1-1 is handled
            uint8_t nn = Packet.rpt_list[idx].ssid & 0x0F;
            if (nn < 1 || nn > 7 || nn > wn)
            {
                digiLog.DropRx++;
                j = 0; // WIDEn-0 unused, N>7 or N>n: invalid, not repeated
                break;
            }
            if (config.digi_fillin && wn != 1)
            {
                j = 0; // fill-in digi: WIDE2-N and up belong to wide-area digis
                break;
            }
            uint8_t maxhop = config.digi_maxhop;
            if (maxhop < 1 || maxhop > 7)
                maxhop = 2;
            if (nn == 1 || wn > maxhop)
            {
                setMyCallUsed(Packet, idx);
                j = 2;
                break;
            }
            Packet.rpt_list[idx].ssid = nn - 1;
            Packet.rpt_flags &= ~(1 << idx);
            if (Packet.rpt_count < AX25_MAX_RPT)
            {
                insertSlot(Packet, idx);
                setMyCallUsed(Packet, idx);
            }
            j = 2;
            break;
        }
        else if (config.digi_legacy && !strcmp(&Packet.rpt_list[idx].call[0], "WIDE") && Packet.rpt_list[idx].ssid == 0)
        {
            // Obsolete bare WIDE alias, only with legacy enabled
            setMyCallUsed(Packet, idx);
            j = 2;
            break;
        }
        else if (config.digi_legacy && !strncmp(&Packet.rpt_list[idx].call[0], "TRACE", 5))
        {
            if (Packet.rpt_flags & (1 << idx))
                continue; // bypass flag *
            ctmp = Packet.rpt_list[idx].ssid & 0x1F;
            if (ctmp > 0)
                ctmp--;
            if (ctmp > 15)
                ctmp = 0;
            if (ctmp == 0)
            {
                strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
                Packet.rpt_list[idx].ssid = config.digi_ssid;
                Packet.rpt_flags |= (1 << idx);
                j = 2;
                break;
            }
            else
            {
                for (j = idx; j < Packet.rpt_count; j++)
                {
                    if (Packet.rpt_flags & (1 << j))
                        break;
                }
                // Move current part to next part
                for (; j >= idx; j--)
                {
                    int n = j + 1;
                    strcpy(&Packet.rpt_list[n].call[0], &Packet.rpt_list[j].call[0]);
                    Packet.rpt_list[n].ssid = Packet.rpt_list[j].ssid;
                    if (Packet.rpt_flags & (1 << j))
                        Packet.rpt_flags |= (1 << n);
                    else
                        Packet.rpt_flags &= ~(1 << n);
                }
                // Reduce N part of TRACEn-N
                Packet.rpt_list[idx + 1].ssid = ctmp;

                // Add new part
                Packet.rpt_count += 1;
                strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
                Packet.rpt_list[idx].ssid = config.digi_ssid;
                Packet.rpt_flags |= (1 << idx);
                j = 2;
                break;
            }
        }
        else if (!strncmp(&Packet.rpt_list[idx].call[0], "RFONLY", 6))
        {
            j = 2;
            // strcpy(&Packet.rpt_list[idx].call[0], &config.aprs_mycall[0]);
            // Packet.rpt_list[idx].ssid = config.aprs_ssid;
            Packet.rpt_flags |= (1 << idx);
            break;
        }
        else if (config.digi_legacy && !strncmp(&Packet.rpt_list[idx].call[0], "RELAY", 5))
        {
            j = 2;
            strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
            Packet.rpt_list[idx].ssid = config.digi_ssid;
            Packet.rpt_flags |= (1 << idx);
            break;
        }
        else if (config.digi_legacy && !strncmp(&Packet.rpt_list[idx].call[0], "GATE", 4))
        {
            j = 2;
            strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
            Packet.rpt_list[idx].ssid = config.digi_ssid;
            Packet.rpt_flags |= (1 << idx);
            break;
        }
        else if (config.digi_legacy && !strncmp(&Packet.rpt_list[idx].call[0], "ECHO", 4))
        {
            j = 2;
            strcpy(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0]);
            Packet.rpt_list[idx].ssid = config.digi_ssid;
            Packet.rpt_flags |= (1 << idx);
            break;
        }
        else if (!strcmp(&Packet.rpt_list[idx].call[0], &config.digi_mycall[0])) // Is path same callsign
        {
            ctmp = Packet.rpt_list[idx].ssid & 0x1F;
            if (ctmp == config.digi_ssid) // IS path same SSID
            {
                if (Packet.rpt_flags & (1 << idx))
                {
                    digiLog.DropRx++;
                    break; // bypass flag *
                }
                Packet.rpt_flags |= (1 << idx);
                j = 1;
                break;
            }
            else
            {
                j = 0;
                break;
            }
        }
        else
        {
            j = 0;
            break;
        }
    }
    return j;
}