/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box Probing Tools distribution.
 *
 *          Common library for C-based tools.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2021 RichardG.
 *
 */
#ifdef __POSIX_UEFI__
#    include <uefi.h>
#else
#    include <stdio.h>
#endif
#include "clib_pci.h"
#include "clib_sys.h"

uint8_t pci_mechanism = 0, pci_device_count = 0;
#ifdef PCI_LIB_VERSION
static struct pci_access *pacc;
static struct pci_dev    *pdev;
#endif

/* Configuration functions. */
uint32_t
pci_cf8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    /* Generate a PCI port CF8h dword. */
    multi_t ret;
    ret.u8[3] = 0x80;
    ret.u8[2] = bus;
    ret.u8[1] = dev << 3;
    ret.u8[1] |= func & 7;
    ret.u8[0] = reg & 0xfc;
    return ret.u32;
}

uint16_t
pci_get_io_bar(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint16_t size, const char *name)
{
    uint16_t ret, temp;

    printf("%s I/O BAR is ", name);

    /* Read BAR register. */
    ret = pci_readw(bus, dev, func, reg);
    if (!(ret & 0x0001) || (ret == 0xffff)) {
        temp = pci_readw(bus, dev, func, reg | 0x2);
        printf("invalid! (%04X%04X)", temp, ret);
        ret = 0;
    } else {
        /* Assign BAR if unassigned. */
        ret &= ~(size - 1);
        if (ret) {
            printf("assigned to %04X", ret);
        } else {
            printf("unassigned ");

            /* Find I/O range for the BAR. */
            ret = io_find_range(size);
            if (ret) {
                /* Assign and check value. */
                pci_writew(bus, dev, func, reg, ret | 0x0001);
                temp = pci_readw(bus, dev, func, reg);
                if ((temp & ~(size - 1)) == ret) {
                    printf("(assigning to %04X)", ret);
                } else {
                    ret = pci_readw(bus, dev, func, reg | 0x2);
                    printf("and not responding! (%04X%04X)", ret, temp);
                    ret = 0;
                }
            } else {
                printf("and no suitable range was found!");
            }
        }
    }

    printf("\n");
    return ret;
}

#ifdef IS_32BIT
uint32_t
pci_get_mem_bar(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t size, const char *name)
{
    uint32_t ret;

    printf("%s memory BAR is ", name);

    /* Read BAR register. */
    ret = pci_readl(bus, dev, func, reg);
    if ((ret & 0x00000001) || (ret == 0xffffffff)) {
        printf("invalid! (%08X)", ret);
        ret = 0;
    } else {
        /* Don't even try to find a valid memory range if the BAR is unassigned. */
        ret &= ~(size - 1);
        if (ret)
            printf("assigned to %08X", ret);
        else
            printf("unassigned!");
    }

    printf("\n");
    return ret;
}
#endif

#ifdef PCI_LIB_VERSION
static void
dummy_print(char *msg, ...)
{
}
#endif

int
pci_init()
{
#ifdef PCI_LIB_VERSION
    if (iopl(3)) {
        perror("iopl");
pci_init_fail:
        pci_mechanism = 0;
        return pci_mechanism;
    }

    pacc = pci_alloc();
    if (!pacc) {
        printf("Failed to allocate pci_access structure.\n");
        goto pci_init_fail;
    }

    libpci_init(pacc);
    pacc->error = pacc->warning = pacc->debug = dummy_print;

    pci_mechanism    = 1;
    pci_device_count = 32;
#else
    multi_t cf8;
    cf8.u32 = 0x80001234;

    /* Determine the supported PCI configuration mechanism. */
    cli();
    outl(0xcf8, cf8.u32);
    cf8.u32 = inl(0xcf8);
    if (cf8.u32 == 0x80001234) {
        pci_mechanism    = 1;
        pci_device_count = 32;
    } else {
        outb(0xcf8, 0x00);
        outb(0xcfa, 0x00);
        if ((inb(0xcf8) == 0x00) && (inb(0xcfa) == 0x00)) {
            pci_mechanism    = 2;
            pci_device_count = 16;
        }
    }
    sti();
    if (pci_mechanism == 0)
        printf("Failed to probe PCI configuration mechanism (%04X%04X). Is this a PCI system?\n", cf8.u16[1], cf8.u16[0]);
#endif

    return pci_mechanism;
}

