/*
    Copyright (C) 1998-2004  Charles MacDonald

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "shared.h"
#include "psp/pspcommon.h"

bitmap_t bitmap;
cart_t cart;                
input_t input;
int frame_underclock;
extern unsigned int gblCpuCycles;

/* Run the virtual console emulation for one frame */
void system_frame(int skip_render)
{
    static int iline_table[] = {0xC0, 0xE0, 0xF0};
    int lpf = (sms.display == DISPLAY_NTSC) ? 262 : 313;
    int iline;
//	u32 cycles = (227 * menuConfig.misc.z80Clock) / 100;
	u32 cycles = (228 * gblCpuCycles) >> 8;

	//50% speed for the first frames
	if (frame_underclock > 0)			{
		cycles = (228 * 66) / 100;
		frame_underclock--;
	}

    /* Debounce pause key */
    if(input.system & INPUT_PAUSE)
    {
        if(!sms.paused)
        {
            sms.paused = 1;

            z80_set_irq_line(IRQ_LINE_NMI, ASSERT_LINE);
            z80_set_irq_line(IRQ_LINE_NMI, CLEAR_LINE);
        }
    }
    else
    {
         sms.paused = 0;
    }

    for(vdp.line = 0; vdp.line < lpf; vdp.line++)
    {
       z80_execute(cycles);

        iline = iline_table[vdp.extended];

        if(!skip_render)
            render_line(vdp.line);

        if(vdp.line <= iline)
        {
            vdp.left -= 1;
            if(vdp.left == -1)
            {
                vdp.left = vdp.reg[0x0A];
                vdp.hint_pending = 1;

                z80_execute(16);

                if(vdp.reg[0x00] & 0x10)
                {
                    z80_set_irq_line(0, ASSERT_LINE);
                }
            }
        }
        else
        {
            vdp.left = vdp.reg[0x0A];
        }

        if(vdp.line == iline)
        {
            vdp.status |= 0x80;
            vdp.vint_pending = 1;

            z80_execute(16);

            if(vdp.reg[0x01] & 0x20)
            {
                z80_set_irq_line(0, ASSERT_LINE);
            }
        }

		if (!menuConfig.sound.perfectSynchro)
	        sound_update(vdp.line);
    }
}




void system_init(void)
{
    error_init();

    sms_init();
    pio_init();
    vdp_init();
    render_init();
    sound_init();

    sms.save = 0;
}

void system_shutdown(void)
{
#ifdef DEBUG
    int i;

    error("PC:%04X\tSP:%04X\n", z80_get_reg(Z80_PC), z80_get_reg(Z80_SP));
    error("AF:%04X\tAF:%04X\n", z80_get_reg(Z80_AF), z80_get_reg(Z80_AF2));
    error("BC:%04X\tBC:%04X\n", z80_get_reg(Z80_BC), z80_get_reg(Z80_BC2));
    error("DE:%04X\tDE:%04X\n", z80_get_reg(Z80_DE), z80_get_reg(Z80_DE2));
    error("HL:%04X\tHL:%04X\n", z80_get_reg(Z80_HL), z80_get_reg(Z80_HL2));
    error("IX:%04X\tIY:%04X\n", z80_get_reg(Z80_IX), z80_get_reg(Z80_IY));

    for(i = 0; i <= 0x0A; i++)
        error("%02X ", vdp.reg[i]);
    error("\n");

    error("MODE:%02X\n", vdp.mode & 7);
    error("PN:%04X\n", vdp.pn);
    error("CT:%04X\n", vdp.ct);
    error("PG:%04X\n", vdp.pg);
    error("SA:%04X\n", vdp.sa);
    error("SG:%04X\n", vdp.sg);

    error("\n");



#endif

    sms_shutdown();
    pio_shutdown();
    vdp_shutdown();
    render_shutdown();
    sound_shutdown();

    error_shutdown();
}

void system_reset(int reloadsram)
{
	//We run 20 frames underclocked for the region autodetection to be correct (except for PAL, it's good as is)
	if (menuConfig.video.country != 1)
		frame_underclock = 20;

	sms_reset();
    pio_reset();
    vdp_reset();
    render_reset();
    sound_reset();
	if (reloadsram)
		machine_manage_sram(SRAM_LOAD, 0);
}

void system_poweron(void)
{
    system_reset(1);
}

/*void system_poweroff(void)
{
}*/

