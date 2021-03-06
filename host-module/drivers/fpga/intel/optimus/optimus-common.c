#include "afu.h"
#include "optimus.h"

void dump_buffer_32(char *buf, uint32_t count)
{
	int i;
    uint32_t *x = (uint32_t*)buf;

    for (i = 0; i < count; i+=4) {
        x = (uint32_t*)(buf+i);
        //optimus_info("buffer: %x\n", *x);
    }
}

void dump_buffer_64(char *buf, uint32_t count)
{
	int i;
    uint64_t *x = (uint64_t*)buf;

    for (i = 0; i < count; i+=8) {
        x = (uint64_t*)(buf+i);
        optimus_info("buffer: %llx\n", *x);
    }
}

struct paccel* kobj_to_paccel(struct kobject *kobj,
            struct optimus *optimus, struct mdev_device *mdev,
            optimus_mode_t *mode, u32 *mode_id)
{
    char name[OPTIMUS_STRING_LEN];
    struct paccel *paccel = NULL;
    int i;

    for (i=0; i<optimus->npaccels; i++) {

        if (optimus->paccels[i].mode == VACCEL_TYPE_DIRECT) {
            snprintf(name, OPTIMUS_STRING_LEN, "%s-direct-%d",
                        dev_driver_string(mdev_parent_dev(mdev)),
                        optimus->paccels[i].mode_id);
        }
        else {
            snprintf(name, OPTIMUS_STRING_LEN, "%s-time_slicing-%d",
                        dev_driver_string(mdev_parent_dev(mdev)),
                        optimus->paccels[i].mode_id);
        }

        optimus_info("%s: scan %s\n", __func__, name);

        if (!strcmp(kobj->name, name)) {
            *mode = optimus->paccels[i].mode;
            *mode_id = optimus->paccels[i].mode_id;
            paccel = &optimus->paccels[i];
            return paccel;
        }
    }

    *mode = VACCEL_TYPE_DIRECT;
    *mode_id = -1;

    return NULL;
}

struct optimus* mdev_to_optimus(struct mdev_device *mdev)
{
    struct optimus *d, *tmp_d;
    struct optimus *ret = NULL;

    mutex_lock(&optimus_list_lock);
    list_for_each_entry_safe(d, tmp_d, &optimus_list, next) {
        if (d->pafu_device == mdev_parent_dev(mdev)) {
            optimus_info("%s: found optimus\n", __func__);
            ret = d;
            break;
        }
    }
    mutex_unlock(&optimus_list_lock);

    return ret;
}

void iommu_unmap_region(struct iommu_domain *domain,
                int flags, u64 start, u64 npages)
{
    long idx, idx_end;
    kvm_pfn_t pfn;
    u64 cnt = 0;

    optimus_info("unmap iommu region start %llx pages %llx\n", start, npages);

    idx = start;
    idx_end = start + npages * PAGE_SIZE;
    for (; idx < idx_end; idx += PAGE_SIZE) {
        pfn = (iommu_iova_to_phys(domain, idx) >> PAGE_SHIFT);
        if (pfn) {
            iommu_unmap(domain, idx, PAGE_SIZE);
            kvm_release_pfn_clean(pfn);
            cnt++;
        }
    }

    optimus_info("unmap %lld pages\n", cnt);
}

struct optimus* pdev_to_optimus(struct platform_device *pdev)
{
    struct device *pafu = &pdev->dev;
    struct optimus *d, *tmp_d;
    struct optimus *ret = NULL;

    mutex_lock(&optimus_list_lock);
    list_for_each_entry_safe(d, tmp_d, &optimus_list, next) {
        if (d->pafu_device == pafu) {
            optimus_info("%s: found optimus\n", __func__);
            ret = d;
            break;
        }
    }
    mutex_unlock(&optimus_list_lock);

    return ret;
}

struct optimus* device_to_optimus(struct device *pafu)
{
    struct optimus *d, *tmp_d;
    struct optimus *ret = NULL;

    mutex_lock(&optimus_list_lock);
    list_for_each_entry_safe(d, tmp_d, &optimus_list, next) {
        if (d->pafu_device == pafu) {
            optimus_info("%s: found optimus\n", __func__);
            ret = d;
            break;
        }
    }
    mutex_unlock(&optimus_list_lock);

    return ret;
}


