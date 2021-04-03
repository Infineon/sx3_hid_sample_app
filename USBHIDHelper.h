/************************************************************************
usbHidCommunication.h

A class for communication with Generic HID USB devices.

************************************************************************/


#pragma once

#include "stdafx.h"

#define DEVICE_OFFSET		6
#define MAX_DEVICES			2


namespace usbHidCommunicationNameSpace
{
	using namespace System;

	using namespace System::Windows::Forms;

	// The following namespace is required for threading (which is used to
	// perform device read and write operations in the background
	using namespace System::Threading;

	// The following namespace is required to allow us to run unmanaged code
	// i.e. support the inclusion of DLLs which are required to communicate
	// with USB HID devices and windows in general
	using namespace System::Runtime::InteropServices;

	// Now we have to define the DLL library headers for setupapi.h to allow
	// us to call them from C++ (since they are C headers).

	[DllImport("hid.dll", CharSet = CharSet::Auto, EntryPoint = "HidD_GetHidGuid")]
	extern "C" void __stdcall HidD_GetHidGuid(
		__out  LPGUID HidGuid);

	[DllImport("hid.dll", CharSet = CharSet::Auto, EntryPoint = "HidD_GetFeature")]
	extern "C" BOOLEAN __stdcall HidD_GetFeature(
		__in   HANDLE HidDeviceObject,
		__out  PVOID ReportBuffer,
		__in   ULONG ReportBufferLength);

