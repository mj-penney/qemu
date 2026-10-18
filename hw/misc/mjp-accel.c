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
DECLARE_INSTANCE_CHECKER(Mjp, MJP, TYPE_MJP)

typedef struct MjpRing {
    uint32_t dma_addr_lo;
    uint32_t dma_addr_hi;
    uint32_t prod; // bookkepping: producer index
    uint32_t cons; // bookkepping: consumer index
} MjpRing;

struct Mjp {
    PCIDevice pdev;
    MemoryRegion mmio;

    MjpRing sub_ring;
    MjpRing cpl_ring;

    /* device registers */

    mjp_device_status device_status;
};

static uint64_t mjp_build_dma_addr(uint32_t lo, uint32_t hi)
{
    return ((uint64_t)hi << 32) | lo;
}

static void mjp_ring_reset(MjpRing *ring)
{
    memset(ring, 0, sizeof(MjpRing));
}

static void mjp_device_reset(Mjp *mjp)
{
    MjpRing *sub_ring = &mjp->sub_ring;
    MjpRing *cpl_ring = &mjp->cpl_ring;

    mjp->device_status = 0;

    mjp_ring_reset(sub_ring);
    mjp_ring_reset(cpl_ring);

    mjp->device_status = MJP_DEVICE_STATUS_RESET;
}

static void mjp_device_enable(Mjp *mjp)
{
    MjpRing *ring = &mjp->sub_ring;
    uint64_t addr = mjp_build_dma_addr(ring->dma_addr_lo,
                                       ring->dma_addr_hi);
    if (!addr) {
        mjp->device_status = MJP_DEVICE_STATUS_ERROR;
        return;
    }

    ring = &mjp->cpl_ring;
    addr = mjp_build_dma_addr(ring->dma_addr_lo,
                              ring->dma_addr_hi);
    if (!addr) {
        mjp->device_status = MJP_DEVICE_STATUS_ERROR;
        return;
    }

    mjp->device_status = MJP_DEVICE_STATUS_ENABLED;
}

static void mjp_control_handle(Mjp *mjp, uint64_t command)
{
    switch (command) {
        case MJP_CONTROL_DEVICE_RESET:
            mjp_device_reset(mjp);
            break;
        case MJP_CONTROL_DEVICE_ENABLE:
            mjp_device_enable(mjp);
        default:
            break;
    }
}

static int mjp_queue_push(Mjp *mjp, struct mjp_cpl_desc *desc)
{
    MjpRing *ring = &mjp->cpl_ring;

    uint64_t addr = mjp_build_dma_addr(ring->dma_addr_lo,
                                       ring->dma_addr_hi);

    uint32_t desc_idx = ring->prod % MJP_CPL_RING_SIZE;
    addr = addr + (desc_idx * sizeof(struct mjp_cpl_desc));

    MemTxResult res = dma_memory_write(
        pci_get_address_space(&mjp->pdev),
        addr,
        desc,
        sizeof(struct mjp_cpl_desc),
        MEMTXATTRS_UNSPECIFIED
    );

    if (res != MEMTX_OK) {
        /* Handle DMA failure */
        printf("Error in dma_memory_write(): %d\n", res);
        return res;
    }

    printf("Completed job: job_id: %lu\n", desc->job_id);

    // device's job to update producer index on the completion ring
    ring->prod++;

    return 0;
}

static int mjp_queue_pop(Mjp *mjp, struct mjp_sub_desc *desc)
{
    MjpRing *ring = &mjp->sub_ring;

    uint64_t addr = mjp_build_dma_addr(ring->dma_addr_lo,
                                       ring->dma_addr_hi);

    uint32_t desc_idx = ring->cons % MJP_SUB_RING_SIZE;
    addr = addr + (desc_idx * sizeof(struct mjp_sub_desc));

    MemTxResult res = dma_memory_read(
        pci_get_address_space(&mjp->pdev),
        addr,
        desc,
        sizeof(struct mjp_sub_desc),
        MEMTXATTRS_UNSPECIFIED
    );

    if (res != MEMTX_OK) {
        /* Handle DMA failure */
        printf("Error in dma_memory_read(): %d\n", res);
        return res;
    }

    printf("Received job: job_id: %lu\n", desc->job_id);
    
    // device's job to update consumer index on the submission ring
    ring->cons++;

    return 0;
}

static void mjp_queue_test(Mjp *mjp)
{
    struct mjp_sub_desc sub_desc;
    struct mjp_cpl_desc cpl_desc;

    if (mjp_queue_pop(mjp, &sub_desc)) {
        return; // error
    }

    cpl_desc.job_id = sub_desc.job_id;

    if (mjp_queue_push(mjp, &cpl_desc)) {
        return; // error
    }

    msix_notify(&mjp->pdev, 0);
}

static uint64_t mjp_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Mjp *mjp = opaque;
    uint64_t val = 0ULL;

    if (size != 4) {
        /* only support size 4 for now, error */
    }

    switch (addr) {
        case MJP_REG_DEVICE_STATUS:
            val = mjp->device_status;
            break;

        /* submission ring */
        case MJP_Q_REG(0, MJP_REG_SUB_BASE_LO):
            val = mjp->sub_ring.dma_addr_lo;
            break;
        case MJP_Q_REG(0, MJP_REG_SUB_BASE_HI):
            val = mjp->sub_ring.dma_addr_hi;
            break;
        case MJP_Q_REG(0, MJP_REG_SUB_PROD):
            val = mjp->sub_ring.prod;
            break;
        case MJP_Q_REG(0, MJP_REG_SUB_CONS):
            val = mjp->sub_ring.cons;
            break;

        /* completion ring */
        case MJP_Q_REG(0, MJP_REG_CPL_BASE_LO):
            val = mjp->cpl_ring.dma_addr_lo;
            break;
        case MJP_Q_REG(0, MJP_REG_CPL_BASE_HI):
            val = mjp->cpl_ring.dma_addr_hi;
            break;
        case MJP_Q_REG(0, MJP_REG_CPL_PROD):
            val = mjp->cpl_ring.prod;
            break;
        case MJP_Q_REG(0, MJP_REG_CPL_CONS):
            val = mjp->cpl_ring.cons;
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
        case MJP_REG_CONTROL:
            mjp_control_handle(mjp, val);
            break;

        /* submission ring */
        case MJP_Q_REG(0, MJP_REG_SUB_BASE_LO):
            mjp->sub_ring.dma_addr_lo = val;
            break;
        case MJP_Q_REG(0, MJP_REG_SUB_BASE_HI):
            mjp->sub_ring.dma_addr_hi = val;
            break;
        case MJP_Q_REG(0, MJP_REG_SUB_PROD):
            mjp->sub_ring.prod = val;
            break;
        case MJP_Q_REG(0, MJP_REG_SUB_CONS):
            mjp->sub_ring.cons = val;
            break;
        case MJP_Q_REG(0, MJP_REG_SUB_DOORBELL):
            mjp_queue_test(mjp);
            break;

        /* completion ring */
        case MJP_Q_REG(0, MJP_REG_CPL_BASE_LO):
            mjp->cpl_ring.dma_addr_lo = val;
            break;
        case MJP_Q_REG(0, MJP_REG_CPL_BASE_HI):
            mjp->cpl_ring.dma_addr_hi = val;
            break;
        case MJP_Q_REG(0, MJP_REG_CPL_PROD):
            mjp->cpl_ring.prod = val;
            break;
        case MJP_Q_REG(0, MJP_REG_CPL_CONS):
            mjp->cpl_ring.cons = val;
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
