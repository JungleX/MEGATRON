#include <linux/moduleparam.h>
#include "afu.h"
#include "optimus.h"

int optimus_dbg = 0;
module_param(optimus_dbg, int, 0664);
MODULE_PARM_DESC(optimus_dbg, "enable debug info, default: 0");
unsigned long long tlb_opt_offset = 0;
module_param(tlb_opt_offset, ullong, 0664);
MODULE_PARM_DESC(tlb_opt_offset, "number of 4k pages offset applied after page slicing, default: 0");
int optimus_timeslc_mask = 0;
module_param(optimus_timeslc_mask, int, 0644);
MODULE_PARM_DESC(optimus_timeslc_mask, "enable time multiplexing, default: 0x0");
//FIXME: replace previous hardcoded offset with tlb_opt_offset

DEFINE_MUTEX(optimus_list_lock);
struct list_head optimus_list = LIST_HEAD_INIT(optimus_list);

void dump_paccels(struct optimus *optimus)
{
    struct paccel *paccels;
    int i;

    if (!optimus) {
        optimus_info("%s: failed\n", __func__);
        return;
    }
    
    paccels = optimus->paccels;
    if (!paccels) {
        optimus_info("%s: paccels empty\n", __func__);
        return;
    }

    for (i=0; i<optimus->npaccels; i++) {
        struct paccel *paccel = &optimus->paccels[i];
        paccel->ops->dump(paccel);
    }
}

static int optimus_iommu_fault_handler(struct iommu_domain *domain,
            struct device *dev, unsigned long iova, int flags, void *arg)
{
    optimus_err("iommu page fault at %lx\n", iova);
    return 0;
}

int vaccel_create(struct kobject *kobj, struct mdev_device *mdev)
{
    struct optimus *optimus;
    struct paccel *paccel;
    struct vaccel *vaccel;
    optimus_mode_t mode;
    u32 mode_id = ~0;
    int ret;

    if (!mdev)
        return -EINVAL;

    vaccel = kzalloc(sizeof(struct vaccel), GFP_KERNEL);
    if (!vaccel)
        return -ENOMEM;

    optimus = mdev_to_optimus(mdev);
    paccel = kobj_to_paccel(kobj, optimus, mdev, &mode, &mode_id);
    if (!paccel) {
        paccel_err(paccel,
                "%s: cannot decode mode and mode id", __func__);
        return -EFAULT;
    }

    if (!paccel->ops) {
        paccel_err(paccel, "no ops found");
        return -EFAULT;
    }

    /* call mode specify initialization function */
    ret = paccel->ops->vaccel_init(vaccel, paccel, mdev);
    if (ret) {
        paccel_err(paccel, "initialization failed");
        return ret;
    }

    /* add to optimus->vaccel_list */
    mutex_lock(&optimus->ops_lock);
    list_add(&vaccel->next, &optimus->vaccel_list);
    mutex_unlock(&optimus->ops_lock);

    /* dump result */
    vaccel_info(vaccel,
            "vaccel created. seq_id %x, mode %s, mode_id %d, gva_start %llx\n",
            vaccel->seq_id,
            vaccel->mode==VACCEL_TYPE_DIRECT?"direct":"timeslicing",
            vaccel->paccel->mode_id,
            vaccel->gva_start);
    dump_paccels(optimus);

    return 0;
}

int vaccel_remove(struct mdev_device *mdev)
{
    struct vaccel *mds, *tmp_mds;
    struct vaccel *vaccel = mdev_get_drvdata(mdev);
    struct optimus *optimus = mdev_to_optimus(mdev);
    struct paccel *paccel = vaccel->paccel;
    int ret = -EINVAL;

    optimus_info("call: %s on vaccel %d \n",__func__, vaccel->seq_id);

    mutex_lock(&optimus->ops_lock);
    list_for_each_entry_safe(mds, tmp_mds, &optimus->vaccel_list, next) {
        if (vaccel == mds) {
            list_del(&vaccel->next);

            ret = paccel->ops->vaccel_uinit(vaccel);
            if (ret) {
                paccel_err(paccel, "uinitialization failed");
                mutex_unlock(&optimus->ops_lock);
                return ret;
            }
            
            kfree(vaccel);
            ret = 0;
            break;
        }
    }
    mutex_unlock(&optimus->ops_lock);

    return ret;
}

