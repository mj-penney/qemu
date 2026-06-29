#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define TYPE_MJP_ACCEL "mjp-accel"
typedef struct MjpAccel MjpAccel;
DECLARE_INSTANCE_CHECKER(MjpAccel, MJP_ACCEL,
                         TYPE_MJP_ACCEL)

struct MjpAccel {
    PCIDevice pdev;
    MemoryRegion mmio;

    /* device registers */
    uint32_t reg0;
    uint32_t reg1;
};

static uint64_t mjp_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;

    return 0;
}

static void mjp_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                                                                unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

static const MemoryRegionOps mjp_mmio_ops = {
    .read = mjp_mmio_read,
    .write = mjp_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void mjp_accel_realize(PCIDevice *pdev, Error **errp)
{
    MjpAccel *mjp = MJP_ACCEL(pdev);
    (void)errp;

    memory_region_init_io(&mjp->mmio, OBJECT(mjp), &mjp_mmio_ops, mjp,
            "mjp-accel-mmio", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &mjp->mmio);
}

static void mjp_accel_uninit(PCIDevice *pdev)
{
    //MjpAccel *mjp = MJP_ACCEL(pdev);
    (void)pdev;
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
    k->vendor_id = PCI_VENDOR_ID_QEMU;
    k->device_id = 0x11e8;
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
