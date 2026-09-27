/****************************************************************************
 * BS-X GX
 *
 * station.h
 *
 * Tunes in to a station and keeps the satellite fed with what's on air
 ***************************************************************************/

#ifndef _STATION_H_
#define _STATION_H_

enum
{
	STATION_OFF,
	STATION_TUNING,
	STATION_ONAIR,
	STATION_ERROR
};

struct StationStatus
{
	int state;
	char title[64];
	char next[64];
	int secondsLeft;
	char error[64];
};

void StationStart(const char *url);
void StationRefresh();
void StationGetStatus(StationStatus *out);

#endif
