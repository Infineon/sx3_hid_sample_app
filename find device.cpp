
VOID findDevice(UINT usbVid, UINT usbPid)
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
	BOOL MatchFound = FALSE;
	DWORD ErrorStatus;
	DWORD ErrorStatusBattReport;
	DWORD ErrorStatusPKTReport;
	DWORD loopCounter = 0;
	LPCWSTR DevicePath;
	int DeviceList = 0;
	BOOL FirstTime = TRUE;

	// Construct the VID and PID in the correct string format for Windows
	// "Vid_hhhh&Pid_hhhh" - where hhhh is a four digit hexadecimal number
#if 0
	String^ usbVidHex = usbVid.ToString("X4");
	String^ usbPidHex = usbPid.ToString("X4");

	usbVidHex = String::Concat("Vid_", usbVidHex);
	usbPidHex = String::Concat("Pid_", usbPidHex);

	String^ DeviceIDToFind = String::Concat(usbVidHex, "&", usbPidHex);
#endif

	CHAR DeviceIDToFind[] = { "V", "i", "d", "_", "0", "4", "C", "4", "&", "P", "i", "d", "_", "1", "4", "A", "4" };
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
	while (TRUE)
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
				deviceAttached = FALSE;
				return;
			}
		}
		else
		{
			// An unknown error occurred! Clean up memory and return with error status
			ErrorStatus = GetLastError();
			SetupDiDestroyDeviceInfoList(DeviceInfoTable);
			deviceAttached = FALSE;
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
			deviceAttached = FALSE;
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

		if (MatchFound == TRUE)
		{
			if (FirstTime)
				InterfaceIndex += DEVICE_OFFSET;
			FirstTime = FALSE;
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
				deviceAttached = FALSE;
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
					deviceAttached = FALSE;
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
					deviceAttached = FALSE;
				}
			}

			// Clean up the memory
			//SetupDiDestroyDeviceInfoList(DeviceInfoTable);

			delete[] DetailedInterfaceDataStructure;
			//SetupDiDestroyDeviceInfoList(DeviceInfoTable);

			if (DeviceList >= /*1*/MAX_DEVICES)
			{
				// File handles opened successfully, device is now attached
				deviceAttached = TRUE;
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
			deviceAttached = FALSE;
			return;
		}
	} // END while(true)
} // END findDevice method