int vaccel_open(struct mdev_device *mdev)
{
    struct vaccel *vaccel = mdev_get_drvdata(mdev);
    int ret = -EINVAL;

    mutex_lock(&vaccel->ops_lock);
    ret = vaccel->ops->open(mdev);
    if (ret) {
        vaccel_err(vaccel, "vaccel_open return false");
    }
    mutex_unlock(&vaccel->ops_lock);

    return ret;
}

void vaccel_close(struct mdev_device *mdev)
{
    struct vaccel *vaccel = mdev_get_drvdata(mdev);
    int ret = -EINVAL;

    mutex_lock(&vaccel->ops_lock);
    ret = vaccel->ops->close(mdev);
    if (ret) {
        vaccel_err(vaccel, "vaccel_close return false");
    }
    mutex_unlock(&vaccel->ops_lock);
}

static inline void vaccel_write_cfg_bar(struct vaccel *vaccel, u32 offset,
                u32 val, bool low)
{
    u32 *pval;

    offset = rounddown(offset, 4);
    pval = (u32 *)(vaccel->vconfig + offset);

    if (low) {
        *pval = (val & GENMASK(31, 4)) | (*pval & GENMASK(3, 0));
        vaccel_info(vaccel, "vaccel: write value %x\n", *pval);
    }
    else {
        *pval = val;
    }
}

static void handle_pci_cfg_write(struct vaccel *vaccel, u32 offset,
                char *buf, u32 count)
{
    u32 new = *(u32 *)(buf);
    u64 size;
    bool lo = IS_ALIGNED(offset, 8);
    
    switch (offset) {
    case 0x04: /* device control */
    case 0x06: /* device status */
        /* do nothing */
        break;
    case 0x3c: /* interrupt line */
        vaccel->vconfig[0x3c] = buf[0];
        break;
    case 0x3d:
        /*
         * Interrupt Pin is hardwired to INTA.
         * This field is write protected by hardware
         */
        break;
    case 0x10: /* BAR0 */
    case 0x14: /* BAR1: upper 32 bits of BAR0 */
        if (new == 0xffffffff) {
            size = ~(OPTIMUS_BAR_0_SIZE - 1);
            vaccel_write_cfg_bar(vaccel, offset, size >> (lo ? 0 : 32), lo);
        }
        else {
            vaccel_write_cfg_bar(vaccel, offset, new, lo);
        }
        break;
    case 0x18: /* BAR2 */
    case 0x1c: /* BAR3: upper 32 bits of BAR2 */
        if (new == 0xffffffff) {
            size = ~(OPTIMUS_BAR_2_SIZE - 1);
            vaccel_write_cfg_bar(vaccel, offset, size >> (lo ? 0 : 32), lo);
        }
        else {
            vaccel_write_cfg_bar(vaccel, offset, new, lo);
        }
        break;
    case 0x20: /* BAR4 */
    case 0x24: /* BAR5 */
        STORE_LE32(&vaccel->vconfig[offset], 0);
        break;
    default:
        //vaccel_info(vaccel, "cfg write @0x%x of %d bytes not handled\n",
        //            offset, count);
        break;
    }
}

static void handle_bar_write(unsigned int index, struct vaccel *vaccel,
                loff_t offset, char *buf, u32 count)
{
    int ret;
    u64 data64 = *(u64*)buf;

    if (!vaccel || !vaccel->ops) {
        optimus_err("vaccel null ptr");
        return;
    }

    ret = vaccel->ops->handle_mmio_write(vaccel, index, offset, data64);
    if (ret) {
        vaccel_err(vaccel, "handle mmio write error");
    }
}

static void handle_bar_read(unsigned int index, struct vaccel *vaccel,
                loff_t offset, char *buf, u32 count)
{
    int ret;
    u64 data64;

    if (!vaccel || !vaccel->ops) {
        optimus_err("vaccel null ptr");
        return;
    }

    ret = vaccel->ops->handle_mmio_read(vaccel, index, offset, &data64);
    if (ret) {
        vaccel_err(vaccel, "handle mmio read error");
        return;
    }

    *(u64*)buf = data64;
}

