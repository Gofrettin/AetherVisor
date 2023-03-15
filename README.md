# AetherVisor: AMD-V memory hacking library

AetherVisor is a minimalistic type-1 AMD hypervisor that provides a memory hacking interface.  

Here's how AetherVisor's features are implemented: https://mellownight.github.io/2023/01/19/AetherVisor.html. 

If you experience any bugs, feel free to open an issue and/or propose a better fix.

## Features

<br>

### Syscall hooks via MSR_LSTAR
```Aether::SyscallHook::Enable()``` - Enables process-wide system call hooks.

```Aether::SyscallHook::Disable()``` - Disables system call hooks.


<br>

### NPT inline hooks

<code>
Aether::NptHook::Set(uintptr_t address, uint8_t* patch, size_t patch_len, NCR3_DIRECTORIES ncr3_id = NCR3_DIRECTORIES::primary, bool global_page = false);</code> - Sets an NPT hook. 
<br>

`patch` - Hook shellcode 

`patch_len` - Hook shellcode 

`length address` - Target address 

`ncr3_id` - The nested paging context that the hook is active in. 

`global_page` - Indicates that a global copy-on-write page (e.g. kernel32.dll, ntdll.dll, etc.) is being hooked.


<br>

### Branch tracing
```Aether::BranchTracer::Init() ``` - Initializes Branch tracer.

```Aether::BranchTracer::Trace(uint8_t* start_addr, uintptr_t range_base, uintptr_t range_size, uint8_t* stop_addr = NULL) ``` - Logs every branch executed until either the return address or the `stop_addr` is reached.

`start_addr` - Where to start tracing

`range_base` - Branches that occur below `range_base` are excluded from the trace

`range_base` - Branches that occur above `range_base` + `range_size` are excluded from the trace

`stop_address` - Where to stop tracing branches; if this value is NULL, the return address on the stack is used. 
 
<br>

#### Sandboxing and Read/Write/Execute instrumentation

```Aether::Sandbox::SandboxRegion(uintptr_t base, uintptr_t size);``` - Puts a region of memory/code into the no-execute region.

```Aether::Sandbox::DenyRegionAccess(void* base, size_t range, bool allow_reads);``` - Deny read/write access to pages outside of the sandbox for code inside of the sandbox.

`base` - Base of the region

`range` - Size of the region

`allow_reads` - If true, only hook write access; otherwise, hook both read and write access.

```Aether::Sandbox::UnboxRegion(uintptr_t base, uintptr_t size);``` - Removes pages from the sandbox.

`base` - Base of the region

`size` - Size of the region


```Aether::SetCallback(CALLBACK_ID handler_id, void* address);``` - Registers an instrumentation callback at `address` to handle an event at `handler_id`


#### callback IDs and callback function prototypes:

<br>

<code>

enum CALLBACK_ID
{
    //  void (*sandbox_mem_access_event)(GuestRegisters* registers, void* o_guest_rip);
    sandbox_readwrite = 0, 

    //  void (*sandbox_execute_event)(GuestRegisters* registers, void* return_address, void* original_guest_rip);
    sandbox_execute = 1,

    //  void (*branch_callback)(GuestRegisters* registers, void* return_address, void* original_guest_rip, void* LastBranchFromIP);
    branch = 2,

    //  void (*branch_trace_finish_event)();
    branch_trace_finished = 3,

    //  void (*syscall_hook)(GuestRegisters* registers, void* return_address, void* o_guest_rip);
    syscall = 4,

    max_id,
};

</code>


### vmmcall interface

```svm_vmmcall(VMMCALL_ID, ...)``` - Calls the hypervisor to do stuff

## Components ##

**AetherVisor-lib -** Static library that contains wrappers for interfacing with AetherVisor via vmmcall.

**AetherVisor-lib-kernel -** A version of AetherVisor-api designed for compilation with Windows kernel drivers.

**AetherVisor-example -** EXE demonstrating AetherVisor's features.

## Supported hardware ##
 Intel processors with VT-x and EPT support

## Supported platforms ##
 Windows 7 - Windows 10, x64 only
 
