// HID_Sample_App.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <windows.h>
#include <stdlib.h>
#include <wtypes.h>
#include <math.h>
#include <hidsdi.h>
#include "hid.h"
#include "HID_Sample_App.h"
#include "fileapi.h"
#include "string.h"

typedef struct cmdlineArgs_t
{
	UINT16 vendorId;
	UINT16 productId;
	CHAR* fwImagePath;
	BOOL programDevice;
	BOOL fallbackToBoot;
}cmdlineArgs_t;


cmdlineArgs_t cmdLineArgs;

/* Various status that Device reports*/
#define STATUS_SUCCESS                    0
#define STATUS_DEVICE_NOT_FOUND       1
#define STATUS_ERASE_FAILED           2
#define STATUS_RESET_FAILED           3
#define STATUS_INVALID_ARGUMENT           4
#define STATUS_INVALID_FILE_HANDLE        5
#define STATUS_ISP_UCODE_DOWNLOAD_FAILED  6
#define STATUS_ISP_FW_DOWNLOAD_FAILED     7
#define STATUS_SUSPEND_EXIT_FAILED    8
#define STATUS_FW_DOWNLOAD_FAILED     11		
#define STATUS_PENDING                    12		
#define STATUS_SECURITY_LOCKED        13		
#define STATUS_GET_FW_VERSION_FAILED  14
#define STATUS_ISP_GET_FW_VERSION_FAILED  15
#define STATUS_ISP_FW_START_FAILED        16

#define STATUS_HID_GET_REPORT_FAILED      0xA0	/* Increasing this number to higher value as this is an error related to tool*/
#define STATUS_HID_WRITE_API_FAILED		  0xA1
#define BUFFER_SIZE 256

BOOL glIsdeviceInSuspendMode = FALSE;
void GetDevicePath(int deviceNumber, GUID guid, char* path)
{
	//Initialization

	size_t   i;
	char* DevPath = (char*)malloc(BUFFER_SIZE);
	SP_DEVINFO_DATA devInfoData;
	SP_DEVICE_INTERFACE_DATA  devInterfaceData;
	PSP_INTERFACE_DEVICE_DETAIL_DATA functionClassDeviceData;
	ULONG requiredLength = 0;

	HDEVINFO hwDeviceInfo = SetupDiGetClassDevs((LPGUID)&guid,   //Returns a handle to the device information set
		NULL,
		NULL,
		DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);

	if (hwDeviceInfo != INVALID_HANDLE_VALUE) {                        //checks if the handle is invalid 
		devInterfaceData.cbSize = sizeof(devInterfaceData);			   //get the size of devInterfaceData structure

																	   //enumerates the device interfaces that are contained in a device information set
		if (SetupDiEnumDeviceInterfaces(hwDeviceInfo, 0, (LPGUID)&guid,
			deviceNumber, &devInterfaceData)) {

			SetupDiGetInterfaceDeviceDetail(hwDeviceInfo, &devInterfaceData, NULL, 0,
				&requiredLength, NULL);
			ULONG predictedLength = requiredLength;
			functionClassDeviceData = (PSP_INTERFACE_DEVICE_DETAIL_DATA)malloc(predictedLength);
			functionClassDeviceData->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);
			devInfoData.cbSize = sizeof(devInfoData);

			//Retrieve the information from Plug and Play including the device path
			if (SetupDiGetInterfaceDeviceDetail(hwDeviceInfo,
				&devInterfaceData,
				functionClassDeviceData,
				predictedLength,
				&requiredLength,
				&devInfoData))
			{
				//wprintf(L"%ls\n", functionClassDeviceData->DevicePath); //Print the device path of the required device

				int pathLen = requiredLength - functionClassDeviceData->cbSize;
				int ret = wcstombs_s(&i, path, (size_t)BUFFER_SIZE, functionClassDeviceData->DevicePath, (size_t)pathLen);
				if (ret != 0) {
					printf("Error encountered during device path conversion, error no = %d\n", ret);
					exit(1);
				}
				//printf("%s\n", path);


			}
		}
	}

	SetupDiDestroyDeviceInfoList(hwDeviceInfo);
}

#define PBSTR "||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||"
#define PBWIDTH 60