uint8_t
pci_readb(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
#ifdef PCI_LIB_VERSION
    pdev = pci_get_dev(pacc, 0, bus, dev, func);
    return pdev ? pci_read_byte(pdev, reg) : 0xff;
#else
    uint8_t  ret;
    uint16_t data_port;
    uint32_t cf8;

    switch (pci_mechanism) {
        case 1:
            data_port = 0xcfc | (reg & 0x03);
            cf8       = pci_cf8(bus, dev, func, reg);
            cli();
            outl(0xcf8, cf8);
            ret = inb(data_port);
            sti();
            break;

        case 2:
            cf8 = pci_readl(bus, dev, func, reg);
            ret = cf8 >> ((reg & 0x03) << 3);
            break;

        default:
            ret = 0xff;
            break;
    }

    return ret;
#endif
}

uint16_t
pci_readw(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
#ifdef PCI_LIB_VERSION
    pdev = pci_get_dev(pacc, 0, bus, dev, func);
    return pdev ? pci_read_word(pdev, reg) : 0xffff;
#else
    uint16_t ret, data_port;
    uint32_t cf8;

    switch (pci_mechanism) {
        case 1:
            data_port = 0xcfc | (reg & 0x02);
            cf8       = pci_cf8(bus, dev, func, reg);
            cli();
            outl(0xcf8, cf8);
            ret = inw(data_port);
            sti();
            break;

        case 2:
            cf8 = pci_readl(bus, dev, func, reg);
            ret = cf8 >> ((reg & 0x02) << 3);
            break;

        default:
            ret = 0xffff;
            break;
    }

    return ret;
#endif
}

uint32_t
pci_readl(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
#ifdef PCI_LIB_VERSION
    pdev = pci_get_dev(pacc, 0, bus, dev, func);
    return pdev ? pci_read_long(pdev, reg) : 0xffffffff;
#else
    uint16_t data_port;
    uint32_t ret, cf8;

    switch (pci_mechanism) {
        case 1:
            cf8 = pci_cf8(bus, dev, func, reg);
            cli();
            outl(0xcf8, cf8);
            ret = inl(0xcfc);
            sti();
            break;

        case 2:
            func      = 0x80 | (func << 1);
            data_port = 0xc000 | (dev << 8) | (reg & 0xfc);
            cli();
            outb(0xcf8, func);
            outb(0xcfa, bus);
            ret = inl(data_port);
            sti();
            break;

        default:
            ret = 0xffffffff;
            break;
    }

    return ret;
#endif
}

void
pci_writeb(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint8_t val)
{
#ifdef PCI_LIB_VERSION
    pdev = pci_get_dev(pacc, 0, bus, dev, func);
    if (pdev)
        pci_write_byte(pdev, reg, val);
#else
    uint8_t  shift;
    uint16_t data_port;
    uint32_t cf8;

    switch (pci_mechanism) {
        case 1:
            data_port = 0xcfc | (reg & 0x03);
            cf8       = pci_cf8(bus, dev, func, reg);
            cli();
            outl(0xcf8, cf8);
            outb(data_port, val);
            sti();
            break;

        case 2:
            cf8   = pci_readl(bus, dev, func, reg);
            shift = (reg & 0x03) << 3;
            cf8 &= ~(0x000000ff << shift);
            cf8 |= val << shift;
            pci_writel(bus, dev, func, reg, cf8);
            break;
    }
#endif
}

