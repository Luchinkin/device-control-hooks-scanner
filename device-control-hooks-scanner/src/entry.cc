#include "defs.hh"

#define dbg_print( format, ...) DbgPrintEx( DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, format, __VA_ARGS__ );

extern "C" NTSYSAPI PVOID RtlPcToFileHeader( PVOID PcValue, PVOID *BaseOfImage );

extern "C" NTSTATUS DriverEntry( DRIVER_OBJECT * driver_object, UNICODE_STRING* /*registry_path*/ )
{
	UNREFERENCED_PARAMETER( driver_object );

	auto on_driver_callback = []( PDRIVER_OBJECT driver )
	{
		const auto device_control_ptr = reinterpret_cast<unsigned __int64>( driver->MajorFunction[ IRP_MJ_DEVICE_CONTROL ] );
		const auto start = reinterpret_cast<unsigned __int64>( driver->DriverStart );
		const auto end = start + driver->DriverSize;

		const bool in_bounds = device_control_ptr >= start && device_control_ptr <= end;
		if ( !in_bounds )
		{
			[[maybe_unused]] void* current_image_base = nullptr;
			if ( !RtlPcToFileHeader( reinterpret_cast<void*>( device_control_ptr ), &current_image_base ) )
			{
				dbg_print(
					"Device control was not in bounds or outside legitimate module:\n\t-> name: %wZ\n\t-> service name: %wZ\n\t-> device control address: 0x%llX\n\t-> driver bounds: 0x%llX - 0x%llX\n",
					&driver->DriverName, &driver->DriverExtension->ServiceKeyName, device_control_ptr, start, end );
			}
		}
	};

	auto iterate_drivers = []( void( *callback )( PDRIVER_OBJECT ) )
	{
		UNICODE_STRING dir_path{};
		RtlInitUnicodeString( &dir_path, L"\\Driver" );

		OBJECT_ATTRIBUTES attributes{};

		InitializeObjectAttributes( &attributes, &dir_path, OBJ_CASE_INSENSITIVE, NULL, NULL );

		HANDLE dir_handle = nullptr;
		auto status = ZwOpenDirectoryObject( &dir_handle, DIRECTORY_QUERY, &attributes );
		if ( !NT_SUCCESS( status ) || !dir_handle )
			return;

		POBJECT_DIRECTORY dir_obj = nullptr;
		status = ObReferenceObjectByHandle( dir_handle, DIRECTORY_ALL_ACCESS, nullptr,
											KernelMode, reinterpret_cast<void**>( &dir_obj ), nullptr );

		if ( !NT_SUCCESS( status ) || !dir_obj )
		{
			ZwClose( dir_handle );
			return;
		}

		ExAcquirePushLockExclusiveEx( &dir_obj->Lock, 0 );

		for ( auto entry : dir_obj->HashBuckets )
		{
			if ( !entry )
				continue;

			while ( entry && entry->Object )
			{
				auto driver = reinterpret_cast<PDRIVER_OBJECT>( entry->Object );
				if ( !driver )
					continue;

				callback( driver );

				entry = entry->ChainLink;
			}
		}

		ExReleasePushLockExclusiveEx( &dir_obj->Lock, 0 );

		ObDereferenceObject( dir_obj );
		ZwClose( dir_handle );
	};

	iterate_drivers( on_driver_callback );

	return STATUS_SUCCESS;
}
