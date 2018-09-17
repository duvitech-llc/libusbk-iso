#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "libusbk.h"

#define USBD_VID     0x0483
#define USBD_PID_HS     0x2E18

#define EP_TRANSFER				0x02
#define EP_PACKET_SIZE			1024
#define ISO_PACKETS_PER_XFER	32


#define ISO_CALC_CONTEXT_SIZE(mNumOfIsoPackets) (sizeof(KISO_CONTEXT)+(sizeof(KISO_PACKET)*(mNumOfIsoPackets)))
#define ISO_CALC_DATABUFFER_SIZE(mNumOfIsoPackets, mIsoPacketSize) (mNumOfIsoPackets*mIsoPacketSize)
BOOL Examples_GetTestDeviceEx(KLST_HANDLE* DeviceList,
	KLST_DEVINFO_HANDLE* DeviceInfo,
	int argc,
	char* argv[],
	KLST_FLAG Flags)
{
	ULONG vidArg = USBD_VID;
	ULONG pidArg = USBD_PID_HS;
	ULONG deviceCount = 0;
	int argPos;
	KLST_HANDLE deviceList = NULL;
	KLST_DEVINFO_HANDLE deviceInfo = NULL;

	// init
	*DeviceList = NULL;
	*DeviceInfo = NULL;

	// Get the test device vid/pid from the command line (if specified)
	for (argPos = 1; argPos < argc; argPos++)
	{
		sscanf_s(argv[argPos], "vid=%04x", &vidArg);
		sscanf_s(argv[argPos], "pid=%04x", &pidArg);
	}

	// Get the device list
	if (!LstK_Init(&deviceList, Flags))
	{
		printf("Error initializing device list.\n");
		return FALSE;
	}

	LstK_Count(deviceList, &deviceCount);
	if (!deviceCount)
	{
		printf("Device list empty.\n");
		SetLastError(ERROR_DEVICE_NOT_CONNECTED);

		// If LstK_Init returns TRUE, the list must be freed.
		LstK_Free(deviceList);

		return FALSE;
	}

	printf("Looking for device vid/pid %04X/%04X..\n", vidArg, pidArg);

	LstK_FindByVidPid(deviceList, vidArg, pidArg, &deviceInfo);

	if (deviceInfo)
	{
		// This function returns the device list and the device info
		// element which matched.  The caller is responsible for freeing
		// this list when it is no longer needed.
		*DeviceList = deviceList;
		*DeviceInfo = deviceInfo;

		// Report the connection state of the example device
		printf("Using %04X:%04X (%s): %s - %s\n",
			deviceInfo->Common.Vid,
			deviceInfo->Common.Pid,
			deviceInfo->Common.InstanceID,
			deviceInfo->DeviceDesc,
			deviceInfo->Mfg);

		return TRUE;
	}
	else
	{
		// Display some simple usage information for the example applications.
		CHAR programPath[MAX_PATH] = { 0 };
		PCHAR programExe = programPath;
		GetModuleFileNameA(GetModuleHandleA(NULL), programPath, sizeof(programPath));
		while (strpbrk(programExe, "\\/")) programExe = strpbrk(programExe, "\\/") + 1;
		printf("Device vid/pid %04X/%04X not found.\n\n", vidArg, pidArg);
		printf("USAGE: %s vid=%04X pid=%04X\n\n", programExe, vidArg, pidArg);

		// If LstK_Init returns TRUE, the list must be freed.
		LstK_Free(deviceList);

		return FALSE;
	}
}

BOOL Examples_GetTestDevice(KLST_HANDLE* DeviceList,
	KLST_DEVINFO_HANDLE* DeviceInfo,
	int argc,
	char* argv[])
{
	return Examples_GetTestDeviceEx(DeviceList,
		DeviceInfo,
		argc,
		argv,
		0);

}

int main(int argc, char* argv[]) {

	printf("LIBUSBK Demo\n");
	KLST_HANDLE deviceList = NULL;
	KLST_DEVINFO_HANDLE deviceInfo = NULL;
	KUSB_HANDLE handle = NULL;
	DWORD errorCode = ERROR_SUCCESS;
	UCHAR pipeID = EP_TRANSFER;

	BOOL success;
	UCHAR dataBuffer[ISO_CALC_DATABUFFER_SIZE(ISO_PACKETS_PER_XFER, EP_PACKET_SIZE)];
	UCHAR buf[EP_PACKET_SIZE] = { 0 };
	ULONG transferred = 0;
	PKISO_CONTEXT isoCtx = NULL;
	KOVL_HANDLE ovlkHandle = NULL;
	LONG posPacket;
	ULONG currentFrameNumber;
	KOVL_POOL_HANDLE ovlPool = NULL;

	/*
	Find the test device. Uses "vid=hhhh pid=hhhh" arguments supplied on the
	command line. (default is: vid=04D8 pid=FA2E)
	*/
	if (!Examples_GetTestDevice(&deviceList, &deviceInfo, argc, argv))
		return GetLastError();

	/*
	Initialize the device. This creates the physical usb handle.
	*/
	if (!UsbK_Init(&handle, deviceInfo))
	{
		errorCode = GetLastError();
		printf("UsbK_Init failed. ErrorCode: %08Xh\n", errorCode);
		goto Done;
	}
	printf("Device opened successfully!\n");


	/*
	Initialize a new iso context handle.
	*/
	IsoK_Init(&isoCtx, ISO_PACKETS_PER_XFER, 0);

	/*
	Set the iso packet offsets.
	*/
	IsoK_SetPackets(isoCtx, EP_PACKET_SIZE);

	/*
	Initialize a new OvlK pool handle.
	*/
	OvlK_Init(&ovlPool, handle, 4, 0);
	OvlK_Acquire(&ovlkHandle, ovlPool);
	UsbK_ResetPipe(handle, pipeID);


	// UsbK_IsoReadPipe(handle, pipeID, dataBuffer, sizeof(dataBuffer), ovlkHandle, isoCtx);
	UsbK_IsoWritePipe(handle, pipeID, dataBuffer, sizeof(dataBuffer), ovlkHandle, isoCtx);
	success = OvlK_WaitAndRelease(ovlkHandle, 1000, &transferred);

	if (!success)
	{
		errorCode = GetLastError();
		printf("IsoPipe Transaction failed. ErrorCode: %08Xh\n", errorCode);
		goto Done;
	}
	
	printf("ISO StartFrame=%08Xh ErrorCount=%u Transferred=%u\n",
		isoCtx->StartFrame, isoCtx->ErrorCount, transferred);
/*
	for (posPacket = 0; posPacket < isoCtx->NumberOfPackets; posPacket++)
	{
		LONG posByte;
		PKISO_PACKET isoPacket = &isoCtx->IsoPackets[posPacket];

		printf("  IsoPacket[%d] Length=%u Status=%08Xh\n", posPacket, isoPacket->Length, isoPacket->Status);
		printf("  Data:");

		for (posByte = 0; posByte < isoPacket->Length; posByte++)
		{
			printf("%02Xh ", dataBuffer[isoPacket->Offset + posByte]);
			if ((posByte & 0xF) == 0xF) printf("\n       ");
		}
		printf("\n");
		if ((posByte & 0xF) != 0xF) printf("\n");
	}
*/

Done:

	// Close the device handle.
	UsbK_Free(handle);

	// Free the device list.
	LstK_Free(&deviceList);
	
	// Free the iso context.
	IsoK_Free(isoCtx);

	// Free the overlapped pool.
	OvlK_Free(ovlPool);

	// return the win32 error code.
	return errorCode;
}