#pragma once
//#define _CRT_SECURE_NO_WARNINGS
#define FOLDERPATH_ACCOUNTS "accounts"
#define FILEPATH_TOPTEN "topten/topten.ini"
#define MAX_SIZE 128

bool GetFromFile(int Position, char *Buffer, unsigned int Size, const char *Filepath);
void InsertPlace(int Position, char* Name, char* Level);