#include "afu.h"
#include "optimus.h"
//#include <xmmintrin.h>
//#include <time.h>

uint64_t get_time_ns(struct timespec *tv)
{
    uint64_t t =  tv->tv_sec * 1000*1000*1000 + tv->tv_nsec;
    return t;
}

int optimus_vtp_srv(void *args)
{
    struct optimus* optimus = (struct optimus*)args;
    
    // Pointer to the next translation request
    volatile uint64_t* next_req = (volatile uint64_t*)optimus->vtp_buffer;

    // End of the 4KB ring buffer
    volatile uint64_t* buf_end = next_req;
    uint64_t req_va;
    uint64_t rsp_pa;
    uint32_t page_size;
    unsigned long nsec;
    bool req_is_speculative;
    struct timespec tv;
    uint64_t t1,t2;
    buf_end = (volatile uint64_t*)((uint64_t)buf_end + 4096);
    optimus_info("enter vtp service \n");
    // Server loop
    while (true)
    {
        // Wait for next request
        uint32_t trips = 0;
        while (0 == *next_req)
        {
            //_mm_pause();
            if (kthread_should_stop()) {
                return 0;
            }
            __builtin_ia32_pause ();
            //ndelay(100);
            // Stop wasting CPU time if no requests are arriving
            if ((++trips & 0xffffff) == 0)
            {
                //struct timespec sleep_time;
                //sleep_time.tv_sec = 0;
                //sleep_time.tv_nsec = 100;
                nsec = 100;
                while (0 == *next_req)
                {
                    //nanosleep(&sleep_time, NULL);
                    if (kthread_should_stop()) {
                        return 0;
                    }
                    ndelay(nsec);
                    if (nsec == 10000) 
                        cond_resched();
                    // Exponential backoff
                    if (((++trips & 0xffff) == 0) && (nsec < 10000))
                    {
                        nsec *= 10;
                    }
                }

                break;
            }
        }
        if (kthread_should_stop()) {
            return 0;
        }
        // Drop the low bit from the request address. They are used as flags.
        req_va= (uint64_t)(*next_req & ~(uint64_t)3);
        
        // Is the request a speculative?
        req_is_speculative = (*next_req & 2);
        //if (req_is_speculative) 
        //    optimus_info("speculative req\n");

        // Translate the address, pinning the page if necessary.
        
        mutex_lock(&optimus->pt_lock);
        
        getnstimeofday(&tv);
        t1 = get_time_ns(&tv);
        rsp_pa = hash_pt_iova_to_hpa(optimus, req_va, &page_size, NULL);
        //rsp_pa = vtp_pt_iova_to_hpa(optimus->pt, req_va, &page_size);
        getnstimeofday(&tv);
        t2 = get_time_ns(&tv);
        optimus_info("time: %lluns\n", t2-t1);
        mutex_unlock(&optimus->pt_lock);
        //optimus_info("%s: vtp gva %llx translated to gpa %llx pgsize %llx\n",
        //           __func__, req_va, rsp_pa, page_size);

        if (rsp_pa)
        {
            // Response is the PA line address
            uint64_t rsp = rsp_pa >> 6;

            // Set bit 1 for 2MB pages
            if (page_size == PGSIZE_2M)
            {
                rsp |= 2;
            }
            writeq(rsp, &optimus->pafu_mmio[VTP_BASE_MMIO + CCI_MPF_VTP_CSR_PAGE_TRANSLATION_RSP]);
            
            
            
            //optimus_info("VTP translation response VA 0x%016llx -> PA 0x%016llx (rsp 0x%016llx), %llx\n", req_va, rsp_pa, rsp, page_size);
            
        }
        else
        {
            // Translation failure! Most likely the memory is not mapped.
            //optimus_info("translation failed\n");
            // Tell the FPGA by setting bit 1 of the response.
            //if (req_is_speculative) {
            //    optimus_info("speculative translation failed\n");
            //}
            writeq(1, &optimus->pafu_mmio[VTP_BASE_MMIO + CCI_MPF_VTP_CSR_PAGE_TRANSLATION_RSP]);

            
        }

        // Done with request. Move on to the next one. Only one request is sent
        // per line.
        *next_req = 0;
        next_req += 8;
        if (next_req == buf_end)
        {
            next_req = (volatile uint64_t*)optimus->vtp_buffer;
        }
        //cond_resched();
    }

    return 0;
}
