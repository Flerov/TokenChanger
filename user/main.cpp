#include <Windows.h>
#include <iostream>
#include <Psapi.h>
#include <strsafe.h>
#include <tchar.h>

#define BUFSIZE 1024
#define IOCTL_CODE 0x815
#define IOCTL_CODE_TWO 0x816
#define SERVICE "DISPLAYSERVICENAME"
#define DEVICE "\\\\.\\DISPLAYDEVICENAME"

void main(int argc, char* argv[]);
HANDLE getHandle();
BOOL GetFileNameFromHandle(HANDLE hFile);


void main(int argc, char* argv[])
{
	unsigned int pid = atoi(argv[1]);
	unsigned int pidtwo = atoi(argv[2]);
	std::cout << "[*]Prepared argument PID one: ";
	std::cout << pid;
	std::cout << "\n[*]Prepared argument PID two: ";
	std::cout << pidtwo;
	std::cout << "\n[+]Call to driver...\n";

	ULONG ret_bytes;
	char* retbuf[1024];
	HANDLE hDevice;
	unsigned int msg = pid;
	char* msg_c = argv[1];
	char* msg_two_c = argv[2];

	hDevice = getHandle();
	BOOLEAN com = DeviceIoControl
	(
		hDevice,
		IOCTL_CODE,
		argv[1],
		strlen(msg_c) + 1, // if it doesn't work do len() + 2
		retbuf,
		200,
		&ret_bytes,
		(LPOVERLAPPED)NULL
	);

	BOOLEAN comtwo = DeviceIoControl
	(
		hDevice,
		IOCTL_CODE_TWO,
		argv[2],
		strlen(msg_two_c) + 1, // if it doesn't work do len() + 2
		retbuf,
		200,
		&ret_bytes,
		(LPOVERLAPPED)NULL
	);

	if (!com || !comtwo)
	{
		std::cout << "[-] Unable to send IRP to driver.\n";
		exit(0);
	}

	std::cout << "[+] IRP sent, check DBG!\nAnswer recieved-> ";
	std::cout << &retbuf;
}

HANDLE getHandle()
{
	SC_HANDLE hSCManager = NULL;
	SC_HANDLE hService = NULL;
	HANDLE hDevice = NULL;

	hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (hSCManager == NULL)
	{
		std::cout << "[-] Opening SCManager Database failed!\n";
		goto clean;
	}

	hService = OpenService(hSCManager, TEXT(SERVICE), SERVICE_ALL_ACCESS);
	if (hService == NULL)
	{
		std::cout << "[-] The specified SERVICE doesn't exist.\n";
		goto clean;
	}

	hDevice = CreateFile
	(
		TEXT(DEVICE),
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (hDevice == INVALID_HANDLE_VALUE)
	{
		if (!GetFileNameFromHandle(hDevice))
		{
			std::cout << "[-] Handle is buggy.\n";
			goto clean;
		}
	}

clean:
	CloseServiceHandle(hService);
	CloseServiceHandle(hSCManager);
	if (hDevice) { return hDevice; }
	return NULL;
}

BOOL GetFileNameFromHandle(HANDLE hFile)
{
	BOOL bSuccess = FALSE;
	TCHAR pszFilename[MAX_PATH + 1];
	HANDLE hFileMap;

	// Get the file size.
	DWORD dwFileSizeHi = 0;
	DWORD dwFileSizeLo = GetFileSize(hFile, &dwFileSizeHi);

	if (dwFileSizeLo == 0 && dwFileSizeHi == 0)
	{
		std::cout << (TEXT("Cannot map a file with a length of zero.\n"));
		return FALSE;
	}

	// Create a file mapping object.
	hFileMap = CreateFileMapping(hFile,
		NULL,
		PAGE_READONLY,
		0,
		1,
		NULL);

	if (hFileMap)
	{
		// Create a file mapping to get the file name.
		void* pMem = MapViewOfFile(hFileMap, FILE_MAP_READ, 0, 0, 1);

		if (pMem)
		{
			if (GetMappedFileName(GetCurrentProcess(),
				pMem,
				pszFilename,
				MAX_PATH))
			{

				// Translate path with device name to drive letters.
				TCHAR szTemp[BUFSIZE];
				szTemp[0] = '\0';

				if (GetLogicalDriveStrings(BUFSIZE - 1, szTemp))
				{
					TCHAR szName[MAX_PATH];
					TCHAR szDrive[3] = TEXT(" :");
					BOOL bFound = FALSE;
					TCHAR* p = szTemp;

					do
					{
						// Copy the drive letter to the template string
						*szDrive = *p;

						// Look up each device name
						if (QueryDosDevice(szDrive, szName, MAX_PATH))
						{
							size_t uNameLen = _tcslen(szName);

							if (uNameLen < MAX_PATH)
							{
								bFound = _tcsnicmp(pszFilename, szName, uNameLen) == 0
									&& *(pszFilename + uNameLen) == _T('\\');

								if (bFound)
								{
									// Reconstruct pszFilename using szTempFile
									// Replace device path with DOS path
									TCHAR szTempFile[MAX_PATH];
									StringCchPrintf(szTempFile,
										MAX_PATH,
										TEXT("%s%s"),
										szDrive,
										pszFilename + uNameLen);
									StringCchCopyN(pszFilename, MAX_PATH + 1, szTempFile, _tcslen(szTempFile));
								}
							}
						}

						// Go to the next NULL character.
						while (*p++);
					} while (!bFound && *p); // end of string
				}
			}
			bSuccess = TRUE;
			UnmapViewOfFile(pMem);
		}

		CloseHandle(hFileMap);
	}
	std::cout << (TEXT("File name is %s\n"), pszFilename);
	return(bSuccess);
}