void
pci_writew(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint16_t val)
{
#ifdef PCI_LIB_VERSION
    pdev = pci_get_dev(pacc, 0, bus, dev, func);
    if (pdev)
        pci_write_word(pdev, reg, val);
#else
    uint8_t  shift;
    uint16_t data_port;
    uint32_t cf8;

    switch (pci_mechanism) {
        case 1:
            data_port = 0xcfc | (reg & 0x02);
            cf8       = pci_cf8(bus, dev, func, reg);
            cli();
            outl(0xcf8, cf8);
            outw(data_port, val);
            sti();
            break;

        case 2:
            cf8   = pci_readl(bus, dev, func, reg);
            shift = (reg & 0x02) << 3;
            cf8 &= ~(0x0000ffff << shift);
            cf8 |= val << shift;
            pci_writel(bus, dev, func, reg, cf8);
            break;
    }
#endif
}

void
pci_writel(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t val)
{
#ifdef PCI_LIB_VERSION
    pdev = pci_get_dev(pacc, 0, bus, dev, func);
    if (pdev)
        pci_write_long(pdev, reg, val);
#else
    uint16_t data_port;
    uint32_t cf8;

    switch (pci_mechanism) {
        case 1:
            cf8 = pci_cf8(bus, dev, func, reg);
            cli();
            outl(0xcf8, cf8);
            outl(0xcfc, val);
            sti();
            break;

        case 2:
            func      = 0x80 | (func << 1);
            data_port = 0xc000 | (dev << 8) | (reg & 0xfc);
            cli();
            outb(0xcf8, func);
            outb(0xcfa, bus);
            outl(data_port, val);
            sti();
            break;
    }
#endif
}

void
pci_scan_bus(uint8_t bus,
             void (*callback)(uint8_t bus, uint8_t dev, uint8_t func,
                              uint16_t ven_id, uint16_t dev_id))
{
#ifdef PCI_LIB_VERSION
    libpci_scan_bus(pacc);
    for (pdev = pacc->devices; pdev; pdev = pdev->next) {
        if (pdev->domain || (pdev->bus != bus))
            continue;
        pci_fill_info(pdev, PCI_FILL_IDENT);
        callback(pdev->bus, pdev->dev, pdev->func, pdev->vendor_id, pdev->device_id);
    }
#else
    uint8_t dev, func, header_type;
    multi_t dev_id;

    /* Iterate through devices. */
    for (dev = 0; dev < pci_device_count; dev++) {
        /* Iterate through functions. */
        for (func = 0; func < 8; func++) {
            /* Read vendor/device ID. */
#    ifdef DEBUG
            if ((bus < DEBUG) && (dev <= bus) && (func == 0)) {
                dev_id.u16[0] = rand();
                dev_id.u16[1] = rand();
            } else {
                dev_id.u32 = 0xffffffff;
            }
#    else
            dev_id.u32  = pci_readl(bus, dev, func, 0x00);
#    endif

            /* Callback if this is a valid ID. */
            if (dev_id.u32 && (dev_id.u32 != 0xffffffff)) {
                callback(bus, dev, func, dev_id.u16[0], dev_id.u16[1]);
            } else {
                /* Stop or move on to the next function if there's nothing here. */
                if (func)
                    continue;
                else
                    break;
            }

            /* Read header type. */
#    ifdef DEBUG
            header_type = (bus < (DEBUG - 1)) ? 0x01 : 0x00;
#    else
            header_type = pci_readb(bus, dev, func, 0x0e);
#    endif

            /* If this is a bridge, mark that we should probe its bus. */
            if (header_type & 0x7f) {
                /* Scan the secondary bus. */
#    ifdef DEBUG
                pci_scan_bus(bus + 1, callback);
#    else
                pci_scan_bus(pci_readb(bus, dev, func, 0x19), callback);
#    endif
            }

            /* If we're at the first function, stop if this is not a multi-function device. */
            if ((func == 0) && !(header_type & 0x80))
                break;
        }
    }
#endif
}
