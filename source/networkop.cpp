/****************************************************************************
 * Snes9x Nintendo Wii/Gamecube Port
 *
 * Tantric 2008-2023
 *
 * networkop.cpp
 *
 * Network support routines
 ****************************************************************************/

#include <errno.h>
#include <network.h>
#include <ogc/lwp_watchdog.h>

#include "snes9xgx.h"
#include "menu.h"

static bool networkInit = false;
char wiiIP[16] = { 0 };

static int netHalt = 0;

/****************************************************************************
 * InitializeNetwork
 * Initializes the Wii/GameCube network interface
 ***************************************************************************/

static lwp_t networkthread = LWP_THREAD_NULL;

static void * netcb (void *arg)
{
	s32 res=-1;
	int retry;
	int wait;
	static bool prevInit = false;

	while(netHalt != 2)
	{
		retry = 5;
		
		while (retry>0 && (netHalt != 2))
		{			
			if(prevInit) 
			{
				int i;
				net_deinit();
				for(i=0; i < 400 && (netHalt != 2); i++) // 10 seconds to try to reset
				{
					res = net_get_status();
					if(res != -EBUSY) // trying to init net so we can't kill the net
					{
						usleep(2000);
						net_wc24cleanup(); //kill the net 
						prevInit=false; // net_wc24cleanup is called only once
						usleep(20000);
						break;					
					}
					usleep(20000);
				}
			}

			usleep(2000);
			res = net_init_async(NULL, NULL);

			if(res != 0)
			{
				sleep(1);
				retry--;
				continue;
			}

			res = net_get_status();
			wait = 400; // only wait 8 sec
			while (res == -EBUSY && wait > 0  && (netHalt != 2))
			{
				usleep(20000);
				res = net_get_status();
				wait--;
			}

			if(res==0) break;
			retry--;
			usleep(2000);
		}
		if (res == 0)
		{
			struct in_addr hostip;
			hostip.s_addr = net_gethostip();
			if (hostip.s_addr)
			{
				inet_ntoa_r(hostip, wiiIP, sizeof(wiiIP));
				networkInit = true;	
				prevInit = true;
			}
		}
		if(netHalt != 2) LWP_SuspendThread(networkthread);
	}
	return NULL;
}

/****************************************************************************
 * StartNetworkThread
 *
 * Signals the network thread to resume, or creates a new thread
 ***************************************************************************/
void StartNetworkThread()
{
	netHalt = 0;

	if(networkthread == LWP_THREAD_NULL)
		LWP_CreateThread(&networkthread, netcb, NULL, NULL, 0, 40);
	else
		LWP_ResumeThread(networkthread);
}

/****************************************************************************
 * StopNetworkThread
 *
 * Signals the network thread to stop
 ***************************************************************************/
void StopNetworkThread()
{
	if(networkthread == LWP_THREAD_NULL || !LWP_ThreadIsSuspended(networkthread))
		return;

	netHalt = 2;
	LWP_ContinueThread(networkthread);

	// wait for thread to finish
	LWP_JoinThread(networkthread, NULL);
	networkthread = LWP_THREAD_NULL;
}

bool InitializeNetwork(bool silent)
{
	StopNetworkThread();

	if(networkInit && net_gethostip() > 0)
		return true;

	networkInit = false;

	int retry = 1;

	while(retry)
	{
		ShowAction("Initializing network...");

		u64 start = gettime();
		StartNetworkThread();

		while (!LWP_ThreadIsSuspended(networkthread))
		{
			usleep(50 * 1000);

			if(diff_sec(start, gettime()) > 10) // wait for 10 seconds max for net init
				break;
		}

		CancelAction();

		if(networkInit || silent)
			break;

		retry = ErrorPromptRetry("Unable to initialize network!");
		
		if(networkInit && net_gethostip() > 0)
			return true;
	}
	return networkInit;
}

