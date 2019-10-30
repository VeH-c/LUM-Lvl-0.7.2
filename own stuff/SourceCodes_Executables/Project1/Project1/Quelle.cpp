//#include <windows.h>
#include <chrono>
#include <thread>
#include <stdio.h>

#include "Header.h"

int main()
{
	unsigned long long cnt = 0;
	std::chrono::seconds interval(WAITTIME);

	while(1)
	{
		cnt++;

		system("START /D \"%cd%\\servers\\0-30\" teeworlds_srv.exe");
		system("START /D \"%cd%\\servers\\30-75\" teeworlds_srv.exe");
		system("START /D \"%cd%\\servers\\75-120\" teeworlds_srv.exe");
		system("START /D \"%cd%\\servers\\120-300\" teeworlds_srv.exe");
		system("START /D \"%cd%\\servers\\public\" teeworlds_srv.exe");

		// call oracle each hour
		if ((cnt - 1) % (3600 / WAITTIME) == 0)
		{
			system("START /D \"%cd%\" oracle.exe");
			printf("oracle instance called...\n");
		}

		printf("[%.2fh]: calls: %d sleep for %d seconds\n", (float)(cnt * WAITTIME) / 3600, (int)cnt, interval);
		std::this_thread::sleep_for(interval);
	}

	return 0;
}

/*
	// start all servers once
	system("START /D \"%cd%\\servers\\0-30\" teeworlds_srv.exe");
	system("START /D \"%cd%\\servers\\30-75\" teeworlds_srv.exe");
	system("START /D \"%cd%\\servers\\75-120\" teeworlds_srv.exe");
	system("START /D \"%cd%\\servers\\120-300\" teeworlds_srv.exe");
	system("START /D \"%cd%\\servers\\public\" teeworlds_srv.exe");

	// call oracle each hour
	while(1)
	{
		system("START /D \"%cd%\" oracle.exe");

		printf("[%.2fh]: Oracle instance called (%lld calls, sleep for %d seconds)\n", (float)(cnt * WAITTIME) / 3600, (int)cnt + 1, interval);

		cnt++;
		std::this_thread::sleep_for(interval);
	}
*/