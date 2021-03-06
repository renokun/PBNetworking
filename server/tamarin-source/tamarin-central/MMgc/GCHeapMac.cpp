/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is [Open Source Virtual Machine.].
 *
 * The Initial Developer of the Original Code is
 * Adobe System Incorporated.
 * Portions created by the Initial Developer are Copyright (C) 2004-2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Adobe AS3 Team
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "MMgc.h"
#include "GC.h"

#ifdef USE_MMAP
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#if defined(__MACH__)
#include <mach/mach.h>
#endif

#ifdef _MAC
#define MAP_ANONYMOUS MAP_ANON
#endif

#if (defined(MMGC_IA32) || defined(MMGC_AMD64)) && defined(MEMORY_INFO)
#include <dlfcn.h>
#include <cxxabi.h>
#endif

namespace MMgc
{
#ifndef USE_MMAP
	void *aligned_malloc(size_t size, size_t align_size, GCMallocFuncPtr m_malloc)
	{
		char *ptr, *ptr2, *aligned_ptr;
		int align_mask = align_size - 1;

		int alloc_size = size + align_size + sizeof(int);
		ptr=(char *)m_malloc(alloc_size);

		if(ptr==NULL) return(NULL);

		ptr2 = ptr + sizeof(int);
		aligned_ptr = ptr2 + (align_size - ((size_t)ptr2 & align_mask));

		ptr2 = aligned_ptr - sizeof(int);
		*((int *)ptr2)=(int)(aligned_ptr - ptr);

		return(aligned_ptr);
	}

	void aligned_free(void *ptr, GCFreeFuncPtr m_free)
	{
		int *ptr2=(int *)ptr - 1;
		char *unaligned_ptr = (char*) ptr - *ptr2;
		m_free(unaligned_ptr);
	}
#endif /* !USE_MMAP */

#ifdef MMGC_AVMPLUS
#ifdef USE_MMAP
	static int get_major_version()
	{
		int mib[2];
		mib[0]=CTL_KERN;
		mib[1]=KERN_OSRELEASE;
		char buf[10];
		size_t siz=sizeof(buf);
		sysctl(mib, 2, &buf, &siz, NULL, 0);
		return strtol(buf, 0, 10);
	}

    static int get_mmap_fdes(int delta)
	{
	  // ensure runtime version
	  if(get_major_version() >= 9) // 9 == 10.5
		  return VM_MAKE_TAG(VM_MEMORY_APPLICATION_SPECIFIC_1+delta);
	  else
		  return -1;
	}

	int GCHeap::vmPageSize()
	{
		long v = sysconf(_SC_PAGESIZE);
		if (v == -1) v = 4096; // Mac 10.1 needs this
		return v;
	}

	void* GCHeap::ReserveCodeMemory(void* address,
									size_t size)
	{
		return mmap(address,
					size,
					PROT_NONE,
					MAP_PRIVATE | MAP_ANONYMOUS,
					get_mmap_fdes(1), 0);
	}

	void GCHeap::ReleaseCodeMemory(void* address,
								   size_t size)
	{
		if (munmap(address, size) != 0)
			GCAssert(false);
	}

	bool GCHeap::SetGuardPage(void* /*address*/)
	{
		return false;
	}

#ifdef AVMPLUS_JIT_READONLY
	/**
	 * SetPageProtection changes the page access protections on a block of pages,
	 * to make JIT-ted code executable or not.
	 *
	 * If executableFlag is true, the memory is made executable and read-only.
	 *
	 * If executableFlag is false, the memory is made non-executable and
	 * read-write.
	 */
	void GCHeap::SetPageProtection(void *address,
							   size_t size,
							   bool executableFlag,
							   bool writeableFlag)
	{
		// mprotect requires that the addresses be aligned on page boundaries
		void *endAddress = (void*) ((char*)address + size);
		void *beginPage = (void*) ((size_t)address & ~0xFFF);
		void *endPage   = (void*) (((size_t)endAddress + 0xFFF) & ~0xFFF);
		size_t sizePaged = (size_t)endPage - (size_t)beginPage;

		int flags = PROT_READ;
		if (executableFlag) {
			flags |= PROT_EXEC;
		}
		if (writeableFlag) {
			flags |= PROT_WRITE;
		}
		int retval = mprotect(beginPage, sizePaged, flags);

		GCAssert(retval == 0);
		(void)retval;
	}
#endif /* AVMPLUS_JIT_READONLY */
	
	void* GCHeap::CommitCodeMemory(void* address,
								   size_t size)
	{
		if (size == 0)
			size = GCHeap::kNativePageSize;  // default of one page

#ifdef AVMPLUS_JIT_READONLY
		int res = mprotect (address, size, PROT_READ | PROT_WRITE);
#else
		int res = mprotect (address, size, PROT_READ | PROT_WRITE | PROT_EXEC);
#endif /* AVMPLUS_JIT_READONLY */

		if (res != 0)
			address = 0;
		else
			address = (void*)( (uintptr)address + size );
	
		committedCodeMemory += size;
		return address;		
	}

	void* GCHeap::DecommitCodeMemory(void* address,
									 size_t size)
	{
		if (size == 0)
			size = GCHeap::kNativePageSize;  // default of one page

		// this doesn't "decommit" it, it just makes it unwritable, Mac OS still backs it with real pages :-(
		int res = mprotect (address, size, PROT_NONE);
		GCAssert(res == 0);
		(void) res;
		committedCodeMemory -= size;
		return (address);
	}
#else
	int GCHeap::vmPageSize()
	{
		return 4096;
	}

	void* GCHeap::ReserveCodeMemory(void* ,
									size_t size)
	{
		return aligned_malloc(size, 4096, m_malloc);
	}

