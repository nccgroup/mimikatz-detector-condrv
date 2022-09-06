/*
Console Monitor
Balazs Bucsay 2022 <balazs.bucsay@nccgroup.com>


This tool is slightly based on WinConMon (https://github.com/EyeOfRa/WinConMon) written by EyeOfRa
and also on Mandiant's research see below:

https://www.mandiant.com/resources/monitoring-windows-console-activity-part-one?
https://www.mandiant.com/resources/monitoring-windows-console-activity-part-2

*/

#include <ntddk.h>
#include <ntstatus.h>
#include "ETWEvents.h"

UNICODE_STRING CONDRV_DRIVER_NAME = RTL_CONSTANT_STRING(L"\\Driver\\Condrv");

FAST_IO_DISPATCH* g_FastIoDispatch = NULL;
IO_STATUS_BLOCK iostat;

typedef NTSTATUS(*Fn_ObReferenceObjectByName)(
	PUNICODE_STRING,
	ULONG,
	PACCESS_STATE,
	ACCESS_MASK,
	POBJECT_TYPE,
	KPROCESSOR_MODE,
	PVOID,
	PVOID*
	);

Fn_ObReferenceObjectByName fnObReferenceObjectByName = NULL;

typedef struct _CONSOLE_IO_MSG_T
{
	ULONG handle;
	PVOID buffer;
	ULONG buffLength;
	ULONG type;
} CONSOLE_IO_MSG_T, * LPCONSOLE_IO_MSG_T;

typedef struct _CONMONDRV_DEVICE_EXTENSION
{
	PDEVICE_OBJECT AttachedToDeviceObject;
} CONMONDRV_DEVICE_EXTENSION, * LPCONMONDRV_DEVICE_EXTENSION;

WCHAR szTitleMatch[] = { L"mimikatz " }; // title to match
ULONG ulTitleMatch;

WCHAR *szTextMatch[] = { L"mimikatz #", L"  .#####.   mimikatz"}; // strings in output to match
ULONG ulnTextMatch = 2; // number of szTextMatch strings
ULONG ulTextMatchLength[2]; // length of szTextMatch strings
ULONG ulTextMatchCounter[2]; // counter to match byte-by-byte prints

extern "C"
VOID DriverUnload(__in PDRIVER_OBJECT driverObject)
{
	EventWriteUnloadEvent(NULL, driverObject->DeviceObject);
	DbgPrint("Unloading driver\n");
	
	LPCONMONDRV_DEVICE_EXTENSION devExt = (LPCONMONDRV_DEVICE_EXTENSION)driverObject->DeviceObject->DeviceExtension;
	if (devExt->AttachedToDeviceObject)
	{
		IoDetachDevice(devExt->AttachedToDeviceObject);
		devExt->AttachedToDeviceObject = NULL;
	}

	if (g_FastIoDispatch) ExFreePoolWithTag(g_FastIoDispatch, 'GCCN');

	if (driverObject->DeviceObject)
		IoDeleteDevice(driverObject->DeviceObject);

	EventUnregisterMimikatz_Console_Detection();
}

NTSTATUS ConMonDrvDefaultDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	// Passthrough to lower device
	LPCONMONDRV_DEVICE_EXTENSION devExt = (LPCONMONDRV_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	IoSkipCurrentIrpStackLocation(Irp);
	return IoCallDriver(devExt->AttachedToDeviceObject, Irp);
}

