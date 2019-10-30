#include <stdio.h>
#include <string.h>
#include <Windows.h>
#include <time.h>

int main()
{
	FILE *fpointer;

	char aUsableChars[] = { "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ" };
	char aCode[7] = { 0 };
	int Type = 0;
	int Val1 = 0;
	int rannum = 0;

	char aFilePathComplete[256] = { 0 };

	// reset
	// level amount: 10, 25, 50, 100 (10, 25, 50, 100 upgrades)
	// money amount: 25, 50, 75, 100 (5, 10, 15, 20 upgrades)
	// events duration: 15 min

	// create directories if they don't exist yet
	DWORD attribs = ::GetFileAttributesA("redeembucket");
	if (attribs == INVALID_FILE_ATTRIBUTES)
	{
		// create directory if it doesnt yet exist
		if (!CreateDirectoryA("redeembucket", NULL))
			return false;

		// create directory if it doesnt yet exist
		if (!CreateDirectoryA("redeembucket/reset", NULL))
			return false;
		// create directory if it doesnt yet exist
		if (!CreateDirectoryA("redeembucket/level", NULL))
			return false;
		// create directory if it doesnt yet exist
		if (!CreateDirectoryA("redeembucket/money", NULL))
			return false;
		// create directory if it doesnt yet exist
		if (!CreateDirectoryA("redeembucket/events", NULL))
			return false;

		// create directory if it doesnt yet exist
		if (!CreateDirectoryA("redeembucket/level/10", NULL))
			return false;
		// create directory if it doesnt yet exist
		if (!CreateDirectoryA("redeembucket/level/25", NULL))
			return false;
		// create directory if it doesnt yet exist
		if (!CreateDirectoryA("redeembucket/level/50", NULL))
			return false;
		// create directory if it doesnt yet exist
		if (!CreateDirectoryA("redeembucket/level/100", NULL))
			return false;

		// create directory if it doesnt yet exist
		if (!CreateDirectoryA("redeembucket/money/25", NULL))
			return false;
		// create directory if it doesnt yet exist
		if (!CreateDirectoryA("redeembucket/money/50", NULL))
			return false;
		// create directory if it doesnt yet exist
		if (!CreateDirectoryA("redeembucket/money/75", NULL))
			return false;
		// create directory if it doesnt yet exist
		if (!CreateDirectoryA("redeembucket/money/100", NULL))
			return false;
	}

	// random seed
	srand(time(NULL));

	printf("Generating codes...\n");

	// 100 codes for each category, each 25 steps change value class
	for (int i = 0; i < 800; ++i)
	{
		// generate random code
		for (int ii = 0; ii < 6; ++ii)
		{
			rannum = rand() % strlen(aUsableChars);

			aCode[ii] = aUsableChars[rannum];
		}

		if (i < 100)// reset codes
		{
			Type = 0;
			Val1 = 0;

			snprintf(aFilePathComplete, sizeof(aFilePathComplete), "redeembucket/reset/%s.ini", aCode);
		}
		else if (i < 200)// level codes
		{
			Type = 1;

			if (i < 125)
				Val1 = 10;
			else if (i < 150)
				Val1 = 25;
			else if (i < 175)
				Val1 = 50;
			else
				Val1 = 100;

			snprintf(aFilePathComplete, sizeof(aFilePathComplete), "redeembucket/level/%d/%s.ini", Val1, aCode);
		}
		else if (i < 300)// money codes
		{
			Type = 2;
				
			if (i < 225)
				Val1 = 25;
			else if (i < 250)
				Val1 = 50;
			else if (i < 275)
				Val1 = 75;
			else
				Val1 = 100;

			snprintf(aFilePathComplete, sizeof(aFilePathComplete), "redeembucket/money/%d/%s.ini", Val1, aCode);
		}
		else// event codes
		{
			Val1 = 15;// duration of event in minutes

			aCode[0] = 'E';

			if (i < 400)
				Type = 100;
			else if (i < 500)
				Type = 101;
			else if (i < 600)
				Type = 102;
			else if (i < 700)
				Type = 103;
			else
				Type = 104;

			aCode[1] = '0' + Type - 100;

			snprintf(aFilePathComplete, sizeof(aFilePathComplete), "redeembucket/events/%s.ini", aCode);
		}

		fpointer = fopen(aFilePathComplete, "w");
		fprintf(fpointer, "%s\n%d\n%d", aCode, Type, Val1);
		fclose(fpointer);

		// console output
//		printf("%s\n%d\n%d\n", aCode, Type, Val1);
	}
	
//	system("pause");
}