	void GCHeap::ReleaseCodeMemory(void* address,
								   size_t )
	{
		aligned_free(address, m_free);
	}

	bool GCHeap::SetGuardPage(void *)
	{
		return false;
	}

#ifdef AVMPLUS_JIT_READONLY
	void GCHeap::SetExecuteBit(void *,
							   size_t ,
							   bool )
	{
		// No-op on Mac CFM
	}
#endif /* AVMPLUS_JIT_READONLY */
	
	
	void* GCHeap::CommitCodeMemory(void* address,
								   size_t size)
	{
		committedCodeMemory += size;
		return address;
	}	
	void* GCHeap::DecommitCodeMemory(void* address,
									 size_t size)
	{
		committedCodeMemory -= size;
		return address;
	}	
#endif /* USE_MMAP */	
#endif /* MMGC_AVMPLUS */

#ifdef USE_MMAP
	char* GCHeap::ReserveMemory(char *address, size_t size)
	{
		char *addr = (char*)mmap(address,
					 size,
					 PROT_NONE,
					 MAP_PRIVATE | MAP_ANONYMOUS,
					 get_mmap_fdes(0), 0);
					 
		// the man page for mmap documents it returns -1 to signal failure. 
		if (addr == (char *)-1) return NULL;
		
		if(address && address != addr) {
			// behave like windows and fail if we didn't get the right address
			ReleaseMemory(addr, size);
			return NULL;
		}
		return addr;
	}

	bool GCHeap::CommitMemory(char *address, size_t size)
	{
		int res = mprotect (address, size, PROT_READ | PROT_WRITE);
		GCAssert(res == 0);
		return (res == 0);
	}

	bool GCHeap::DecommitMemory(char *address, size_t size)
	{
		// FIXME: this doesn't actually untie the pages from real memory
		int res = mprotect (address, size, PROT_NONE);
		GCAssert(res == 0);
		return (res == 0);
	}

	bool GCHeap::CommitMemoryThatMaySpanRegions(char *address, size_t size)
	{
		return CommitMemory(address, size);
	}

	bool GCHeap::DecommitMemoryThatMaySpanRegions(char *address, size_t size)
	{
		return DecommitMemory(address, size);
	}

	void GCHeap::ReleaseMemory(char *address, size_t size)
	{
		int result = munmap(address, size);
		GCAssert(result == 0);
		(void)result;
	}
#else

	char* GCHeap::AllocateMemory(size_t size)
	{
		return (char *) aligned_malloc(size, 4096, m_malloc);
		//return (char *) MPAllocateAligned(size, kMPAllocate4096ByteAligned, 0);
	}

	void GCHeap::ReleaseMemory(char *address)
	{
		aligned_free(address, m_free);
		//MPFree(address);
	}
#endif

#ifdef MEMORY_INFO  

#ifdef MMGC_PPC
	// no idea how to do this with codewarrior
	void GetInfoFromPC(sintptr pc, char *buff, int /*buffSize*/) 
	{
		sprintf(buff, "0x%x", (uint32)pc);	
	}

	void GetStackTrace(sintptr *trace, int len, int skip) 
	{
	  register int stackp;
	  sintptr pc;
	  asm("mr %0,r1" : "=r" (stackp));
	  while(skip--) {
	    stackp = *(int*)stackp;
	  }
	  int i=0;
	  // save space for 0 terminator
	  len--;
	  while(i<len && stackp) {
	    pc = *((sintptr*)stackp+2);
	    trace[i++]=pc;
	    stackp = *(int*)stackp;
	  }
	  trace[i] = 0;
	}
#endif

#if (defined(MMGC_IA32) || defined(MMGC_AMD64))

	void GetInfoFromPC(sintptr pc, char *buff, int buffSize) 
	{
		int err = 0;
		Dl_info dlip;
		dladdr((void * const)pc, &dlip);
		const char* nm = abi::__cxa_demangle(dlip.dli_sname,0,0,&err);
		if (!nm || err) nm = dlip.dli_sname;
		snprintf(buff, buffSize, "0x%08x:%s", (uint32)pc, dlip.dli_sname);
	}
	
	void GetStackTrace(sintptr* trace, int len, int skip) 
	{
		void **ebp;
		#ifdef MMGC_IA32
		asm("mov %%ebp, %0" : "=r" (ebp));
		#else
		asm("mov %%rbp, %0" : "=r" (ebp));
		#endif
		while(skip-- && *ebp)
		{
			ebp = (void**)(*ebp);
		}

		len--;
		int i=0;
		while(i<len && *ebp)
		{
			trace[i++] = *((sintptr*)ebp+1);
			ebp = (void**)(*ebp);			
		}
		trace[i] = 0;
	}
#endif

#endif

	size_t GCHeap::GetPrivateBytes()
	{
		size_t private_bytes = 0;
		kern_return_t ret;
		task_t task = mach_task_self();
		
		vm_size_t pagesize = 0;
		ret = host_page_size(mach_host_self(), &pagesize);
		
		vm_address_t addr = VM_MIN_ADDRESS;
		vm_size_t size = 0;
		while (true)
		{
			mach_msg_type_number_t count = VM_REGION_TOP_INFO_COUNT;
			vm_region_top_info_data_t info;
			mach_port_t object_name;

			addr += size;

#ifdef MMGC_64BIT
			ret = vm_region_64(task, &addr, &size, VM_REGION_TOP_INFO, (vm_region_info_t)&info, &count, &object_name);
#else
			ret = vm_region(task, &addr, &size, VM_REGION_TOP_INFO, (vm_region_info_t)&info, &count, &object_name);
#endif

			if (ret != KERN_SUCCESS)
				break;
			private_bytes += info.private_pages_resident;
		}
		return private_bytes;
	}
}