static ssize_t vaccel_access(struct mdev_device *mdev, char *buf, size_t count,
			   loff_t pos, bool is_write)
{
    struct vaccel *vaccel;
	unsigned int index;
	loff_t offset;
	int ret = 0;

	if (!mdev || !buf)
		return -EINVAL;

    vaccel = mdev_get_drvdata(mdev);
	if (!vaccel) {
		vaccel_err(vaccel, "%s vaccel not found\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&vaccel->ops_lock);

	index = OPTIMUS_VFIO_PCI_OFFSET_TO_INDEX(pos);
	offset = pos & OPTIMUS_VFIO_PCI_OFFSET_MASK;
	switch (index) {
	case VFIO_PCI_CONFIG_REGION_INDEX:

		//vaccel_info(vaccel, "%s: PCI config space %s at offset 0x%llx\n",
		//	 __func__, is_write ? "write" : "read", offset);

		if (is_write) {
			dump_buffer_32(buf, count);
			handle_pci_cfg_write(vaccel, offset, buf, count);
		} else {
			memcpy(buf, (vaccel->vconfig + offset), count);
			dump_buffer_32(buf, count);
		}

		break;

	case VFIO_PCI_BAR0_REGION_INDEX ... VFIO_PCI_BAR5_REGION_INDEX:

        vaccel_info(vaccel, "%s: BAR %d %s at offset 0x%llx size %ld\n", __func__,
            index - VFIO_PCI_BAR0_REGION_INDEX, is_write ? "write" : "read",
            offset, count);

		if (is_write) {
			dump_buffer_64(buf, count);
			handle_bar_write(index, vaccel, offset, buf, count);
		} else {
			handle_bar_read(index, vaccel, offset, buf, count);
			dump_buffer_64(buf, count);
		}

		break;

	default:
		ret = -EINVAL;
		goto accessfailed;
	}

	ret = count;

accessfailed:
	mutex_unlock(&vaccel->ops_lock);

	return ret;
}

ssize_t vaccel_read(struct mdev_device *mdev, char __user *buf, size_t count,
            loff_t *ppos)
{
    unsigned int done = 0;
    int ret;
    //struct vaccel *vaccel = mdev_get_drvdata(mdev);

    //vaccel_info(vaccel, "%s: count %lu\n", __func__, count);

    while (count) {
       size_t filled;

       if (count >= 8 && (*ppos % 8 == 0)) {
           u64 val;

           ret = vaccel_access(mdev, (char *)&val, sizeof(val),
                    *ppos, false);
           if (ret <= 0)
               goto read_err;

           if (copy_to_user(buf, &val, sizeof(val)))
               goto read_err;

           filled = 8;
       }
       else if (count >= 4 && (*ppos % 4 == 0)) {
           u32 val;

           ret = vaccel_access(mdev, (char *)&val, sizeof(val),
                    *ppos, false);
           if (ret <= 0)
               goto read_err;

           if (copy_to_user(buf, &val, sizeof(val)))
               goto read_err;

           filled = 4;
       }
       else if (count >= 2 && (*ppos % 2 == 0)) {
           u16 val;

           ret = vaccel_access(mdev, (char *)&val, sizeof(val),
                        *ppos, false);
           if (ret <= 0)
               goto read_err;

           if (copy_to_user(buf, &val, sizeof(val)))
               goto read_err;

           filled = 2;
       }
       else {
           u8 val;

           ret = vaccel_access(mdev, (char *)&val, sizeof(val),
                   *ppos, false);
           if (ret <= 0)
               goto read_err;

           if (copy_to_user(buf, &val, sizeof(val)))
               goto read_err;

           filled = 1;
       }

       count -= filled;
       done += filled;
       *ppos += filled;
       buf += filled;
    }

    return done;

read_err:
    return -EFAULT;
}

ssize_t vaccel_write(struct mdev_device *mdev, const char __user *buf,
		   size_t count, loff_t *ppos)
{
	unsigned int done = 0;
	int ret;
    //struct vaccel *vaccel = mdev_get_drvdata(mdev);

    //vaccel_info(vaccel, "%s: count %ld", __func__, count);

	while (count) {
		size_t filled;

        if (count >= 8 && !(*ppos % 8)) {
            u64 val;

            if (copy_from_user(&val, buf, sizeof(val)))
                goto write_err;

            ret = vaccel_access(mdev, (char *)&val, sizeof(val),
                        *ppos, true);
            if (ret <= 0)
                goto write_err;

            filled = 8;
        }
        else if (count >= 4 && !(*ppos % 4)) {
			u32 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = vaccel_access(mdev, (char *)&val, sizeof(val),
					  *ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 4;
		}
        else if (count >= 2 && !(*ppos % 2)) {
			u16 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = vaccel_access(mdev, (char *)&val, sizeof(val),
					  *ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 2;
		} else {
			u8 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = vaccel_access(mdev, (char *)&val, sizeof(val),
					  *ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 1;
		}
		count -= filled;
		done += filled;
		*ppos += filled;
		buf += filled;
	}

	return done;
write_err:
	return -EFAULT;
}

static int vaccel_get_device_info(struct mdev_device *mdev,
                struct vfio_device_info *dev_info)
{
    dev_info->flags = VFIO_DEVICE_FLAGS_PCI;
    dev_info->flags |= VFIO_DEVICE_FLAGS_RESET;
    dev_info->num_regions = VFIO_PCI_NUM_REGIONS;
    dev_info->num_irqs = VFIO_PCI_NUM_IRQS;

    return 0;
}

static int vaccel_get_region_info(struct mdev_device *mdev,
                struct vfio_region_info *region_info,
                u16 *cap_type_id, void **cap_type)
{
    unsigned int size = 0;
    struct vaccel *vaccel;
    u32 region_index;

    if (!mdev)
        return -EINVAL;

    vaccel = mdev_get_drvdata(mdev);
    if (!vaccel)
        return -EINVAL;

    region_index = region_info->index;
    if (region_index >= VFIO_PCI_NUM_REGIONS)
        return -EINVAL;

    mutex_lock(&vaccel->ops_lock);

    switch (region_index) {
    case VFIO_PCI_CONFIG_REGION_INDEX:
        size = OPTIMUS_CONFIG_SPACE_SIZE;
        break;
    case VFIO_PCI_BAR0_REGION_INDEX:
        size = OPTIMUS_BAR_0_SIZE;
        break;
    case VFIO_PCI_BAR2_REGION_INDEX:
        size = OPTIMUS_BAR_2_SIZE;
        break;
    default:
        size = 0;
        break;
    }

    region_info->size = size;
    region_info->offset = OPTIMUS_VFIO_PCI_INDEX_TO_OFFSET(region_index);
    region_info->flags = VFIO_REGION_INFO_FLAG_READ |
        VFIO_REGION_INFO_FLAG_WRITE;

    mutex_unlock(&vaccel->ops_lock);
    return 0;
}

static int vaccel_get_irq_info(struct mdev_device *mdev, struct vfio_irq_info *irq_info)
{
	switch (irq_info->index) {
	case VFIO_PCI_INTX_IRQ_INDEX:
	case VFIO_PCI_MSI_IRQ_INDEX:
	case VFIO_PCI_REQ_IRQ_INDEX:
		break;

	default:
		return -EINVAL;
	}

	irq_info->flags = 0;
	irq_info->count = 0;

	return 0;
}

static int vaccel_reset(struct mdev_device *mdev)
{
    struct vaccel *vaccel;

    if (!mdev)
        return -EINVAL;

    vaccel = mdev_get_drvdata(mdev);
    if (!vaccel)
        return -EINVAL;

    vaccel_info(vaccel, "call: %s\n", __func__);

    vaccel_close(mdev);

    memset(vaccel->vconfig, 0, OPTIMUS_CONFIG_SPACE_SIZE);
    memset(vaccel->bar[VACCEL_BAR_0], 0, OPTIMUS_BAR_0_SIZE);
    memset(vaccel->bar[VACCEL_BAR_2], 0, OPTIMUS_BAR_2_SIZE);

    vaccel_create_config_space(vaccel);
    vaccel->gva_start = 0;
    vaccel->paging_notifier_gpa = 0;
    vaccel->kvm = NULL;

    vaccel_open(mdev);

    return 0;
}

static long vaccel_ioctl(struct mdev_device *mdev, unsigned int cmd,
			unsigned long arg)
{
	int ret = 0;
	unsigned long minsz;
    struct vaccel *vaccel;

	if (!mdev)
		return -EINVAL;

	vaccel = mdev_get_drvdata(mdev);
	if (!vaccel)
		return -ENODEV;

	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
	{
		struct vfio_device_info info;

		minsz = offsetofend(struct vfio_device_info, num_irqs);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		ret = vaccel_get_device_info(mdev, &info);
		if (ret)
			return ret;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}
	case VFIO_DEVICE_GET_REGION_INFO:
	{
		struct vfio_region_info info;
		u16 cap_type_id = 0;
		void *cap_type = NULL;

		minsz = offsetofend(struct vfio_region_info, offset);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		ret = vaccel_get_region_info(mdev, &info, &cap_type_id,
					   &cap_type);
		if (ret)
			return ret;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}

	case VFIO_DEVICE_GET_IRQ_INFO:
	{
		struct vfio_irq_info info;

		minsz = offsetofend(struct vfio_irq_info, count);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		ret = vaccel_get_irq_info(mdev, &info);
		if (ret)
			return ret;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}
	case VFIO_DEVICE_SET_IRQS:
	{
        /* current not supported */
        //vaccel_err(vaccel, "set irqs is not implemented!\n");
		return 0;
	}
	case VFIO_DEVICE_RESET:
		return vaccel_reset(mdev);
	}
	return -ENOTTY;
}

static const struct file_operations vd_fops = {
    .owner = THIS_MODULE
};

/* physical device hook */

static ssize_t
info_show(struct device *dev,
            struct device_attribute *attr, char *buf)
{
    struct optimus *optimus = device_to_optimus(dev);

    return sprintf(buf, "This is a physical FPGA with %d accelerators.\n",
                        optimus->npaccels);
}
static DEVICE_ATTR_RO(info);

static ssize_t
weights_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
    #ifdef SCHED_ENABLE_WEIGHT

    struct optimus *optimus = device_to_optimus(dev);

    return sprintf(buf, "%016llx\n", optimus->weights);

    #else
	return sprintf(buf, "0\n");
    #endif
}

static ssize_t
weights_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
    #ifdef SCHED_ENABLE_WEIGHT

    struct optimus *optimus = device_to_optimus(dev);
    int err;
    u64 data;
    struct vaccel *vacc, *vacc2;
    struct paccel *pacc;

    err = kstrtou64(buf, 0, &data);
    if (err)
        return err;

    optimus->weights = data;

    mutex_lock(&optimus->ops_lock);

    list_for_each_entry(vacc, &optimus->next, next) {
        if (vacc->seq_id >= 64/SCHED_WEIGHT_BIT)
            break;
        if (vacc->mode != VACCEL_TYPE_TIME_SLICING)
            continue;
        if ((data & SCHED_WEIGHT_MASK) == 0)
            continue;
        pacc = vacc->paccel;
        if (pacc->timeslc.policy != PACCEL_TS_POLICY_FAIR_NOTIFY)
            continue;
        vacc->timeslc.weight = data & SCHED_WEIGHT_MASK;
        data = data >> SCHED_WEIGHT_BIT;
        mutex_lock(&pacc->ops_lock);

        list_del(&vacc->timeslc.paccel_next);
        list_for_each_entry(vacc2, &pacc->timeslc.children, timeslc.paccel_next) {
            if (vacc2->timeslc.running_time * vacc->timeslc.weight >
                    vacc->timeslc.running_time * vacc2->timeslc.weight)
                break;
        }
        if (&vacc->timeslc.paccel_next == &pacc->timeslc.children) {
            list_add_tail(&vacc->timeslc.paccel_next,
                    &pacc->timeslc.children);
        }
        else {
            list_add(&vacc->timeslc.paccel_next,
                    vacc2->timeslc.paccel_next.prev);
        }

        mutex_unlock(&pacc->ops_lock);
    }

    mutex_unlock(&optimus->ops_lock);

    #endif

    return count;
}

static DEVICE_ATTR_RW(weights);

static struct attribute *optimus_attrs[] = {
    &dev_attr_info.attr,
    &dev_attr_weights.attr,
    NULL,
};

static const struct attribute_group optimus_dev_group = {
    .name = "optimus",
    .attrs = optimus_attrs,
};

const static struct attribute_group *optimus_dev_groups[] = {
    &optimus_dev_group,
    NULL,
};

/* virtual device hook */

static ssize_t
vaccel_hw_id_show(struct device *dev,
            struct device_attribute *attr, char *buf)
{
    struct mdev_device *mdev = mdev_from_dev(dev);
    struct vaccel *vaccel = mdev_get_drvdata(mdev);

    u64 guidh, guidl;
    u8 *mmio = vaccel->optimus->pafu_mmio;
    u32 off = vaccel->paccel->mmio_start;

    guidl = readq(&mmio[off+0x8]);
    guidh = readq(&mmio[off+0x10]);

    return sprintf(buf, "%llx%llx\n", guidh, guidl);
}
static DEVICE_ATTR_RO(vaccel_hw_id);

static ssize_t
vaccel_sw_id_show(struct device *dev,
            struct device_attribute *attr, char *buf)
{
    struct mdev_device *mdev = mdev_from_dev(dev);
    struct vaccel *vaccel = mdev_get_drvdata(mdev);

    return sprintf(buf, "phys_id %d\nseq_id %d\n",
                vaccel->paccel->accel_id,
                vaccel->seq_id);
}
static DEVICE_ATTR_RO(vaccel_sw_id);

static struct attribute *vaccel_dev_attrs[] = {
    &dev_attr_vaccel_hw_id.attr,
    &dev_attr_vaccel_sw_id.attr,
    NULL,
};

static const struct attribute_group vaccel_dev_group = {
    .name = "vaccel_info",
    .attrs = vaccel_dev_attrs,
};

const struct attribute_group *vaccel_dev_groups[] = {
    &vaccel_dev_group,
    NULL,
};

/* virtual device type hook
 * Here we have two kinds of virtual devices: 1) direct, 2) time-slicing
 * Each physical devices will have a type, and the attributes will depend
 * on the kind.
 */

static ssize_t
name_show(struct kobject *kobj,
            struct device *dev, char *buf)
{
    /* TODO */
    return sprintf(buf, "%s\n", kobj->name);
}
MDEV_TYPE_ATTR_RO(name);

static ssize_t
device_api_show(struct kobject *kobj, struct device *dev,
            char *buf)
{
    return sprintf(buf, "%s\n", VFIO_DEVICE_API_PCI_STRING);
}
MDEV_TYPE_ATTR_RO(device_api);

static ssize_t
available_instances_show(struct kobject *kobj,
            struct device *dev, char *buf)
{
    /* TODO */
    return sprintf(buf, "%d\n", 0);
}
MDEV_TYPE_ATTR_RO(available_instances);

static struct attribute *vaccel_direct_types_attrs[] = {
    &mdev_type_attr_name.attr,
    &mdev_type_attr_device_api.attr,
    NULL,
};

static struct attribute *vaccel_timeslicing_types_attrs[] = {
    &mdev_type_attr_name.attr,
    &mdev_type_attr_device_api.attr,
    &mdev_type_attr_available_instances.attr,
    NULL,
};

static struct mdev_parent_ops*
optimus_mdev_get_fops(u32 num_direct, u32 num_timeslicing)
{
    struct mdev_parent_ops *fops;
    struct attribute_group **type_groups;
    u32 ngroups = num_direct + num_timeslicing;
    int i;
    int ptr = 0;

    type_groups = kzalloc(sizeof(struct attribute_group *)*(ngroups+1), GFP_KERNEL);
    if (type_groups == NULL)
        return NULL;

    for (i=0; i<num_direct; i++) {
        struct attribute_group *type_group;
        char *name;

        type_group = kzalloc(sizeof(struct attribute_group), GFP_KERNEL);
        if (type_group == NULL) {
            kfree(type_groups);
            return NULL;
        }

        name = kzalloc(OPTIMUS_STRING_LEN, GFP_KERNEL);
        if (name == NULL) {
            kfree(type_groups);
            kfree(type_group);
            return NULL;
        }

        sprintf(name, "direct-%d", i);
        type_group->name = name;
        type_group->attrs = vaccel_direct_types_attrs;

        type_groups[ptr] = type_group;

        ptr++;
    }

    for (i=0; i<num_timeslicing; i++) {
        struct attribute_group *type_group;
        char *name;

        type_group = kzalloc(sizeof(struct attribute_group), GFP_KERNEL);
        if (type_group == NULL) {
            kfree(type_groups);
            return NULL;
        }

        name = kzalloc(OPTIMUS_STRING_LEN, GFP_KERNEL);
        if (name == NULL) {
            kfree(type_groups);
            kfree(type_group);
            return NULL;
        }

        sprintf(name, "time_slicing-%d", i);
        type_group->name = name;
        type_group->attrs = vaccel_timeslicing_types_attrs;

        type_groups[ptr] = type_group;

        ptr++;
    }

    type_groups[ptr] = NULL;

    fops = kzalloc(sizeof(*fops), GFP_KERNEL);
    if (fops == NULL) {
        for (i=0; i<ptr; i++) {
            kfree(type_groups[i]->name);
            kfree(type_groups[i]);
            kfree(type_groups);
        }
        return NULL;
    }

    fops->owner = THIS_MODULE;
    fops->dev_attr_groups = optimus_dev_groups;
    fops->mdev_attr_groups = vaccel_dev_groups;
    fops->supported_type_groups = type_groups;
    fops->create = vaccel_create;
    fops->remove = vaccel_remove;
    fops->open = vaccel_open;
    fops->release = vaccel_close;
    fops->read = vaccel_read;
    fops->write = vaccel_write;
    fops->ioctl = vaccel_ioctl;

    return fops;
}

static int optimus_probe(struct optimus *optimus, u32 *ndirect, u32 *nts)
{
    /* TODO: base on real hardware instead of mask */

    int i;
    int npaccels = readq(&optimus->pafu_mmio[0x20]);

    optimus->npaccels = npaccels;
    optimus->paccels =
            kzalloc(sizeof(struct paccel)*npaccels, GFP_KERNEL);

    *ndirect = 0;
    *nts = 0;

    for (i=0; i<npaccels; i++) {
        int tmp = 1 << i;
        if (optimus_timeslc_mask & tmp) {
            optimus->paccels[i].mode = VACCEL_TYPE_TIME_SLICING;
            optimus->paccels[i].mode_id = *nts;
            (*nts)+=1;
        }
        else {
            optimus->paccels[i].mode = VACCEL_TYPE_DIRECT;
            optimus->paccels[i].mode_id = *ndirect;
            (*ndirect)+=1;
        }

        optimus->paccels[i].accel_id = i;
        optimus->paccels[i].mmio_start = 0x1000*(i+1);
        optimus->paccels[i].mmio_size = 0x1000;

        optimus->paccels[i].optimus = optimus;

        if (optimus->paccels[i].mode == VACCEL_TYPE_DIRECT) {
            optimus->paccels[i].direct.occupied = false;
            optimus->paccels[i].ops = &paccel_direct_ops;
        }
        else {
            optimus->paccels[i].timeslc.total = 16;
            optimus->paccels[i].timeslc.occupied = 0;
            optimus->paccels[i].timeslc.policy =
                    PACCEL_TS_POLICY_FAIR_NOTIFY;
            INIT_LIST_HEAD(&optimus->paccels[i].timeslc.children);
            optimus->paccels[i].timeslc.curr = NULL;
            optimus->paccels[i].ops = &paccel_time_slicing_ops;
            if (optimus->paccels[i].timeslc.policy ==
                    PACCEL_TS_POLICY_FAIR_NOTIFY) {
                optimus->paccels[i].timeslc.state_sz =
                        readq(&optimus->pafu_mmio[0x1000*(i+1) + OPTIMUS_STATE_SZ]);
            }
        }

        mutex_init(&optimus->paccels[i].ops_lock);
    }

    return 0;
}

static int optimus_iommu_init(struct optimus *optimus,
            struct platform_device *pdev)
{

    optimus->domain = iommu_domain_alloc(&pci_bus_type);
    if (!optimus->domain) {
        optimus_info("failed to alloc iommu_domain\n");
        return -1;
    }

    optimus->iommu_map_flags = IOMMU_READ | IOMMU_WRITE;
    if (iommu_capable(&pci_bus_type, IOMMU_CAP_CACHE_COHERENCY)) {
        optimus->iommu_map_flags |= IOMMU_CACHE;
    }
    else {
        optimus_info("no iommu cache choerency support\n");
    }

    if (iommu_attach_device(optimus->domain,
                    pdev->dev.parent->parent)) {
        optimus_info("attach devcice failed\n");
        return -1;
    }
    else {
        optimus_info("attach device success\n");
    }

    iommu_set_fault_handler(optimus->domain,
            optimus_iommu_fault_handler, NULL);

    return 0;
}
static int optimus_vtp_init(struct optimus *optimus)
{
    uint64_t gpa;
    optimus->vtp_buffer = kzalloc(4096, GFP_KERNEL);
    gpa = virt_to_phys((void *)(optimus->vtp_buffer));
    if (optimus->vtp_buffer == NULL) {
        optimus_info("alloc vtp buffer failed\n");
        return -1;
    }
    optimus_info("vpt buffer pa: 0x%llx \n",gpa);
    //vtp_pt_init(&optimus->pt);
    hash_init(optimus->hash_pt);
    writeq(2, &optimus->pafu_mmio[VTP_BASE_MMIO + CCI_MPF_VTP_CSR_MODE]);
    writeq(gpa/CL(1), &optimus->pafu_mmio[VTP_BASE_MMIO + CCI_MPF_VTP_CSR_PAGE_TRANSLATION_BUF_PADDR]);
    writeq(1, &optimus->pafu_mmio[VTP_BASE_MMIO + CCI_MPF_VTP_CSR_MODE]);
    return 0;
}
static int optimus_iommu_uinit(struct optimus *optimus,
                struct platform_device *pdev)
{
    if (optimus->domain) {
        iommu_detach_device(optimus->domain, pdev->dev.parent->parent);
        iommu_domain_free(optimus->domain);
        optimus->domain = NULL;
    }
    return 0;
}
static int optimus_vtp_uinit(struct optimus *optimus)
{
    if (optimus->vtp_buffer) {
        kfree(optimus->vtp_buffer);
        optimus->vtp_buffer = NULL;
    }
    writeq(2, &optimus->pafu_mmio[VTP_BASE_MMIO+CCI_MPF_VTP_CSR_MODE]);
    //vtp_pt_uinit(&optimus->pt);

    return 0;
}
int fpga_register_afu_mdev_device(struct platform_device *pdev)
{
    int ret;
    char buf[OPTIMUS_STRING_LEN];
    struct mdev_parent_ops *fops;
    struct device *pafu = &pdev->dev;
    struct feature_platform_data *pdata = dev_get_platdata(pafu);
    struct feature_header *hdr =
            get_feature_ioaddr_by_index(pafu, PORT_FEATURE_ID_UAFU);
    struct feature_afu_header *afu_hdr =
            (struct feature_afu_header *)(hdr + 1);
    struct optimus *optimus;
    u32 ndirect, nts;
    u64 guidl, guidh;

    printk("optimus: registering, optimus_dbg=%d, tlb_opt_offset=%#llx\n", optimus_dbg, tlb_opt_offset);

    mutex_lock(&pdata->lock);
	if (pdata->disable_count) {
		mutex_unlock(&pdata->lock);
		return -EBUSY;
	}
	guidl = readq(&afu_hdr->guid.b[0]);
	guidh = readq(&afu_hdr->guid.b[8]);
	mutex_unlock(&pdata->lock);

	scnprintf(buf, PAGE_SIZE, "%016llx%016llx", guidh, guidl);
    optimus_info("%s: phy afu id %s\n", __func__, buf);

    if (guidh != OPTIMUS_GUID_HI ||
            guidl != OPTIMUS_GUID_LO) {
        optimus_info("not optimus hardware\n");
        return -EINVAL;
    }

    optimus = kzalloc(sizeof(struct optimus), GFP_KERNEL);
    optimus->pafu_device = pafu;
    optimus->pafu_mmio = (u8 *)hdr;
    mutex_init(&optimus->ops_lock);
    INIT_LIST_HEAD(&optimus->vaccel_list);
    atomic_set(&optimus->next_seq_id, 0);
    #ifdef SCHED_ENABLE_WEIGHT
    optimus->weights = 0;
    #endif
    
    optimus_probe(optimus, &ndirect, &nts);
#ifndef SOFTWARE_VTP
    optimus_iommu_init(optimus, pdev);
    optimus->vtp = NULL;
#else  
    mutex_init(&optimus->pt_lock);
    optimus_vtp_init(optimus);
    optimus->vtp= kthread_run(optimus_vtp_srv, optimus, "optimus-vtp");
#endif 

    mutex_lock(&optimus_list_lock);
    list_add(&optimus->next, &optimus_list);
    mutex_unlock(&optimus_list_lock);

    /* Start scheduler kthread */
    if (nts != 0) {
        optimus->scheduler =
            kthread_run(kthread_watch_time, optimus, "optimus-sched");
        optimus->user_check_signal = 0;
    }
    else
        optimus->scheduler = NULL;


    fops = optimus_mdev_get_fops(ndirect, nts);
    if (fops == NULL)
        return -1;
    ret = mdev_register_device(&pdev->dev, fops);
    if (ret != 0)
        return ret;

    return ret;
}

void fpga_unregister_afu_mdev_device(struct platform_device *pdev)
{
    struct optimus *d, *tmp_d;
    struct optimus *optimus = pdev_to_optimus(pdev);

    if (!optimus)
        return;

    if (optimus->scheduler) {
        kthread_stop(optimus->scheduler);
        optimus->scheduler = NULL;
    }
#ifndef SOFTWARE_VTP
    optimus_iommu_uinit(optimus, pdev);
#else 
    if (optimus->vtp) {
        kthread_stop(optimus->vtp);
        optimus->vtp = NULL;
    }
    optimus_vtp_uinit(optimus);
#endif 
	mdev_unregister_device(&pdev->dev);

    mutex_lock(&optimus_list_lock);
    list_for_each_entry_safe(d, tmp_d, &optimus_list, next) {
        if (optimus == d) {
            list_del(&optimus->next);
            break;
        }
    }
    mutex_unlock(&optimus_list_lock);
}
