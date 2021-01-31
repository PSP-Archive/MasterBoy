/*
    state.c --
    Save state management.
*/

#include "shared.h"

void system_save_state(VIRTUAL_FILE *fd)
{
    char *id = STATE_HEADER;
    uint16 version = STATE_VERSION;

    /* Write header */
    VirtualFileWrite(id, sizeof(id), 1, fd);
    VirtualFileWrite(&version, sizeof(version), 1, fd);

    /* Save VDP context */
    VirtualFileWrite(&vdp, sizeof(vdp_t), 1, fd);

    /* Save SMS context */
    VirtualFileWrite(&sms, sizeof(sms_t), 1, fd);

    VirtualFilePutc(cart.fcr[0], fd);
    VirtualFilePutc(cart.fcr[1], fd);
    VirtualFilePutc(cart.fcr[2], fd);
    VirtualFilePutc(cart.fcr[3], fd);
    VirtualFileWrite(cart.sram, 0x8000, 1, fd);

    /* Save Z80 context */
    VirtualFileWrite(Z80_Context, sizeof(Z80_Regs), 1, fd);
    VirtualFileWrite(&after_EI, sizeof(int), 1, fd);

    /* Save YM2413 context */
    VirtualFileWrite(FM_GetContextPtr(), FM_GetContextSize(), 1, fd);

    /* Save SN76489 context */
    VirtualFileWrite(SN76489_GetContextPtr(0), SN76489_GetContextSize(), 1, fd);
}


void system_load_state(VIRTUAL_FILE *fd)
{
    int i;
    uint8 *buf;
    char id[4];
    uint16 version;

    /* Initialize everything */
    z80_reset(0);
    z80_set_irq_callback(sms_irq_callback);
    system_reset(0);
    if(snd.enabled)
        sound_reset();

    /* Read header */
    VirtualFileRead(id, 4, 1, fd);
    VirtualFileRead(&version, 2, 1, fd);

    /* Load VDP context */
    VirtualFileRead(&vdp, sizeof(vdp_t), 1, fd);

    /* Load SMS context */
    VirtualFileRead(&sms, sizeof(sms_t), 1, fd);

    cart.fcr[0] = VirtualFileGetc(fd);
    cart.fcr[1] = VirtualFileGetc(fd);
    cart.fcr[2] = VirtualFileGetc(fd);
    cart.fcr[3] = VirtualFileGetc(fd);
    VirtualFileRead(cart.sram, 0x8000, 1, fd);

    /* Load Z80 context */
    VirtualFileRead(Z80_Context, sizeof(Z80_Regs), 1, fd);
    VirtualFileRead(&after_EI, sizeof(int), 1, fd);

    /* Load YM2413 context */
    buf = malloc(FM_GetContextSize());
    VirtualFileRead(buf, FM_GetContextSize(), 1, fd);
    FM_SetContext(buf);
    free(buf);

    /* Load SN76489 context */
    buf = malloc(SN76489_GetContextSize());
    VirtualFileRead(buf, SN76489_GetContextSize(), 1, fd);
    SN76489_SetContext(0, buf);
    free(buf);

    /* Restore callbacks */
    z80_set_irq_callback(sms_irq_callback);

    for(i = 0x00; i <= 0x2F; i++)
    {
        cpu_readmap[i]  = &cart.rom[(i & 0x1F) << 10];
        cpu_writemap[i] = dummy_write;
    }

    for(i = 0x30; i <= 0x3F; i++)
    {
        cpu_readmap[i] = &sms.wram[(i & 0x07) << 10];
        cpu_writemap[i] = &sms.wram[(i & 0x07) << 10];
    }

    sms_mapper_w(3, cart.fcr[3]);
    sms_mapper_w(2, cart.fcr[2]);
    sms_mapper_w(1, cart.fcr[1]);
    sms_mapper_w(0, cart.fcr[0]);

    /* Force full pattern cache update */
    bg_list_index = 0x200;
    for(i = 0; i < 0x200; i++)
    {
        bg_name_list[i] = i;
        bg_name_dirty[i] = -1;
    }

    /* Restore palette */
    for(i = 0; i < PALETTE_SIZE; i++)
        palette_sync(i);
}

