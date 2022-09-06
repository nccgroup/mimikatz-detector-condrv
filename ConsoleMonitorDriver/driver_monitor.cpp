/*
Console Monitor
Balazs Bucsay 2022 <balazs.bucsay@nccgroup.com>


This tool is slightly based on WinConMon (https://github.com/EyeOfRa/WinConMon) written by EyeOfRa
and also on Mandiant's research see below:

https://www.mandiant.com/resources/monitoring-windows-console-activity-part-one
https://www.mandiant.com/resources/monitoring-windows-console-activity-part-2

*/

#include <ntddk.h>
#include <ntstatus.h>

UNICODE_STRING DEVICE_NAME = RTL_CONSTANT_STRING(L"\\Device\\ConMonitor");
UNICODE_STRING DEVICE_SYMBOLIC_NAME = RTL_CONSTANT_STRING(L"\\??\\ConMonitor");
UNICODE_STRING SubDeviceName = RTL_CONSTANT_STRING(L"\\ConMonitor");
UNICODE_STRING CONDRV_DRIVER_NAME = RTL_CONSTANT_STRING(L"\\Driver\\Condrv");

FAST_IO_DISPATCH* g_FastIoDispatch = NULL;

HANDLE hPipe = NULL;
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
	ULONG handle; // 4 bytes
	PVOID buffer; // 8 bytes
	ULONG buffLength; // 4 bytes
	ULONG type; // 4bytes
} CONSOLE_IO_MSG_T, * LPCONSOLE_IO_MSG_T;

typedef struct _CONMONDRV_DEVICE_EXTENSION
{
	PDEVICE_OBJECT AttachedToDeviceObject;
} CONMONDRV_DEVICE_EXTENSION, * LPCONMONDRV_DEVICE_EXTENSION;

