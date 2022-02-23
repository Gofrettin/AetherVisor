#include "itlb_hook.h"
#include "vmexit.h"

namespace TlbHooks
{
	int hook_count;
	SplitTlbHook first_tlb_hook;

	SplitTlbHook* SetTlbHook(void* address, uint8_t* patch, size_t patch_len)
	{
		__invlpg(address);

		auto hook_entry = &first_tlb_hook;

		for (int i = 0; i < hook_count; hook_entry = hook_entry->next_hook, ++i)
		{
		}

		CR3 cr3;
		cr3.Flags = __readcr3();

		hook_entry->hooked_pte = Utils::GetPte(address, cr3.AddressOfPageDirectory << PAGE_SHIFT);
		hook_entry->hooked_pte->ExecuteDisable = 0;
		hook_entry->hooked_page_va = PAGE_ALIGN(address);

		auto hookless_pfn = hook_entry->hookless_pte->PageFrameNumber;

		auto copy_page = Utils::VirtualAddrFromPfn(hookless_pfn);

		memcpy(copy_page, (uint8_t*)PAGE_ALIGN(address), PAGE_SIZE);

		auto irql = Utils::DisableWP();
		auto retptr = Utils::FindPattern((uintptr_t)PAGE_ALIGN(address), PAGE_SIZE, "\xCC", 1, 0x00);
		
		memcpy(address, patch, patch_len);

		Utils::EnableWP(irql);

		/*	bp execution in guest context to fill iTLB in guest ASID	*/		
		hook_entry->exec_gadget = (uint8_t*)retptr;
		hook_entry->hooked_pte->Present = 0;

		__invlpg(address);
		hook_count += 1;

		return hook_entry;
	}


	SplitTlbHook* FindByPage(void* virtual_addr)
	{
		auto hook_entry = &first_tlb_hook;

		for (int i = 0; i < hook_count; hook_entry = hook_entry->next_hook, ++i)
		{
			if (hook_entry->hooked_page_va == virtual_addr)
			{
				return hook_entry;
			}
		}
		return 0;
	}

	SplitTlbHook* FindByHookedPhysicalPage(uint64_t page_physical)
	{
		PFN_NUMBER pfn = page_physical >> PAGE_SHIFT;
		auto hook_entry = &first_tlb_hook;

		for (int i = 0; i < hook_count; hook_entry = hook_entry->next_hook, ++i)
		{
			if (hook_entry->hooked_pte->PageFrameNumber == pfn)
			{
				return hook_entry;
			}
		}
		return 0;
	}

	SplitTlbHook* FindByReadGadget(uintptr_t rip)
	{
		auto hook_entry = &first_tlb_hook;

		for (int i = 0; i < hook_count; hook_entry = hook_entry->next_hook, ++i)
		{
			if (Utils::Diff((uintptr_t)hook_entry->read_gadget, rip) < 16)
			{
				return hook_entry;
			}
		}
		return 0;
	}

	SplitTlbHook* FindByExecuteGadget(uintptr_t rip)
	{
		auto hook_entry = &first_tlb_hook;

		for (int i = 0; i < hook_count; hook_entry = hook_entry->next_hook, ++i)
		{
			if (Utils::Diff((uintptr_t)hook_entry->exec_gadget, rip) < 16)
			{
				return hook_entry;
			}
		}
		return 0;
	}

	void Init()
	{
		auto first_hookless_page = ExAllocatePool(NonPagedPool, PAGE_SIZE);

		CR3 cr3;
		cr3.Flags = __readcr3();

		first_tlb_hook.hookless_pte = Utils::GetPte(first_hookless_page, cr3.AddressOfPageDirectory << PAGE_SHIFT);
		first_tlb_hook.next_hook = (SplitTlbHook*)ExAllocatePool(NonPagedPool, sizeof(SplitTlbHook));

		auto hook_entry = &first_tlb_hook;

		/*	reserve memory for hooks because we can't allocate memory in VM root	*/

		int max_hooks = 20;

		for (int i = 0; i < max_hooks; hook_entry = hook_entry->next_hook, ++i)
		{
			auto hookless_page = ExAllocatePool(NonPagedPool, PAGE_SIZE);

			hook_entry->read_gadget = (uint8_t*)ExAllocatePool(NonPagedPool, 12);

			hook_entry->next_hook = (SplitTlbHook*)ExAllocatePool(NonPagedPool, sizeof(SplitTlbHook));
			hook_entry->next_hook->hookless_pte = Utils::GetPte(hookless_page, cr3.AddressOfPageDirectory << PAGE_SHIFT);

			memcpy(hook_entry->read_gadget, "\x48\xA1\x00\x00\x00\x00\x00\x00\x00\x00\xCC", 11);
		}

		hook_count = 0;
	}

	void HandleTlbHookBreakpoint(CoreVmcbData* vcpu)
	{
		auto rip = vcpu->guest_vmcb.save_state_area.Rip;

		PHYSICAL_ADDRESS ncr3;
		ncr3.QuadPart = vcpu->guest_vmcb.control_area.NCr3;

		auto pte = Utils::GetPte((void*)rip, ncr3.QuadPart);

		SplitTlbHook* hook_entry = hook_entry = TlbHooks::FindByExecuteGadget(rip);

		if (!hook_entry)
		{
			hook_entry = TlbHooks::FindByReadGadget(rip);
			Logger::Log("[AMD-Hypervisor] - read gadget caught, added dTLB entry! \n");
		
			/*	Make sure we can intercept memory access after TLB miss	*/
			pte->Present = 0;	
		}
		else 
		{
			Logger::Log("[AMD-Hypervisor] - exec gadget caught, added iTLB entry! \n");

			pte->Present = 0;
		}
	}

	void HandlePageFaultTlb(CoreVmcbData* vcpu, GPRegs* guest_regs)
	{
		auto fault_address = (void*)vcpu->guest_vmcb.control_area.ExitInfo2;

		PageFaultErrorCode error_code;
		error_code.as_uint32 = vcpu->guest_vmcb.control_area.ExitInfo1;

		auto hook_entry = TlbHooks::FindByPage(PAGE_ALIGN(fault_address));

		if (!hook_entry)
		{
			Logger::Log("[AMD-Hypervisor] - This is a normal page fault at %p \n", fault_address);

			InjectException(vcpu, 14, error_code.as_uint32);
			return;
		}

		Logger::Log("[AMD-Hypervisor] -This page fault is from our hooked page at %p \n", fault_address);
		
		/*	make room for our own TLB entries	*/
		vcpu->guest_vmcb.control_area.TlbControl = 3;

		if (error_code.fields.execute)
		{
			/*	mark page as present	*/

			auto pte = Utils::GetPte(fault_address, vcpu->guest_vmcb.save_state_area.Cr3,
				[](PT_ENTRY_64* pte) -> int {
					pte->Present = 1;
					return 0;
				}
			);

			vcpu->guest_vmcb.save_state_area.Rip = (uintptr_t)hook_entry->exec_gadget;
		}
		else
		{
			auto pte = Utils::GetPte(fault_address, vcpu->guest_vmcb.save_state_area.Cr3);

			Logger::Log(
				"[AMD-Hypervisor] - Read access on hooked page, rip = 0x%p \n", 
				vcpu->guest_vmcb.save_state_area.Rip
			);

			/*	This is the page of one of our hooks, load the innocent page frame into dTLB	*/
			pte->Present = 1;
			pte->PageFrameNumber = hook_entry->hookless_pte->PageFrameNumber;

			vcpu->guest_vmcb.save_state_area.Rip = (uintptr_t)hook_entry->read_gadget;		
		}
	}
};
