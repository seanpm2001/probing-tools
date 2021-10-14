/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box Probing Tools distribution.
 *
 *		Common library for C-based tools.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2021 RichardG.
 *
 */
#ifdef __POSIX_UEFI__
# include <uefi.h>
#else
# include <inttypes.h>
# include <stdint.h>
# include <stdio.h>
# include <string.h>
# ifdef __WATCOMC__
#  include <dos.h>
#  include <graph.h>
# endif
#endif
#include "clib.h"


#ifdef __WATCOMC__
static union REGPACK rp; /* things break if this is not a global variable... */
#endif


/* String functions. */
int
parse_hex_u8(char *val, uint8_t *dest)
{
    uint32_t dest32;
    int ret = parse_hex_u32(val, &dest32);
    *dest = dest32;
    return ret;
}


int
parse_hex_u16(char *val, uint16_t *dest)
{
    uint32_t dest32;
    int ret = parse_hex_u32(val, &dest32);
    *dest = dest32;
    return ret;
}


int
parse_hex_u32(char *val, uint32_t *dest)
{
    int i, len = strlen(val);
    uint8_t digit;

    *dest = 0;
    for (i = 0; i < len; i++) {
    	if ((val[i] >= 0x30) && (val[i] <= 0x39))
    		digit = val[i] - 0x30;
    	else if ((val[i] >= 0x41) && (val[i] <= 0x46))
    		digit = val[i] - 0x37;
    	else if ((val[i] >= 0x61) && (val[i] <= 0x66))
    		digit = val[i] - 0x57;
    	else
    		return 0;
    	*dest = (*dest << 4) | digit;
    }

    return 1;
}


/* Comparator functions. */
int
comp_ui8(const void *elem1, const void *elem2)
{
    uint8_t a = *((uint8_t *) elem1);
    uint8_t b = *((uint8_t *) elem2);
    return ((a < b) ? -1 : ((a > b) ? 1 : 0));
}


/* System functions. */
#ifdef __WATCOMC__
/* Defined in header. */
#elif defined(__GNUC__)
void
cli()
{
    __asm__("cli");
}


void
sti()
{
    __asm__("sti");
}
#else
void
cli()
{
}


void
sti()
{
}
#endif


/* Terminal functions. */
#ifdef __WATCOMC__
int
term_get_size_x()
{
    struct videoconfig vc;
    _getvideoconfig(&vc);
    return vc.numtextcols;
}


int
term_get_size_y()
{
    struct videoconfig vc;
    _getvideoconfig(&vc);
    return vc.numtextrows;
}


int
term_get_cursor_pos(uint8_t *x, uint8_t *y)
{
    rp.h.ah = 0x03;
    rp.h.bh = 0x00;
    intr(0x10, &rp);
    *x = rp.h.dl;
    *y = rp.h.dh;
    return 1;
}


int
term_set_cursor_pos(uint8_t x, uint8_t y)
{
    rp.h.ah = 0x02;
    rp.h.dl = x;
    rp.h.dh = y;
    intr(0x10, &rp);
    return 1;
}


void
term_finallinebreak()
{
    /* DOS already outputs a final line break. */
}
#else
int
term_get_size_x()
{
    return 80;
}


int
term_get_size_y()
{
    return 25;
}


int
term_get_cursor_pos(uint8_t *x, uint8_t *y)
{
    return 0;
}


int
term_set_cursor_pos(uint8_t x, uint8_t y)
{
    return 0;
}


void
term_finallinebreak()
{
    printf("\n");
}
#endif


/* Port I/O functions. */
#ifdef __WATCOMC__
/* Defined in header. */
#elif defined(__GNUC__)
uint8_t
inb(uint16_t port)
{
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}


void
outb(uint16_t port, uint8_t val)
{
    __asm__ __volatile__("outb %0, %1" : : "a" (val), "Nd" (port));
}


uint16_t
inw(uint16_t port)
{
    uint16_t ret;
    __asm__ __volatile__("inw %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}


void
outw(uint16_t port, uint16_t val)
{
    __asm__ __volatile__("outw %0, %1" : : "a" (val), "Nd" (port));
}


uint32_t
inl(uint16_t port)
{
    uint32_t ret;
    __asm__ __volatile__("inl %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}


void
outl(uint16_t port, uint32_t val)
{
    __asm__ __volatile__("outl %0, %1" : : "a" (val), "Nd" (port));
}
#else
uint8_t
inb(uint16_t port)
{
    return 0xff;
}


void
outb(uint16_t port, uint8_t val)
{
}


uint16_t
inw(uint16_t port)
{
    return 0xffff;
}


void
outw(uint16_t port, uint16_t val)
{
}


uint32_t
inl(uint16_t port)
{
    return 0xffffffff;
}


void
outl(uint16_t port, uint32_t val)
{
}
#endif


/* PCI I/O functions. */
uint32_t
pci_cf8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    /* Generate a PCI port CF8h dword. */
    multi_t ret;
    ret.u8[3]  = 0x80;
    ret.u8[2]  = bus;
    ret.u8[1]  = dev << 3;
    ret.u8[1] |= func & 7;
    ret.u8[0]  = reg & 0xfc;
    return ret.u32;
}


uint8_t
pci_readb(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    uint8_t ret;
    uint16_t data_port = 0xcfc | (reg & 0x03);
    uint32_t cf8 = pci_cf8(bus, dev, func, reg);
    cli();
    outl(0xcf8, cf8);
    ret = inb(data_port);
    sti();
    return ret;
}


uint16_t
pci_readw(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    uint16_t ret, data_port = 0xcfc | (reg & 0x02);
    uint32_t cf8 = pci_cf8(bus, dev, func, reg);
    cli();
    outl(0xcf8, cf8);
    ret = inw(data_port);
    sti();
    return ret;
}


uint32_t
pci_readl(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    uint32_t ret, cf8 = pci_cf8(bus, dev, func, reg);
    cli();
    outl(0xcf8, cf8);
    ret = inl(0xcfc);
    sti();
    return ret;
}


void
pci_writeb(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint8_t val)
{
    uint16_t data_port = 0xcfc | (reg & 0x03);
    uint32_t cf8 = pci_cf8(bus, dev, func, reg);
    cli();
    outl(0xcf8, cf8);
    outb(data_port, val);
    sti();
}


void
pci_writew(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint16_t val)
{
    uint16_t data_port = 0xcfc | (reg & 0x02);
    uint32_t cf8 = pci_cf8(bus, dev, func, reg);
    cli();
    outl(0xcf8, cf8);
    outw(data_port, val);
    sti();
}


void
pci_writel(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t val)
{
    uint32_t cf8 = pci_cf8(bus, dev, func, reg);
    cli();
    outl(0xcf8, cf8);
    outl(0xcfc, val);
    sti();
}


/* File I/O functions. */
void
fseek_to(FILE *f, long offset)
{
    fseek(f, offset, SEEK_SET);

#ifdef __POSIX_UEFI__
    /* Work around broken fseek implementation. */
    long pos = ftell(f);
    if (pos == offset)
	return;

    uint8_t dummy[512];
    while (pos < offset) {
	fread(&dummy, MIN(sizeof(dummy), offset - pos), 1, f);
	pos = ftell(f);
    }
#endif
}