#include <windows.h>
#include <stdio.h>
#include <dirent.h>
#include "Header.h"

char aName[10][MAX_SIZE] = { 0 };
char aLevel[10][MAX_SIZE] = { 0 };

int main()
{
	DIR *dir;
	struct dirent *ent;

	FILE *fpointer;
	char aNam[MAX_SIZE] = { 0 };// current name
	char aLev[MAX_SIZE] = { 0 };// current level
	char aStatus[MAX_SIZE] = { 0 };// current status
	char filepath[MAX_SIZE] = { 0 };

	// open directory
	dir = opendir(FOLDERPATH_ACCOUNTS);

	if (dir != NULL)
	{
		/* print all the files and directories within directory */
		while ((ent = readdir(dir)) != NULL)
		{
			if (strcmp(ent->d_name, ".") && strcmp(ent->d_name, ".."))
			{
				snprintf(filepath, MAX_SIZE, "%s/%s", FOLDERPATH_ACCOUNTS, ent->d_name);

				// Name
				GetFromFile(18, aNam, MAX_SIZE, filepath);
				// Level
				GetFromFile(8, aLev, MAX_SIZE, filepath);
				// status
				GetFromFile(2, aStatus, MAX_SIZE, filepath);

				// don't list frozen accounts
				if (!strcmp(aStatus, "1"))
					continue;

				// if name empty
				if (aNam[0] == NULL)
					strncpy(aNam, "unknown", MAX_SIZE);

				// compare with list
				for (int i = 0; i < 10; ++i)
				{
					if (atoi(aLev) > atoi(aLevel[i]))
					{
						InsertPlace(i, aNam, aLev);
						break;
					}
				}
			}
		}
		closedir(dir);
	}
	else
	{
//		printf("Err in open dir\n");
//		system("pause");
		return false;
	}
/*
	for (int i = 0; i < 10; ++i)
	{
		printf("name: %s\n", aName[i]);
		printf("level: %s\n", aLevel[i]);
	}
	system("pause");
*/
	// write buffer to the top ten file
	fpointer = fopen(FILEPATH_TOPTEN, "w");

	for (int i = 0; i < 10; i++)
	{
		if (aName[i][0] == NULL)
			strcpy(aName[i], "unknown");
		fprintf(fpointer, aName[i]);
		fprintf(fpointer, "\n");
		if (aLevel[i][0] == NULL)
			strcpy(aLevel[i], "0");
		fprintf(fpointer, aLevel[i]);
		fprintf(fpointer, "\n");
	}

	fclose(fpointer);
	return true;
}

bool GetFromFile(int Position, char *Buffer, unsigned int Size, const char *Filepath)
{
	FILE *fpointer;

	int counter = 0;
	char ch = 0;

	char aLineBuf[256] = { 0 };

	fpointer = fopen(Filepath, "r");

	while (1)
	{
		ch = fgetc(fpointer);

		if (ch == EOF)
			break;

		if (ch == '\n')
		{
			counter++;
		}
		else
		{
			if (counter == Position)
			{
				snprintf(aLineBuf, sizeof(aLineBuf), "%s%c", aLineBuf, ch);
			}
		}

		if (counter > Position)
			break;
	}

	strncpy(Buffer, aLineBuf, Size);

	fclose(fpointer);

	return true;
}

void InsertPlace(int Position, char* Name, char* Level)
{
	for (int i = 9; i >= Position; --i)
	{
		if (i == 9)
			continue;

		strncpy(aName[i + 1], aName[i], MAX_SIZE);
		strncpy(aLevel[i + 1], aLevel[i], MAX_SIZE);

		if (i == Position)
		{
			strncpy(aName[i], Name, MAX_SIZE);
			strncpy(aLevel[i], Level, MAX_SIZE);
		}
	}
}