void printProgress(double percentage) {
	int val = (int)(percentage * 100);
	int lpad = (int)(percentage * PBWIDTH);
	int rpad = PBWIDTH - lpad;
	printf("\r%3d%% [%.*s%*s]", val, lpad, PBSTR, rpad, "");
	fflush(stdout);
}

static BOOL
ParseArguments(
	int argc,
	char* argv[])
{
	char* arg;

	/* Skip argument 0: Program name. */
	argc--;
	argv++;

	cmdLineArgs.fallbackToBoot = FALSE;
	cmdLineArgs.programDevice = FALSE;

	while (argc-- > 0)
	{
		arg = *argv++;

		if (_stricmp(arg, "-vid") == 0)
		{
			if (argc < 1)
			{
				printf("Vendor ID needs to be specified\r\n");
				return FALSE;
			}
			arg = *argv++;
			cmdLineArgs.vendorId = strtoul(arg, NULL,0);
			argc--;
			if (!cmdLineArgs.vendorId)
			{
				printf("Invalid Vendor ID detected - %04X\r\n", cmdLineArgs.vendorId);
			}
			else
			{
				printf("Vendor ID: 0x%04X\r\n", cmdLineArgs.vendorId);
			}
			continue;
		}


		if (_stricmp(arg, "-pid") == 0)
		{
			if (argc < 1)
			{
				printf("Product ID needs to be specified\r\n");
				return FALSE;
			}
			arg = *argv++;
			cmdLineArgs.productId = strtoul(arg, NULL, 0);
			argc--;
			if (!cmdLineArgs.productId)
			{
				printf("Invalid Product ID detected - %04X\r\n", cmdLineArgs.productId);
			}
			else
			{
				printf("Product ID:  0x%04X\r\n", cmdLineArgs.productId);
			}
			continue;
		}

		if (_stricmp(arg, "-fw") == 0)
		{
			if (argc < 1)
			{
				printf("Firmware Path needs to be specified\r\n");
				return FALSE;
			}

			cmdLineArgs.fwImagePath = *argv++;
			printf("Firmware Path: %s\r\n", cmdLineArgs.fwImagePath);
			cmdLineArgs.programDevice = TRUE;
			argc--;
			continue;
		}

		if (_stricmp(arg, "-reset") == 0)
		{
			cmdLineArgs.fallbackToBoot = TRUE;
			argc--;
			continue;
		}

		printf("%s is an invalid argument\r\n", arg);
		return FALSE;
	}

	return TRUE;
}





