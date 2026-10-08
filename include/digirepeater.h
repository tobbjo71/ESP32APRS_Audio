#ifndef DIGIREPEATER_H
#define DIGIREPEATER_H

#include <AX25.h>

int digiProcess(AX25Msg &Packet);
bool digiDupeSeen(const AX25Msg &Packet);
void digiDupeRemember(const AX25Msg &Packet);

#endif