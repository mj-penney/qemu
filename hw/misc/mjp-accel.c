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

#include "../accelerator/include/hw.h"

#define TYPE_MJP "mjp-accel"
typedef struct Mjp Mjp;
DECLARE_INSTANCE_CHECKER(Mjp, MJP,
                         TYPE_MJP)

struct Mjp {
    PCIDevice pdev;
    MemoryRegion mmio;

    /* device registers */
    uint32_t dma_addr_lo;
    uint32_t dma_addr_hi;
};

static uint64_t mjp_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Mjp *mjp = opaque;
    uint64_t val = 0ULL;

    if (size != 4) {
        /* only support size 4 for now, error */
    }

    switch (addr) {
        case MJP_DMA_ADDR_LO:
            val = mjp->dma_addr_lo;
            break;
        case MJP_DMA_ADDR_HI:
            val = mjp->dma_addr_hi;
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
    Mjp *mjp = opaque;

    if (size != 4) {
        /* only support size 4 for now, error */
    }

    switch (addr) {
        case MJP_DMA_ADDR_LO:
            mjp->dma_addr_lo = val;
            break;
        case MJP_DMA_ADDR_HI:
            mjp->dma_addr_hi = val;
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

static void mjp_realize(PCIDevice *pdev, Error **errp)
{
    Mjp *mjp = MJP(pdev);

    memory_region_init_io(&mjp->mmio,
                          OBJECT(mjp),
                          &mjp_mmio_ops,
                          mjp,
                          "mjp-mmio",
                          MJP_BAR0_SIZE);

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

static void mjp_uninit(PCIDevice *pdev)
{
    Mjp *mjp = MJP(pdev);

    /* only one vector to clean up */
    msix_vector_unuse(pdev, 0);
    msix_uninit(pdev, &mjp->mmio, &mjp->mmio);
}

static void mjp_instance_init(Object *obj)
{
    //Mjp *mjp = MJP(obj);
    (void)obj;
}

static void mjp_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = mjp_realize;
    k->exit = mjp_uninit;
    k->vendor_id = MJP_VENDOR_ID;
    k->device_id = MJP_DEVICE_ID;
    k->revision = 0x10;
    k->class_id = PCI_CLASS_OTHERS;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo mjp_types[] = {
    {
        .name          = TYPE_MJP,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Mjp),
        .instance_init = mjp_instance_init,
        .class_init    = mjp_class_init,
        .interfaces    = (const InterfaceInfo[]) {
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            { },
        },
    }
};

DEFINE_TYPES(mjp_types)