int main(int argc, char* argv[])
{
	BOOL status = FALSE;
	BOOL isCXFWv111orLater = FALSE;
	UCHAR enteredChoice, ISPFirmwareFileName[128], dummy, *write_buf, *read_buf, inBuf[64], inCount, outBuf[512], tempBuf[512] = { 0 }, hidProdCmd;
	ULONG FileWriteSize = 0, firmwaresize = 0, offset = 0, EP_Out_Packet_Size = 1024, EP_In_Packet_Size = 1024, buflen, retStatus;
	ULONG numDevices = 0, i = 0, HIDDeviceNum = 0, FoundHIDDevice = 0, localCounter, prevTime;
	PHID_DEVICE deviceList = NULL;
	FILE* firmwarefile = NULL;
	FILE* logfile = NULL;
	DWORD LastError;
	GUID GUID_HID;
	char devicepath[BUFFER_SIZE];
	char IntNum[3];
	ULONG firmwareFileSize = 0;

	SYSTEMTIME st;
	BOOL bEraseFailed = FALSE;


	if (!ParseArguments(argc, argv))
	{
		printf("\r\n*********************** Usage Example ***********************\r\n");
		printf("HID_Sample_App.exe -vid <Vendor ID> -pid <Product ID> -fw <fw_image.img>\r\n");
		printf("For eg:\r\nHID_Sample_App.exe -vid 04b4 -pid 00c2 -fw USBVideoBridge.img\r\n");
		printf("*************************************************************\r\n");
		return;
	}



	FindKnownHidDevices(&deviceList, &numDevices);
	while (numDevices)
	{

		if (INVALID_HANDLE_VALUE != deviceList[i].HidDevice)
		{
			if (((deviceList[i].Attributes.VendorID == cmdLineArgs.vendorId) && (deviceList[i].Attributes.ProductID == cmdLineArgs.productId)))
			{
				HidD_GetHidGuid(&GUID_HID);
				GetDevicePath(i, GUID_HID, devicepath);
				memcpy(&IntNum, &devicepath[29], (size_t)2);
				IntNum[2] = '\0';


				printf("Found HID device: VID: 0x%04X  PID: 0x%04X \n", deviceList[i].Attributes.VendorID, deviceList[i].Attributes.ProductID);
				printf("HID Interface number = %s\n", IntNum);

				HIDDeviceNum = i;

				HidDevHandle = deviceList[HIDDeviceNum].HidDevice;
				HidDevice = &deviceList[HIDDeviceNum];
				FoundHIDDevice = 1;
				break;
			}
		}
		numDevices--;
		i++;
	}

	if (!FoundHIDDevice)
	{
		printf("Did not find HID Device...\r\n");
		printf("Connect HID Device...and try again\r\n");
		retStatus = STATUS_DEVICE_NOT_FOUND;
		exit(retStatus);
	}

	if (argc == 1)
	{
		printf("\r\n*********************** Usage Example ***********************\r\n");
		printf("HID_Sample_App.exe -vid <Vendor ID> -pid <Product ID> -fw <fw_image.img>\r\n");
		printf("For eg:\r\nHID_Sample_App.exe -vid 04b4 -pid 00c2 -fw USBVideoBridge.img\r\n");
		printf("*************************************************************\r\n");
		return;
	}
	


	if (cmdLineArgs.fallbackToBoot)
	{
		printf("\r\n\r\nStep 1: Enable SPI\r\n");
		/********* Reconfigure the IO Matrix for enabling the SPI interface *************/
		outBuf[0] = CFG_MODE_FEATURE_ID;
		outBuf[1] = MODE_FEATURE_FLASH_READ_WRITE;
		status = HidD_SetFeature(HidDevice->HidDevice, (PVOID)outBuf, (ULONG)2);
		if (status == TRUE)
		{

			outBuf[0] = RESET_ID;
			outBuf[1] = ERASE_AND_FALLBACK;
			status = HidD_SetOutputReport(HidDevice->HidDevice, (PVOID)outBuf, (ULONG)2);
			if (status == TRUE)
			{
				printf("Erase and fallback to bootloader is successful\n");
			}
		}
		else
		{
			printf("Fallback command Failed\n");

		}
	}




	retStatus = STATUS_SUCCESS;
	if (cmdLineArgs.programDevice)
	{
		printf("\r\n\r\nStep 1: Enable SPI\r\n");
		/********* Reconfigure the IO Matrix for enabling the SPI interface *************/
		outBuf[0] = CFG_MODE_FEATURE_ID;
		outBuf[1] = MODE_FEATURE_FLASH_READ_WRITE;
		status = HidD_SetFeature(HidDevice->HidDevice, (PVOID)outBuf, (ULONG)2);
		if (status == TRUE)
		{
			firmwarefile = fopen(cmdLineArgs.fwImagePath, "rb");
			if (firmwarefile == NULL)
			{
				printf("Cannot open file.\n");
				retStatus = STATUS_FW_DOWNLOAD_FAILED;
				return;
			}

			fseek(firmwarefile, 0, SEEK_END);
			firmwaresize = ftell(firmwarefile);
			fseek(firmwarefile, 0, SEEK_SET);
			firmwaresize = (firmwaresize + 255) / 256;
			firmwaresize = firmwaresize * 256;
			if (firmwaresize <= 0)
			{
				printf("Empty file\n");
				fclose(firmwarefile);
				retStatus = STATUS_ISP_FW_DOWNLOAD_FAILED;
				return;
			}
			else
			{
				firmwareFileSize = firmwaresize;
				printf("Firmware file size = %d bytes\n", firmwareFileSize);
			}

			/**** Sector Erase command for SPI flash before Writing *****/
			printf("Step 2: Erasing SPI Flash\r\n");


			for (int i = 0; i < (firmwaresize / SPI_FLASH_SECT_SIZE) + 1; i++)
			{
				//printf(".");
				printProgress((double)i / (firmwaresize / SPI_FLASH_SECT_SIZE));
				outBuf[0] = FLASH_ERASE_ID;
				outBuf[1] = i;
				BYTE Retries = 0;
				status = HidD_SetOutputReport(HidDevice->HidDevice, (PVOID)outBuf, (ULONG)2);
				if (status == TRUE)
				{
					Sleep(100);
					outBuf[0] = FLASH_ERASE_POLL_ID;
					while (HidD_GetInputReport(HidDevice->HidDevice, (PVOID)outBuf, (ULONG)2) == TRUE && outBuf[0] != 0)
					{
						outBuf[0] = FLASH_ERASE_POLL_ID;
						Sleep(100);
						++Retries;
						if (Retries > 50)
						{
							LastError = GetLastError();
							printf("Flash Erase Get Report Failed.. Sector number %d \r\n", i);
							printf("Last Error = %lu (%d) \r\n", LastError, LastError);
							bEraseFailed = TRUE;
							break;
						}
					}
				}
				else
				{
					LastError = GetLastError();
					printf("Erasing SPI Flash Sector number %d.Set Report failed\r\n", i);
					printf("Last Error = %lu (%d) \r\n", LastError, LastError);

				}

			}
			printf("\n");
			if (bEraseFailed)
			{
				printf("Erase Sectors failed\n");
				return;
			}
			printf("\r\n\r\nStep 3: Downloading Firmware\n");

			/*********** Firmware Download through Flash Write command ***********/
			UINT16 i = 0;
			BYTE retry;
			int flag = 0;
			double progress = 0;
			while (firmwaresize > 0 && status == TRUE)
			{
				Sleep(10);
				retry = 0;
				progress = (double)(firmwareFileSize - firmwaresize) / firmwareFileSize;
				
				printProgress(progress);

				EP_Out_Packet_Size = FLASH_PAGE_SIZE;
				outBuf[0] = FLASH_WRITE_ID;
				outBuf[1] = (i >> 8);
				outBuf[2] = (i & 0xFF);
				if (firmwaresize > EP_Out_Packet_Size)
				{

					fread(&outBuf[5], 1, EP_Out_Packet_Size, (firmwarefile));
					EP_Out_Packet_Size = FLASH_PAGE_SIZE;
					EP_In_Packet_Size = FLASH_PAGE_SIZE;
					outBuf[3] = (FLASH_PAGE_SIZE >> 8);
					outBuf[4] = (FLASH_PAGE_SIZE & 0xFF);
					status = HidD_SetOutputReport(HidDevice->HidDevice, (PVOID)outBuf, EP_Out_Packet_Size + 5);
					if (status == FALSE)
					{
						LastError = GetLastError();
						printf("first Set Report Failed \r\n");
						printf("Last Error = %lu (%d) \r\n", LastError, LastError);
						exit(0);
					}
					while (retry < 3)
					{

						tempBuf[0] = FLASH_READ_SET_ID;
						tempBuf[1] = (i >> 8);
						tempBuf[2] = (i & 0xFF);
						tempBuf[3] = (FLASH_PAGE_SIZE >> 8);
						tempBuf[4] = (FLASH_PAGE_SIZE & 0xFF);
						Sleep(10);
						status = HidD_SetOutputReport(HidDevice->HidDevice, (PVOID)tempBuf, (ULONG)5);
						if (status == TRUE)
						{
							tempBuf[0] = FLASH_READ_GET_ID;
							status = HidD_GetInputReport(HidDevice->HidDevice, (PVOID)tempBuf, EP_In_Packet_Size + 3);
							if (status == FALSE)
							{

								printf("Data check Get Report failed...sector %d\r\n", i);
								LastError = GetLastError();
								printf("Last Error = %lu (%d) \r\n", LastError, LastError);
								exit(0);
							}
							else
							{
								for (int j = 0; j < EP_Out_Packet_Size; j++)
								{
									if (tempBuf[j] != outBuf[j + 5])
									{
										retry++;
										flag = 0;
										printf("\nError.. mismatch of data\n");
										if (retry == 3)
										{
											printf("Data Mismatch after 3 retries\r\n");
										}
										break;

									}
									else
									{
										flag = 1;
									}

								}
								if (flag)
									break;
							}

						}
						else
						{
							retry++;
							if (retry == 3)
							{
								printf("Data check Set Report failed after three retries.. sector %d\n", i);
								LastError = GetLastError();
								printf("Last Error = %lu (%d) \r\n", LastError, LastError);
								exit(0);
							}

						}
					}

					firmwaresize = firmwaresize - (EP_Out_Packet_Size);
				}
				else
				{
					//printf("\nLast section of firmware download\n");
					fread(&outBuf[5], 1, firmwaresize, (firmwarefile));
					EP_Out_Packet_Size = firmwaresize;
					EP_In_Packet_Size = firmwaresize;
					outBuf[3] = (EP_Out_Packet_Size >> 8);
					outBuf[4] = (EP_Out_Packet_Size & 0xFF);
					firmwaresize = 0;
					status = HidD_SetOutputReport(HidDevice->HidDevice, (PVOID)outBuf, EP_Out_Packet_Size + 5);
					if (status == FALSE)
					{
						printf("Last Section Set Report 1 Failed\n");
						LastError = GetLastError();
						printf("Last Error = %lu (%d) \r\n", LastError, LastError);
						exit(0);
					}
					while (retry < 3)
					{

						tempBuf[0] = FLASH_READ_SET_ID;
						tempBuf[1] = (i >> 8);
						tempBuf[2] = (i & 0xFF);
						tempBuf[3] = (EP_Out_Packet_Size >> 8);
						tempBuf[4] = (EP_Out_Packet_Size & 0xFF);
						status = HidD_SetOutputReport(HidDevice->HidDevice, (PVOID)tempBuf, (ULONG)5);
						if (status == TRUE)
						{
							tempBuf[0] = FLASH_READ_GET_ID;
							status = HidD_GetInputReport(HidDevice->HidDevice, (PVOID)tempBuf, EP_In_Packet_Size + 3);
							if (status == FALSE)
							{
								printf("Last Section Get Report Failed\n");
								LastError = GetLastError();
								printf("Last Error = %lu (%d) \r\n", LastError, LastError);
								exit(0);
							}
							for (int j = 0; j < EP_Out_Packet_Size; j++)
							{
								if (tempBuf[j] != outBuf[j + 5])
								{
									retry++;
									if (retry == 3)
									{
										printf("Last Section data check failed after 3 retries\n");
										LastError = GetLastError();
										printf("Last Error = %lu (%d) \r\n", LastError, LastError);
									}
									flag = 0;
									break;
								}
								else
								{
									flag = 1;
								}

							}

						}
						else
						{
							printf("Last Section Set Report 2 Failed\n");
							LastError = GetLastError();
							printf("Last Error = %lu (%d) \r\n", LastError, LastError);
							exit(0);
						}
						if (flag)
						{
							break;
						}
					}
				}
				i++;
				if (flag == 0)
				{
					break;
				}
			}
			if (flag)
			{
				printProgress(1);
				printf("\nDownload Complete\n");
			}
			else
			{
				printf("\nDownload Failed\n");
			}
			fclose(firmwarefile);

			printf("Step 4: Switch to 32-bit GPIF\r\n");

			/********* Reconfigure the firmware for 32-bit GPIF by disabling the SPI interface *********/
			outBuf[0] = CFG_MODE_FEATURE_ID;
			outBuf[1] = MODE_FEATURE_NORMAL;
			status = HidD_SetFeature(HidDevice->HidDevice, (PVOID)outBuf, (ULONG)2);
			if (status == TRUE)
			{
				printf("Step 5: Do Device Reset (Cold Reset)\r\n");

				/**** Issue a Hard Reset for the new firmware to take effect *****/
				outBuf[0] = RESET_ID;
				outBuf[1] = RESET_COLD;
				status = HidD_SetOutputReport(HidDevice->HidDevice, (PVOID)outBuf, (ULONG)2);
				if (status == TRUE)
				{
					printf("Reset Successful\n");
				}
				else
				{
					printf("Reset Failed\n");
				}
			}


		}
		else
		{

			printf("Reconfig failed\r\n");
			LastError = GetLastError();
			printf("GetLastError = %lu (%d) \r\n", LastError, LastError);

		}
	}

	if (retStatus != STATUS_SUCCESS)
	{
		printf("Return Status = %d", retStatus);
	}
	return retStatus;
}