BOOLEAN
ConMonDrvFastIoDeviceControl(
	_In_ struct _FILE_OBJECT* FileObject,
	_In_ BOOLEAN Wait,
	_In_opt_ PVOID InputBuffer,
	_In_ ULONG InputBufferLength,
	_Out_opt_ PVOID OutputBuffer,
	_In_ ULONG OutputBufferLength,
	_In_ ULONG IoControlCode,
	_Out_ PIO_STATUS_BLOCK IoStatus,
	_In_ struct _DEVICE_OBJECT* DeviceObject
)
{
	LPCONMONDRV_DEVICE_EXTENSION devExt = (LPCONMONDRV_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	PFAST_IO_DISPATCH fastIoDispatch = devExt->AttachedToDeviceObject->DriverObject->FastIoDispatch;
	BOOLEAN bResult = TRUE;

	if (IoControlCode == 0x500038)
	{
		DbgPrint("0x500038 , skipping call\n");
	}
	else
		bResult = fastIoDispatch->FastIoDeviceControl(FileObject,
			Wait,
			InputBuffer,
			InputBufferLength,
			OutputBuffer,
			OutputBufferLength,
			IoControlCode,
			IoStatus,
			devExt->AttachedToDeviceObject);

	LPCONSOLE_IO_MSG_T pMsg = (LPCONSOLE_IO_MSG_T)InputBuffer;
	switch (IoControlCode)
	{
	// Read IO input - output for the commands
	case 0x50000F:
	{
		// normal message printed as response
		if (pMsg->type == 0x10) {
			if (pMsg->buffLength == 2)
			{
				WCHAR* p = (WCHAR *)pMsg->buffer;
				for (ULONG i = 0; i < ulnTextMatch; i++)
				{
					if (szTextMatch[i][ulTextMatchCounter[i]] == p[0])
					{
						ulTextMatchCounter[i]++;
						if (ulTextMatchCounter[i] == ulTextMatchLength[i])
						{
							EventWriteSampleEventB(NULL);
							DbgPrint("MATCH2222!!!!!! %d \n", i);
							ulTextMatchCounter[i] = 0;
						}
					}
					else
						ulTextMatchCounter[i] = 0;
				}
					
				/*UCHAR* p2;
				DbgPrint("OUTPUT Type: %x - %p - blength: %x\n", pMsg->type, pMsg, pMsg->buffLength);
				DbgPrint("OUTPUT message: %ws\n", pMsg->buffer);
				DbgPrint("OUTPUT HEX: ", pMsg->buffer);
				p2 = (unsigned char*)pMsg->buffer;
				for (ULONG i = 0; i < pMsg->buffLength; i++) DbgPrint("%02X ", *p++);
				DbgPrint("\n");*/
			}

			/*
			NTSTATUS status = ZwWriteFile(hPipe, NULL, NULL, NULL, &iostat, pMsg->buffer,
				pMsg->buffLength, NULL, NULL);
			if ((status != STATUS_SUCCESS) && (status != STATUS_PENDING)) DbgPrint("ZwWriteFile error: %08x\n", status);
			*/
		}
		// Title of the app
		else if (pMsg->type == 0x09)
		{
			if (ulTitleMatch < pMsg->buffLength)
			{
				if (!wcsncmp(szTitleMatch, (WCHAR*)pMsg->buffer, ulTitleMatch - 1))
				{
					EventWriteSampleEventA(NULL);
					DbgPrint("MATCH!\n");
				}
			}
		}
		else
		{
			/*
			// TODO investigate the remaining messages
			UCHAR* p;
			DbgPrint("OUTPUT Type: %x - %p - blength: %x\n", pMsg->type, pMsg, pMsg->buffLength);
			DbgPrint("OUTPUT message: %ws\n", pMsg->buffer);
			DbgPrint("OUTPUT HEX: ", pMsg->buffer);
			p = (unsigned char*)pMsg->buffer;
			for (ULONG i = 0; i < pMsg->buffLength; i++) DbgPrint("%02X ", *p++);
			DbgPrint("\n");
			*/
		}

		break;
	}
	default:
		break;
	}
	return bResult;
}

NTSTATUS ConMonDrvCreateCloseDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	LPCONMONDRV_DEVICE_EXTENSION devExt = (LPCONMONDRV_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	PIO_STACK_LOCATION irpStack = NULL;
	irpStack = IoGetCurrentIrpStackLocation(Irp);

	IoSkipCurrentIrpStackLocation(Irp);
	return IoCallDriver(devExt->AttachedToDeviceObject, Irp);
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(DriverObject);
	UNREFERENCED_PARAMETER(RegistryPath);

	NTSTATUS status = 0;
	UNICODE_STRING funcName;
	PDRIVER_OBJECT condrvObj = NULL;
	extern POBJECT_TYPE* IoDriverObjectType;

	EventRegisterMimikatz_Console_Detection();
	DbgPrint("Driver loading\n");

	ulTitleMatch = (ULONG)wcslen(szTitleMatch);
	ulTextMatchLength[0] = (ULONG)wcslen(szTextMatch[0]);
	ulTextMatchLength[1] = (ULONG)wcslen(szTextMatch[1]);
	ulTextMatchCounter[0] = 0;
	ulTextMatchCounter[1] = 0;

	// routine that will execute when our driver is unloaded/service is stopped
	DriverObject->DriverUnload = DriverUnload;

	for (int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
		DriverObject->MajorFunction[i] = ConMonDrvDefaultDispatch;
	}

	// routines that will execute once a handle to our device's symbolik link is opened/closed
	DriverObject->MajorFunction[IRP_MJ_CREATE] = ConMonDrvCreateCloseDispatch;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = ConMonDrvCreateCloseDispatch;

	g_FastIoDispatch = (FAST_IO_DISPATCH*)ExAllocatePoolWithTag(NonPagedPool, sizeof(FAST_IO_DISPATCH), 'GCCN');
	RtlZeroMemory(g_FastIoDispatch, sizeof(FAST_IO_DISPATCH));
	g_FastIoDispatch->SizeOfFastIoDispatch = sizeof(FAST_IO_DISPATCH);
	g_FastIoDispatch->FastIoDeviceControl = ConMonDrvFastIoDeviceControl;

	DriverObject->FastIoDispatch = g_FastIoDispatch;

	status = IoCreateDevice(DriverObject, sizeof(CONMONDRV_DEVICE_EXTENSION), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DriverObject->DeviceObject);
	if (!NT_SUCCESS(status))
	{
		DbgPrint("Could not create device\n");
	}
	else
	{
		DbgPrint("Device created\n");
	}

	// Turning on Direct I/O
	DriverObject->DeviceObject->Flags |= DO_DIRECT_IO;
	
	RtlInitUnicodeString(&funcName, L"ObReferenceObjectByName");
	fnObReferenceObjectByName = (Fn_ObReferenceObjectByName)MmGetSystemRoutineAddress(&funcName);
	if (!fnObReferenceObjectByName) goto EXIT_FAILED;


	WCHAR DeviceNameString[128];
	RtlCopyMemory(DeviceNameString,
		funcName.Buffer,
		funcName.Length);

	DeviceNameString[funcName.Length] = L'\0';

	status = EventWriteStartEvent(NULL, (unsigned short)wcslen(L"Mimikatz Console Detection started"), L"Mimikatz Console Detection started", status);

	// Get device object of Condrv.sys
	status = fnObReferenceObjectByName(&CONDRV_DRIVER_NAME, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, (PVOID*)&condrvObj);
	if (!NT_SUCCESS(status)) goto EXIT_FAILED;

	LPCONMONDRV_DEVICE_EXTENSION devExt = (LPCONMONDRV_DEVICE_EXTENSION)DriverObject->DeviceObject->DeviceExtension;

	status = IoAttachDeviceToDeviceStackSafe(
		DriverObject->DeviceObject,
		condrvObj->DeviceObject,
		&devExt->AttachedToDeviceObject);

	if (condrvObj) ObDereferenceObject(condrvObj);

	if (!NT_SUCCESS(status))
	{
	EXIT_FAILED:
		if (g_FastIoDispatch) ExFreePoolWithTag(g_FastIoDispatch, 'GCCN');
		if (DriverObject->DeviceObject) IoDeleteDevice(DriverObject->DeviceObject);
	}
	DbgPrint("Driver loaded\n");

	return status;
}