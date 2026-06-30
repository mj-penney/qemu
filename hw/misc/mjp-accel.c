#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define TYPE_MJP_ACCEL "mjp-accel"
typedef struct MjpAccel MjpAccel;
DECLARE_INSTANCE_CHECKER(MjpAccel, MJP_ACCEL,
                         TYPE_MJP_ACCEL)

/* vendor & device ids */

#define MJP_ACCEL_VENDOR_ID 0x1234
#define MJP_ACCEL_DEVICE_ID 0x11e8

/* memory region offsets */

#define MJP_ACCEL_BAR0_SIZE    0x10000

#define MJP_REGS_BAR_IDX       0
#define MJP_REGS_OFFSET        0x00000
#define MJP_REGS_SIZE          0x01000

#define MJP_MSIX_TABLE_BAR_IDX 0
#define MJP_MSIX_TABLE_OFFSET  0x01000
#define MJP_MSIX_TABLE_SIZE    0x01000

#define MJP_MSIX_PBA_BAR_IDX   0
#define MJP_MSIX_PBA_OFFSET    0x02000
#define MJP_MSIX_PBA_SIZE      0x01000

#define MJP_MSIX_VEC_NUM 1
#define MJP_MSIX_CAP_POS 0 /* don't manually set msix cap_pos */

/* individual register offsets */

#define MJP_R0_OFFSET 0x00
#define MJP_R1_OFFSET 0x04

struct MjpAccel {
    PCIDevice pdev;
    MemoryRegion mmio;

    /* device registers */
    uint32_t r0;
    uint32_t r1;
};

static uint64_t mjp_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    MjpAccel *mjp = opaque;
    uint64_t val = 0ULL;

    if (size != 4) {
        /* only support size 4 for now, error */
    }

    switch (addr) {
        case MJP_R0_OFFSET:
            val = mjp->r0;
            break;
        case MJP_R1_OFFSET:
            val = mjp->r1;
            break;
        default:
            val = 0;
            break;
    }

    return val;
}

static void mjp_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                                                                unsigned size)
{
    MjpAccel *mjp = opaque;

    if (size != 4) {
        /* only support size 4 for now, error */
    }

    switch (addr) {
        case MJP_R0_OFFSET:
            mjp->r0 = val;
            break;
        case MJP_R1_OFFSET:
            mjp->r1 = val;
            break;
        default:
            break;
    }
}

static const MemoryRegionOps mjp_mmio_ops = {
    .read = mjp_mmio_read,
    .write = mjp_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void mjp_accel_realize(PCIDevice *pdev, Error **errp)
{
    MjpAccel *mjp = MJP_ACCEL(pdev);

    memory_region_init_io(&mjp->mmio,
                          OBJECT(mjp),
                          &mjp_mmio_ops,
                          mjp,
                          "mjp-accel-mmio",
                          MJP_ACCEL_BAR0_SIZE);

    pci_register_bar(pdev,
                     MJP_REGS_BAR_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY,
                     &mjp->mmio);

    int res = msix_init(pdev, MJP_MSIX_VEC_NUM,
                        &mjp->mmio,
                        MJP_MSIX_TABLE_BAR_IDX, MJP_MSIX_TABLE_OFFSET,
                        &mjp->mmio,
                        MJP_MSIX_PBA_BAR_IDX, MJP_MSIX_PBA_OFFSET,
                        MJP_MSIX_CAP_POS,
                        errp);

    if (res < 0) {
        /* handle error */
    } else {
        /* only one vector for now */
        msix_vector_use(pdev, 0);
    }
}

static void mjp_accel_uninit(PCIDevice *pdev)
{
    MjpAccel *mjp = MJP_ACCEL(pdev);

    /* only one vector to clean up */
    msix_vector_unuse(pdev, 0);
    msix_uninit(pdev, &mjp->mmio, &mjp->mmio);
}

static void mjp_accel_instance_init(Object *obj)
{
    //MjpAccel *mjp = MJP_ACCEL(obj);
    (void)obj;
}

static void mjp_accel_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = mjp_accel_realize;
    k->exit = mjp_accel_uninit;
    k->vendor_id = MJP_ACCEL_VENDOR_ID;
    k->device_id = MJP_ACCEL_DEVICE_ID;
    k->revision = 0x10;
    k->class_id = PCI_CLASS_OTHERS;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo mjp_accel_types[] = {
    {
        .name          = TYPE_MJP_ACCEL,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(MjpAccel),
        .instance_init = mjp_accel_instance_init,
        .class_init    = mjp_accel_class_init,
        .interfaces    = (const InterfaceInfo[]) {
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            { },
        },
    }
};

DEFINE_TYPES(mjp_accel_types)