extern "C"
VOID DriverUnload(__in PDRIVER_OBJECT driverObject)
{
	sizeof(PVOID);
	LPCONMONDRV_DEVICE_EXTENSION devExt = (LPCONMONDRV_DEVICE_EXTENSION)driverObject->DeviceObject->DeviceExtension;
	if (devExt->AttachedToDeviceObject)
	{
		IoDetachDevice(devExt->AttachedToDeviceObject);
	devExt->AttachedToDeviceObject = NULL;
		}

	NTSTATUS status = IoDeleteSymbolicLink(&DEVICE_SYMBOLIC_NAME);
	UNREFERENCED_PARAMETER(status);

	if (g_FastIoDispatch) ExFreePoolWithTag(g_FastIoDispatch, 'GCCN');

	if (driverObject->DeviceObject)
		IoDeleteDevice(driverObject->DeviceObject);

	if (hPipe) ZwClose(hPipe);
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
	UCHAR* p;
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
	DbgPrint("Called: %08X - Length: %d\n", IoControlCode, InputBufferLength);
	switch (IoControlCode)
	{
	// Write IO output - input typed into shells
	case 0x500013:
	{
		DbgPrint("INPUT Type: %x - %p - blength: %x\n", pMsg->type, pMsg, pMsg->buffLength);
		//DbgPrint("Called: %08X - IO OUTPUT (input to shell) Length: %d\n", IoControlCode, InputBufferLength);
		
		DbgPrint("INPUT %ws\n", pMsg->buffer);
		DbgPrint("INPUT HEX: ");
		p = (unsigned char*)pMsg->buffer;
		for (ULONG i = 0; i < pMsg->buffLength; i++) DbgPrint("%02X ", *p++);
		DbgPrint("\n");
		
		if (pMsg->type == 0x14) 
		{	
			NTSTATUS status = ZwWriteFile(hPipe, NULL, NULL, NULL, &iostat, pMsg->buffer,
				pMsg->buffLength, NULL, NULL);
			if ((status != STATUS_SUCCESS) && (status != STATUS_PENDING)) DbgPrint("ZwWriteFile error: %08x\n", status);
		}
		// interactive message
		else if (pMsg->type == 0x08)
		{
			if ((((char*)pMsg->buffer)[0] == 0x01) && (((char*)pMsg->buffer)[4] == 0x01))
			{
				NTSTATUS status = ZwWriteFile(hPipe, NULL, NULL, NULL, &iostat, ((char *)pMsg->buffer)+14,
					sizeof(WCHAR), NULL, NULL);
				if ((status != STATUS_SUCCESS) && (status != STATUS_PENDING)) DbgPrint("ZwWriteFile error: %08x\n", status);
			}
		}
		else
		{
			// Other type of messages (other than whole line from cmd/interactive from powershell)
			
			DbgPrint("OUTPUT Type: %x - %p - blength: %x\n", pMsg->type, pMsg, pMsg->buffLength);
			DbgPrint("OUTPUT HEX: ");
			p = (unsigned char*)pMsg->buffer;
			for (ULONG i = 0; i < pMsg->buffLength; i++) DbgPrint("%02X ", *p++);
			DbgPrint("\n");
		}
		break;
	}
	// Read IO input - output for the commands
	case 0x50000F:
	{
		DbgPrint("Called: %08X - IO INPUT (output to shell) Length: %d\n", IoControlCode, InputBufferLength);
		
		// normal message printed as response
		if (pMsg->type == 0x10) {
			NTSTATUS status = ZwWriteFile(hPipe, NULL, NULL, NULL, &iostat, pMsg->buffer,
				pMsg->buffLength, NULL, NULL);
			if ((status != STATUS_SUCCESS) && (status != STATUS_PENDING)) DbgPrint("ZwWriteFile error: %08x\n", status);
		}
		// character by character
		else if (pMsg->type == 0x00)
		{
			// awful way to detect printeble characters
			if (pMsg->buffLength == 1)
			{
				NTSTATUS status = ZwWriteFile(hPipe, NULL, NULL, NULL, &iostat, pMsg->buffer,
					sizeof(WCHAR), NULL, NULL);
				if ((status != STATUS_SUCCESS) && (status != STATUS_PENDING)) DbgPrint("ZwWriteFile error: %08x\n", status);
			}
		}
		// Set title?
		else if (pMsg->type == 0x09)
		{
			NTSTATUS status = ZwWriteFile(hPipe, NULL, NULL, NULL, &iostat, pMsg->buffer,
				pMsg->buffLength, NULL, NULL);
			if ((status != STATUS_SUCCESS) && (status != STATUS_PENDING)) DbgPrint("ZwWriteFile error: %08x\n", status);
		}
		else
		{
			// TODO investigate the remaining messages
			DbgPrint("OUTPUT Type: %x - %p - blength: %x\n", pMsg->type, pMsg, pMsg->buffLength);
			DbgPrint("OUTPUT message: %ws\n", pMsg->buffer);
			DbgPrint("OUTPUT HEX: ");
			p = (unsigned char*)pMsg->buffer;
			for (ULONG i = 0; i < pMsg->buffLength; i++) DbgPrint("%02X ", *p++);
			DbgPrint("\n");
		}

		break;
	}
	case 0x500006: // CdCompleteIo
	{
		/*DbgPrint("Called: %08X - CdCompleteIo Length: %d\n", IoControlCode, InputBufferLength);
		if (InputBufferLength)
		{
			DbgPrint("OUTPUT HEX: ");
			p = (unsigned char*)InputBuffer;
			for (ULONG i = 0; i < InputBufferLength; i++) DbgPrint("%02X ", *p++);
			DbgPrint("\n");
		}
		__debugbreak();*/
		break;
	}
	case 0x500016: // ??
	{
		break;
	}
	case 0x50000B: // CdCompleteIo 2
	{
		/*if (InputBufferLength)
		{
			DbgPrint("OUTPUT HEX: ");
			p = (unsigned char*)InputBuffer;
			for (ULONG i = 0; i < InputBufferLength; i++) DbgPrint("%02X ", *p++);
			DbgPrint("\n");
		}*/
		break;
	}
	case 0x500023: // ??
	{
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

	/* Not entirely sure how to do the disctinction between the requests that are addressed to the \\.\Condrv
	* and this driver. Best way found was to open the device with a subname and compare to that
	*/
	if (irpStack->FileObject->FileName.Length == 0x16) // L"\ConMonitor" 
		if (RtlEqualUnicodeString(&irpStack->FileObject->FileName, &SubDeviceName, TRUE))
		{
			Irp->IoStatus.Information = 0;
			Irp->IoStatus.Status = STATUS_SUCCESS;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);

			return STATUS_SUCCESS;
		}

	IoSkipCurrentIrpStackLocation(Irp);
	return IoCallDriver(devExt->AttachedToDeviceObject, Irp);
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(DriverObject);
	UNREFERENCED_PARAMETER(RegistryPath);

	NTSTATUS status = 0;
	UNICODE_STRING sPipe, funcName;
	OBJECT_ATTRIBUTES attr;
	NTSTATUS ret;
	PDRIVER_OBJECT condrvObj = NULL;
	extern POBJECT_TYPE* IoDriverObjectType;

	DbgPrint("Driver loading");

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

	RtlInitUnicodeString(&sPipe, L"\\??\\pipe\\ConMonDrvPipe");

	InitializeObjectAttributes(&attr, &sPipe, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	ret = ZwCreateFile(&hPipe,
		SYNCHRONIZE | FILE_WRITE_DATA, // or FILE_READ_DATA
		&attr,
		&iostat,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		FILE_OPEN,
		FILE_NON_DIRECTORY_FILE,
		NULL,
		0);

	if (ret != STATUS_SUCCESS)
	{
		DbgPrint("Loading driver failed: 0x%08x\n", DEVICE_NAME);
		return ret;
	}
		

	status = IoCreateDevice(DriverObject, sizeof(CONMONDRV_DEVICE_EXTENSION), &DEVICE_NAME, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DriverObject->DeviceObject);
	if (!NT_SUCCESS(status))
	{
		DbgPrint("Could not create device %wZ", DEVICE_NAME);
	}
	else
	{
		DbgPrint("Device %wZ created", DEVICE_NAME);
	}

	status = IoCreateSymbolicLink(&DEVICE_SYMBOLIC_NAME, &DEVICE_NAME);
	if (NT_SUCCESS(status))
	{
		DbgPrint("Symbolic link %wZ created", DEVICE_SYMBOLIC_NAME);
	}
	else
	{
		DbgPrint("Error creating symbolic link %wZ", DEVICE_SYMBOLIC_NAME);
	}

	// Turning on Direct I/O
	DriverObject->DeviceObject->Flags |= DO_DIRECT_IO;

	RtlInitUnicodeString(&funcName, L"ObReferenceObjectByName");
	fnObReferenceObjectByName = (Fn_ObReferenceObjectByName)MmGetSystemRoutineAddress(&funcName);
	if (!fnObReferenceObjectByName) goto EXIT_FAILED;

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
	DbgPrint("Driver loaded");

	return status;
}