	// Now we define a class to encapsulate the USB functions
	public ref class usbHidCommunication
	{
	public:
		// Constructor method
		usbHidCommunication(System::Void)
		{
			// Set deviceAttached to false
			deviceAttached = false;

			// Set the read and write handles to invalid
			BatteryStatusHandle = INVALID_HANDLE_VALUE;
			PacketCountHandle = INVALID_HANDLE_VALUE;

		} // END usbHidCommunication method

		// Destructor method
		~usbHidCommunication(System::Void)
		{
			// Cleanly detach ourselves from the USB device
			detachUsbDevice();
		} // END ~usbHidCommunication method

	public:
		// This public method requests that device notification messages are sent to the calling form
		// which the form must catch with a WndProc override.
		//
		// You will need to supply the handle of the calling form for this to work, usually
		// specified with 'this->form'.  Something like the following line needs to be placed
		// in the form's contructor method:
		// 
		// a_usbHidCommunication.requestDeviceNotificationsToForm(this->Handle);
		// 
		System::Void requestDeviceNotificationsToForm(System::IntPtr handleOfWindow)
		{
			// Define the Globally Unique Identifier (GUID) for HID class devices:
			GUID InterfaceClassGuid;// = {0x4d1e55b2, 0xf16f, 0x11cf, 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30};

			HidD_GetHidGuid(&InterfaceClassGuid);

			// Register for WM_DEVICECHANGE notifications.  This code uses these messages to detect
			// plug and play connection/disconnection events for USB devices
			DEV_BROADCAST_DEVICEINTERFACE MyDeviceBroadcastHeader;
			MyDeviceBroadcastHeader.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
			MyDeviceBroadcastHeader.dbcc_size = sizeof(MyDeviceBroadcastHeader);
			MyDeviceBroadcastHeader.dbcc_reserved = 0;
			MyDeviceBroadcastHeader.dbcc_classguid = InterfaceClassGuid;
			RegisterDeviceNotification((HANDLE)handleOfWindow, &MyDeviceBroadcastHeader, DEVICE_NOTIFY_WINDOW_HANDLE);
		}

	public:
		// This public method filters WndProc notification messages for the required
		// device notifications and triggers a re-detection of the USB device if required.
		//
		// The main form of the application needs to include an override of the WndProc
		// class for this to be called, usually this is defined as a protected method
		// of the main form and looks like the following:
		//
		System::Void handleDeviceChangeMessages(Message% m, System::Int16 vid, System::Int16 pid)
		{
			if (m.Msg == WM_DEVICECHANGE)
			{
				//					 if(((int)m.WParam == DBT_DEVICEARRIVAL) || ((int)m.WParam == DBT_DEVICEREMOVEPENDING) ||
				//						 ((int)m.WParam == DBT_DEVICEREMOVECOMPLETE) || ((int)m.WParam == DBT_CONFIGCHANGED) )
				if ((int)m.WParam == DBT_DEVNODES_CHANGED)
				{
					// Check the device is still available
					findDevice(vid, pid); // VID, PID			 
				}
			}
		}

		// Define public method for reading the deviceAttached flag
	public:
		System::Boolean isDeviceAttached(System::Void)
		{
			return deviceAttached;
		} // END isDeviceAttached method

	private:
		// Private variables for holding the device found state and the
		// read/write handles
		System::Boolean deviceAttached;

		HANDLE BatteryStatusHandle;
		HANDLE PacketCountHandle;

	public:
		// This method attempts to find the target USB device and discover the device's path
		//
		// Note: A limitation of this routine is that it cannot deal with two or more devices
		//       connected to the host that have the same VID and PID, it will simply pick
		//       the first one it finds...
		System::Void findDevice(System::Int16 usbVid, System::Int16 usbPid)
		{

			// Define the Globally Unique Identifier (GUID) for HID class devices:
			GUID InterfaceClassGuid;// = {0x4d1e55b2, 0xf16f, 0x11cf, 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}; 

			HidD_GetHidGuid(&InterfaceClassGuid);

			// Initialise required datatypes and variables
			HDEVINFO DeviceInfoTable = INVALID_HANDLE_VALUE;
			PSP_DEVICE_INTERFACE_DATA InterfaceDataStructure = new SP_DEVICE_INTERFACE_DATA;
			PSP_DEVICE_INTERFACE_DETAIL_DATA DetailedInterfaceDataStructure/* = new SP_DEVICE_INTERFACE_DETAIL_DATA*/;

			SP_DEVINFO_DATA DevInfoData;


			DWORD InterfaceIndex = 0;
			DWORD StatusLastError = 0;
			DWORD dwRegType;
			DWORD dwRegSize;
			DWORD StructureSize = 0;
			PBYTE PropertyValueBuffer;
			bool MatchFound = false;
			DWORD ErrorStatus;
			DWORD ErrorStatusBattReport;
			DWORD ErrorStatusPKTReport;
			DWORD loopCounter = 0;
			LPCWSTR DevicePath;
			int DeviceList = 0;
			bool FirstTime = true;

			// Construct the VID and PID in the correct string format for Windows
			// "Vid_hhhh&Pid_hhhh" - where hhhh is a four digit hexadecimal number
			String^ usbVidHex = usbVid.ToString("X4");
			String^ usbPidHex = usbPid.ToString("X4");

			usbVidHex = String::Concat("Vid_", usbVidHex);
			usbPidHex = String::Concat("Pid_", usbPidHex);

			String^ DeviceIDToFind = String::Concat(usbVidHex, "&", usbPidHex);

			// If the device is currently flagged as attached then we are 'rechecking' the device, probably
			// due to some message receieved from Windows indicating a device status chanage.  In this case
			// we should detach the USB device cleanly (if required) before reattaching it.
			detachUsbDevice();

			// Here we populate a list of plugged-in devices matching our class GUID (DIGCF_PRESENT specifies that the list
			// should only contain devices which are plugged in)
			DeviceInfoTable = SetupDiGetClassDevs(&InterfaceClassGuid,
				NULL,
				NULL,
				(DIGCF_PRESENT | // only devices present
				DIGCF_INTERFACEDEVICE)); // function class devices.

			// Look through the retrieved list of class GUIDs looking for a match on our interface GUID
			while (true)
			{
				InterfaceDataStructure->cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
				if (SetupDiEnumDeviceInterfaces(DeviceInfoTable,
					NULL,
					&InterfaceClassGuid,
					InterfaceIndex,
					InterfaceDataStructure))
				{
					// Check for an error
					ErrorStatus = GetLastError();

					// Are we at the end of the list?
					if (ERROR_NO_MORE_ITEMS == ErrorStatus)
					{
						// Device is not attached, clean up memory and return with error status
						SetupDiDestroyDeviceInfoList(DeviceInfoTable);
						deviceAttached = false;
						return;
					}
				}
				else
				{
					// An unknown error occurred! Clean up memory and return with error status
					ErrorStatus = GetLastError();
					SetupDiDestroyDeviceInfoList(DeviceInfoTable);
					deviceAttached = false;
					return;
				}

				// Now we have devices with a matching class GUID and interface GUID we need to get the hardware IDs for 
				// the devices.  From that we can get the VID and PID in order to find our target device.

				// Initialize an appropriate SP_DEVINFO_DATA structure.  We need this structure for calling
				// SetupDiGetDeviceRegistryProperty()
				DevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
				SetupDiEnumDeviceInfo(DeviceInfoTable, InterfaceIndex, &DevInfoData);

				// Firstly we have to determine the size of the query so we can allocate memory to store the returned
				// hardware ID structure
				SetupDiGetDeviceRegistryProperty(DeviceInfoTable, &DevInfoData,
					SPDRP_HARDWAREID, &dwRegType, NULL, 0, &dwRegSize);

				// Now allocate the memory for the hardware ID structure
				PropertyValueBuffer = (BYTE *)malloc(dwRegSize);

				// Test to ensure the memory was allocated:
				if (PropertyValueBuffer == NULL)
				{
					// Not enough memory... clean up and return error status
					SetupDiDestroyDeviceInfoList(DeviceInfoTable);
					deviceAttached = false;
					return;
				}

				// Get the hardware ID for the current device.  The PropertyValueBuffer gets filled with an array
				// of NULL terminated strings (REG_MULTI_SZ).  The first string in the buffer contains the 
				// hardware ID in the format "Vid_xxxx&Pid_xxxx" so we compare that against our target device
				// identifier to see if we have a match.
				SetupDiGetDeviceRegistryProperty(DeviceInfoTable,
					&DevInfoData, SPDRP_HARDWAREID, &dwRegType,
					PropertyValueBuffer, dwRegSize, NULL);

				// Get the string
				String^ DeviceIDFromRegistry = gcnew String((wchar_t *)PropertyValueBuffer);

				// We don't need the PropertyValueBuffer any more so we free the memory
				free(PropertyValueBuffer);

				// Convert the strings to lowercase
				DeviceIDFromRegistry = DeviceIDFromRegistry->ToLowerInvariant();
				DeviceIDToFind = DeviceIDToFind->ToLowerInvariant();

				// Check if the deviceID has the correct VID and PID
				MatchFound = DeviceIDFromRegistry->Contains(DeviceIDToFind);

				if (MatchFound == true)
				{
					if (FirstTime)
						InterfaceIndex += DEVICE_OFFSET;
					FirstTime = false;
					// The target device has been found, now we need to retrieve the device path so we can open
					// the read and write handles required for USB communication

					// First call to determine the size of the returned structure
					SetupDiGetDeviceInterfaceDetail(DeviceInfoTable,
						InterfaceDataStructure,
						NULL,
						NULL,
						&StructureSize,
						NULL);

					DWORD required_length = StructureSize;

					// Allocate the memory required
					DetailedInterfaceDataStructure = (PSP_DEVICE_INTERFACE_DETAIL_DATA)(malloc(StructureSize));

					// Test to ensure the memory was allocated
					if (DetailedInterfaceDataStructure == NULL)
					{
						// Not enough memory... clean up and return error status
						SetupDiDestroyDeviceInfoList(DeviceInfoTable);
						deviceAttached = false;
						return;
					}

					// Now we call it again to get the structure
					DetailedInterfaceDataStructure->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
					DevicePath = DetailedInterfaceDataStructure->DevicePath;
					SetupDiGetDeviceInterfaceDetail(DeviceInfoTable,
						InterfaceDataStructure,
						DetailedInterfaceDataStructure,
						StructureSize,
						&required_length,
						NULL);

					if (DeviceList == 0)
					{
						// Open the battery status handle
						BatteryStatusHandle = CreateFile(DevicePath,
							GENERIC_WRITE | GENERIC_READ,
							FILE_SHARE_READ | FILE_SHARE_WRITE,
							NULL, OPEN_EXISTING, 0, NULL);
						ErrorStatusBattReport = GetLastError();

						if (ErrorStatusBattReport == ERROR_SUCCESS)
						{
							// File handles opened successfully, device is now attached
							DeviceList++;
						}
						else
						{
							// Something went wrong... If we managed to open either handle close them
							// and set deviceAttachedButBroken since we found the device but, for some
							// reason, can't use it.
							if (ErrorStatusBattReport == ERROR_SUCCESS)
								CloseHandle(BatteryStatusHandle);
							deviceAttached = false;
						}
					}
					else if (DeviceList == 1)
					{
						// Open the packet count handle
						PacketCountHandle = CreateFile(DevicePath,
							GENERIC_WRITE | GENERIC_READ,
							FILE_SHARE_READ | FILE_SHARE_WRITE,
							NULL, OPEN_EXISTING, 0, 0);
						ErrorStatusPKTReport = GetLastError();
						if (ErrorStatusBattReport == ERROR_SUCCESS)
						{
							// File handles opened successfully, device is now attached
							DeviceList++;
						}
						else
						{
							// Something went wrong... If we managed to open either handle close them
							// and set deviceAttachedButBroken since we found the device but, for some
							// reason, can't use it.
							if (ErrorStatusPKTReport == ERROR_SUCCESS)
								CloseHandle(PacketCountHandle);
							deviceAttached = false;
						}
					}

					// Clean up the memory
					//SetupDiDestroyDeviceInfoList(DeviceInfoTable);

					delete[] DetailedInterfaceDataStructure;
					//SetupDiDestroyDeviceInfoList(DeviceInfoTable);

					if (DeviceList >= /*1*/MAX_DEVICES)
					{
						// File handles opened successfully, device is now attached
						deviceAttached = true;
						return;
					}
				} // END if

				// Select the next interface and loop again until we find the device or run out of items
				InterfaceIndex++;

				// Just to be safe, we check how many times we've looped to avoid a infinite loop situation
				// cause by some unforeseen error condition
				loopCounter++;

				if (loopCounter > 1000000)
				{
					// We've looped over a million times... let's just give up
					deviceAttached = false;
					return;
				}
			} // END while(true)
		} // END findDevice method

	public:

		BOOLEAN GetBatteryStatus(unsigned char *output, int length)
		{
			BOOLEAN return_value;
			return_value = HidD_GetFeature(BatteryStatusHandle, (PVOID)output, (ULONG)length);
			if (return_value == false)
				detachUsbDevice();

			return return_value;
		}

		BOOLEAN GetPacketCounts(unsigned char *output, int length)
		{
			BOOLEAN return_value;
			return_value = HidD_GetFeature(PacketCountHandle, (PVOID)output, (ULONG)length);
			if (return_value == false)
				detachUsbDevice();

			return return_value;
		}

	public:
		// This public method detaches the USB device and forces the 
		// worker threads to cancel IO and abort if required.
		// This is used when we're done communicating with the device
		System::Void detachUsbDevice(System::Void)
		{
			if (deviceAttached == true)
			{
				// Unattach the device
				deviceAttached = false;

				// Close the device file handles
				CloseHandle(BatteryStatusHandle);
				CloseHandle(PacketCountHandle);
			}
		} // END detachUsbDevice Method

	}; // END class
} // END namespace usbHidCommunicationNameSpace