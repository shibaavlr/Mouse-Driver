
#include <ntifs.h>
int _fltused;
typedef unsigned __int8  BYTE;
typedef unsigned __int16 WORD;
typedef unsigned __int32 DWORD;
typedef unsigned __int64 QWORD;
typedef int BOOL;

#include <ntifs.h>

extern "C" {
	NTKERNELAPI NTSTATUS IoCreateDriver(PUNICODE_STRING DriverName,
		PDRIVER_INITIALIZE InitializationFunction);

}



#pragma warning(disable : 4201)
typedef struct _MOUSE_INPUT_DATA {
	USHORT UnitId;
	USHORT Flags;
	union {
		ULONG Buttons;
		struct {
			USHORT ButtonFlags;
			USHORT ButtonData;
		};
	};
	ULONG  RawButtons;
	LONG   LastX;
	LONG   LastY;
	ULONG  ExtraInformation;
} MOUSE_INPUT_DATA, * PMOUSE_INPUT_DATA;

typedef VOID
(*MouseClassServiceCallbackFn)(
	PDEVICE_OBJECT DeviceObject,
	PMOUSE_INPUT_DATA InputDataStart,
	PMOUSE_INPUT_DATA InputDataEnd,
	PULONG InputDataConsumed
	);

typedef struct _MOUSE_OBJECT
{
	PDEVICE_OBJECT              mouse_device;
	MouseClassServiceCallbackFn service_callback;
	BOOL                        use_mouse;
} MOUSE_OBJECT, * PMOUSE_OBJECT;

BOOL UC_open(void);

extern "C" VOID MouseClassServiceCallback(
	PDEVICE_OBJECT DeviceObject,
	PMOUSE_INPUT_DATA InputDataStart,
	PMOUSE_INPUT_DATA InputDataEnd,
	PULONG InputDataConsumed
);

MOUSE_OBJECT UCObject;
extern "C" {
	QWORD _KeAcquireSpinLockAtDpcLevel;
	QWORD _KeReleaseSpinLockFromDpcLevel;
	QWORD _IofCompleteRequest;
	QWORD _IoReleaseRemoveLockEx;
};

namespace UC
{
	void UC(long x, long y, unsigned short button_flags);
}



namespace driver {
	namespace codes {
		
		constexpr ULONG move =
			CTL_CODE(FILE_DEVICE_UNKNOWN, 0x696, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);


	} 
	struct Request {
		long x;
		long y;
		unsigned short button_flags;
	};
	NTSTATUS create(PDEVICE_OBJECT device_object, PIRP irp) {
		UNREFERENCED_PARAMETER(device_object);

		IoCompleteRequest(irp, IO_NO_INCREMENT);

		return irp->IoStatus.Status;
	}

	NTSTATUS close(PDEVICE_OBJECT device_object, PIRP irp) {
		UNREFERENCED_PARAMETER(device_object);

		IoCompleteRequest(irp, IO_NO_INCREMENT);

		return irp->IoStatus.Status;
	}

	NTSTATUS device_control(PDEVICE_OBJECT device_object, PIRP irp) {
		UNREFERENCED_PARAMETER(device_object);



		NTSTATUS status = STATUS_UNSUCCESSFUL;

	
		PIO_STACK_LOCATION stack_irp = IoGetCurrentIrpStackLocation(irp);


		auto request = reinterpret_cast<Request*>(irp->AssociatedIrp.SystemBuffer);

		if (stack_irp == nullptr || request == nullptr) {
			IoCompleteRequest(irp, IO_NO_INCREMENT);
			return status;
		}


		static PEPROCESS target_process = nullptr;

		const ULONG control_code = stack_irp->Parameters.DeviceIoControl.IoControlCode;
		switch (control_code) {
		case codes::move:
			UC::UC(request->x, request->y, request->button_flags);
			status = STATUS_SUCCESS;
			break;

		default:
			break;
		}

		irp->IoStatus.Status = status;
		irp->IoStatus.Information = sizeof(Request);

		IoCompleteRequest(irp, IO_NO_INCREMENT);

		return status;
	}

}  
NTSTATUS driver_main(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path) {
	UNREFERENCED_PARAMETER(registry_path);

	UNICODE_STRING device_name = {};
	RtlInitUnicodeString(&device_name, L"\\Device\\UC");


	PDEVICE_OBJECT device_object = nullptr;
	NTSTATUS status = IoCreateDevice(driver_object, 0, &device_name, FILE_DEVICE_UNKNOWN,
		FILE_DEVICE_SECURE_OPEN, FALSE, &device_object);
	if (status != STATUS_SUCCESS) {

		return status;
	}



	UNICODE_STRING symbolic_link = {};
	RtlInitUnicodeString(&symbolic_link, L"\\DosDevices\\UC");

	status = IoCreateSymbolicLink(&symbolic_link, &device_name);
	if (status != STATUS_SUCCESS) {

		return status;
	}




	SetFlag(device_object->Flags, DO_BUFFERED_IO);

	driver_object->MajorFunction[IRP_MJ_CREATE] = driver::create;
	driver_object->MajorFunction[IRP_MJ_CLOSE] = driver::close;
	driver_object->MajorFunction[IRP_MJ_DEVICE_CONTROL] = driver::device_control;


	ClearFlag(device_object->Flags, DO_DEVICE_INITIALIZING);

	

	return status;
}





extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT Driver, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(Driver);
	UNREFERENCED_PARAMETER(RegistryPath);

	_KeAcquireSpinLockAtDpcLevel = (QWORD)KeAcquireSpinLockAtDpcLevel;
	_KeReleaseSpinLockFromDpcLevel = (QWORD)KeReleaseSpinLockFromDpcLevel;
	_IofCompleteRequest = (QWORD)IofCompleteRequest;
	_IoReleaseRemoveLockEx = (QWORD)IoReleaseRemoveLockEx;




	UNICODE_STRING driver_name = {};
	RtlInitUnicodeString(&driver_name, L"\\Driver\\UC");

	

	return IoCreateDriver(&driver_name, &driver_main);
	return STATUS_SUCCESS;
}


extern "C" NTSYSCALLAPI
NTSTATUS
ObReferenceObjectByName(
	__in PUNICODE_STRING ObjectName,
	__in ULONG Attributes,
	__in_opt PACCESS_STATE AccessState,
	__in_opt ACCESS_MASK DesiredAccess,
	__in POBJECT_TYPE ObjectType,
	__in KPROCESSOR_MODE AccessMode,
	__inout_opt PVOID ParseContext,
	__out PVOID * Object
);

extern "C" NTSYSCALLAPI POBJECT_TYPE * IoDriverObjectType;

BOOL UC_open(void)
{
	

	if (UCObject.use_mouse == 0) {

		UNICODE_STRING class_string;
		RtlInitUnicodeString(&class_string, L"\\Driver\\MouClass");


		PDRIVER_OBJECT class_driver_object = NULL;
		NTSTATUS status = ObReferenceObjectByName(&class_string, OBJ_CASE_INSENSITIVE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, (PVOID*)&class_driver_object);
		if (!NT_SUCCESS(status)) {
			UCObject.use_mouse = 0;
			return 0;
		}

		UNICODE_STRING hid_string;
		RtlInitUnicodeString(&hid_string, L"\\Driver\\MouHID");


		PDRIVER_OBJECT hid_driver_object = NULL;

		status = ObReferenceObjectByName(&hid_string, OBJ_CASE_INSENSITIVE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, (PVOID*)&hid_driver_object);
		if (!NT_SUCCESS(status))
		{
			if (class_driver_object) {
				ObfDereferenceObject(class_driver_object);
			}
			UCObject.use_mouse = 0;
			return 0;
		}

		PVOID class_driver_base = NULL;


		PDEVICE_OBJECT hid_device_object = hid_driver_object->DeviceObject;
		while (hid_device_object && !UCObject.service_callback)
		{
			PDEVICE_OBJECT class_device_object = class_driver_object->DeviceObject;
			while (class_device_object && !UCObject.service_callback)
			{
				if (!class_device_object->NextDevice && !UCObject.mouse_device)
				{
					UCObject.mouse_device = class_device_object;
				}

				PULONG_PTR device_extension = (PULONG_PTR)hid_device_object->DeviceExtension;
				ULONG_PTR device_ext_size = ((ULONG_PTR)hid_device_object->DeviceObjectExtension - (ULONG_PTR)hid_device_object->DeviceExtension) / 4;
				class_driver_base = class_driver_object->DriverStart;
				for (ULONG_PTR i = 0; i < device_ext_size; i++)
				{
					if (device_extension[i] == (ULONG_PTR)class_device_object && device_extension[i + 1] > (ULONG_PTR)class_driver_object)
					{
						UCObject.service_callback = (MouseClassServiceCallbackFn)(device_extension[i + 1]);

						break;
					}
				}
				class_device_object = class_device_object->NextDevice;
			}
			hid_device_object = hid_device_object->AttachedDevice;
		}

		if (!UCObject.mouse_device)
		{
			PDEVICE_OBJECT target_device_object = class_driver_object->DeviceObject;
			while (target_device_object)
			{
				if (!target_device_object->NextDevice)
				{
					UCObject.mouse_device = target_device_object;
					break;
				}
				target_device_object = target_device_object->NextDevice;
			}
		}

		ObfDereferenceObject(class_driver_object);
		ObfDereferenceObject(hid_driver_object);

		if (UCObject.mouse_device && UCObject.service_callback) {
			UCObject.use_mouse = 1;
		}

	}

	return UCObject.mouse_device && UCObject.service_callback;
}

#define KeMRaiseIrql(a,b) *(b) = KfRaiseIrql(a)
void UC::UC(long x, long y, unsigned short button_flags)
{
	KIRQL irql;
	ULONG input_data;
	MOUSE_INPUT_DATA mid = { 0 };
	mid.LastX = x;        
	mid.LastY = y;
	mid.ButtonFlags = button_flags;
	if (!UC_open()) {
		return;
	}
	mid.UnitId = 1;
	KeMRaiseIrql(DISPATCH_LEVEL, &irql);
	MouseClassServiceCallback(UCObject.mouse_device, &mid, (PMOUSE_INPUT_DATA)&mid + 1, &input_data);
	KeLowerIrql(irql);
}



