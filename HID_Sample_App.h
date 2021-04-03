
#define CFG_MODE_FEATURE_ID		1	
#define FLASH_READ_SET_ID		2	
#define FLASH_READ_GET_ID		3
#define FLASH_WRITE_ID			4
#define FLASH_ERASE_ID			5
#define FLASH_ERASE_POLL_ID		6	

#define RESET_ID				18
#define FLASH_PAGE_SIZE			256
#define SPI_FLASH_SECT_SIZE		(64*1024)

#define MODE_FEATURE_NORMAL		0
#define MODE_FEATURE_FLASH_READ_WRITE	1

#define RESET_COLD				0
#define RESET_WARM				1
#define ERASE_AND_FALLBACK      1

HANDLE		HidDevHandle;						/* Handle to HID device */
PHID_DEVICE HidDevice;