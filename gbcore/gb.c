/*--------------------------------------------------
   TGB Dual - Gameboy Emulator -
   Copyright (C) 2001  Hii

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

//-------------------------------------------------
// GB ���̑��G�~�����[�V������/�O���Ƃ̃C���^�[�t�F�[�X

#include "../psp/pspcommon.h"
#include "gb.h"
//#include "../menu.h"

int now_gb_mode;
struct gb_regs g_regs;
struct gbc_regs cg_regs;
int gb_lastVramCrc = 0;

word dmy[160*5]; // vframe �͂ݏo�������p
word vframe_mem[SIZE_LINE*(144+112)];
#ifdef USE_GPU
word *vframe = (word*)0x040CC000;
#else
word *vframe = vframe_mem;
#endif

struct ext_hook hook_proc;

int gbSkip;
//skip_buf;
//int now_frame;
int re_render;

char hook_ext;
char use_gba;

void gb_fill_vframe(word color)
{
	memset(vframe,0,VFRAME_SIZE);
}

void gb_init(void)
{
	lcd_init();
	rom_init();
	apu_init();// ROM����ɍ��ꂽ��
	mbc_init();
	cpu_init();
	sgb_init();
	#ifdef CHEAT_SUPPORT
		cheat_init();
	#endif
	
	apu_reset();
	mbc_reset();
	//target=NULL;
	
	gbe_init();

	gb_reset();

	hook_ext=false;
	use_gba=false;
}

void gb_reset()
{
	set_gb_type();
	
	g_regs.SC=0;
	g_regs.DIV=0;
	g_regs.TIMA=0;
	g_regs.TMA=0;
	g_regs.TAC=0;
	g_regs.LCDC=0x91;
	g_regs.STAT=0;
	g_regs.SCY=0;
	g_regs.SCX=0;
	g_regs.LY=153;
	g_regs.LYC=0;
	g_regs.BGP=0xFC;
	g_regs.OBP1=0xFF;
	g_regs.OBP2=0xFF;
	g_regs.WY=0;
	g_regs.WX=0;
	g_regs.IF=0;
	g_regs.IE=0;
	
	cpu_irq_check();
	
//	memset(&c_regs,0,sizeof(c_regs));
	
	cpu_reset();
	lcd_reset();
	apu_reset();
	mbc_reset();
	sgb_reset();
	
	gbe_reset();
	
	gb_fill_vframe(0);
	
//	now_frame=0;
	gbSkip=0;
	//skip_buf=0;
	re_render=0;
	
//	char *gb_names[]={"Invalid","Gameboy","SuperGameboy","Gameboy Color","Gameboy Advance"};
//	if (m_rom->get_loaded())
//		renderer_output_log("Current GB Type : %s \n",gb_names[m_rom->get_info()->gb_type]);
}

void gb_hook_extport(struct ext_hook *ext)
{
	hook_proc=*ext;
	hook_ext=true;
}

void gb_unhook_extport()
{
	hook_ext=false;
}

void gb_set_skip(int frame)
{
//	skip_buf=frame;
}

/*char gb_load_rom(byte *buf,int size,byte *ram,int ram_size)
{
	if (rom_load_rom(buf,size,ram,ram_size)){
		gb_reset();
		return true;
	}
	else
		return false;
}*/

/*#define write_state(in, inlen, out, outbak) \
{ \
	if(outbak){ \
		memcpy(out, in, inlen); \
	} \
	out += inlen; \
}*/

#define write_state(fd, in, inlen) \
{ \
	if(buf){ \
		memcpy(buf, in, inlen); \
		buf += inlen; \
	}else{ \
		VirtualFileWrite(in, inlen, 1, fd); \
	} \
}

void gb_save_state(VIRTUAL_FILE *fd, byte *buf)
{
	const int tbl_ram[]={1,1,1,4,16,8}; // 0��1�͕ی�

#ifdef CHEAT_SUPPORT
	if (buf || fd)
		cheat_decreate_cheat_map();
#endif

	write_state(fd, &rom_get_info()->gb_type,sizeof(int));

	if (rom_get_info()->gb_type<=2){ // normal gb & sgb
		write_state(fd, cpu_get_ram(),0x2000); // ram
		write_state(fd, cpu_get_vram(),0x2000); // vram
		write_state(fd, get_sram(),tbl_ram[rom_get_info()->ram_size]*0x2000); // sram
		write_state(fd, cpu_get_oam(),0xA0);
		write_state(fd, cpu_get_stack(),0x80);

		int page,ram_page;
		page=(mbc_get_rom()-get_rom())/0x4000;
		ram_page=(mbc_get_sram()-get_sram())/0x2000;

		write_state(fd, &page,sizeof(int)); // rom_page
		write_state(fd, &ram_page,sizeof(int)); // ram_page

		int dmy=0;

		write_state(fd, cpu_get_c_regs(),sizeof(struct cpu_regs)); // cpu_reg
		write_state(fd, &g_regs,sizeof(struct gb_regs));//sys_reg
		int halt=((*cpu_get_halt())?1:0);
		write_state(fd, &halt,sizeof(int));
		write_state(fd, &dmy,sizeof(int)); // ���̔łł̓V���A���ʐM�ʐM�����܂ł̃N���b�N��
		                                               // (�ʐM�̎d�l���啝�ɕς�������߃_�~�[�Ŗ��߂Ă���)
		int mbc_dat=mbc_get_state();
		write_state(fd, &mbc_dat,sizeof(int));//MBC

		int ext_is=mbc_is_ext_ram()?1:0;
		write_state(fd, &ext_is,sizeof(int));

		// ver 1.1 �ǉ�
		write_state(fd, apu_get_stat_cpu(),sizeof(struct apu_stat));
		write_state(fd, apu_get_mem(),0x30);
		write_state(fd, apu_get_stat_gen(),sizeof(struct apu_stat));

		byte resurved[256];
		memset(resurved,0,256);
		write_state(fd, resurved,256);//�����̂��߂Ɋm��
		
		// RIN�g��
		if(now_gb_mode==2){
			write_state(fd, &sgb_mode, sizeof(int));
			write_state(fd, &bit_received, sizeof(int));
			write_state(fd, &bits_received, sizeof(int));
			write_state(fd, &packets_received, sizeof(int));
			write_state(fd, &sgb_state, sizeof(int));
			write_state(fd, &sgb_index, sizeof(int));
			write_state(fd, &sgb_multiplayer, sizeof(int));
			write_state(fd, &sgb_fourplayers, sizeof(int));
			write_state(fd, &sgb_nextcontrol, sizeof(int));
			write_state(fd, &sgb_readingcontrol, sizeof(int));
			write_state(fd, &sgb_mask, sizeof(int));
			
			write_state(fd, sgb_palette, sizeof(unsigned short)*8*16);
			write_state(fd, sgb_palette_memory, sizeof(unsigned short)*512*4);
			write_state(fd, sgb_buffer, 7*16);
			write_state(fd, sgb_ATF, 18*20);
			write_state(fd, sgb_ATF_list, 45*20*18);
			/*
			sceIoWrite(fd, sgb_border, 2048);
			sceIoWrite(fd, sgb_borderchar, 32*256);
			
			int i, j, n=0;
			for (i=0; i<224; i++){
				for (j=0; j<256; j++){
					if (i>=40 && i<=183 && j==48) j=208;
					border_tmp[n++] = sgb_border_buffer[i*256+j];
				}
			}
			sceIoWrite(fd, border_tmp, sizeof(border_tmp));
			*/
		}
	}
	else if (rom_get_info()->gb_type>=3){ // GB Colour / GBA
		write_state(fd, cpu_get_ram(),0x2000*4); // ram
		write_state(fd, cpu_get_vram(),0x2000*2); // vram
		write_state(fd, get_sram(),tbl_ram[rom_get_info()->ram_size]*0x2000); // sram
		write_state(fd, cpu_get_oam(),0xA0);
		write_state(fd, cpu_get_stack(),0x80);

		int cpu_dat[16];
		cpu_save_state(cpu_dat);

		int page,ram_page;
		page=(mbc_get_rom()-get_rom())/0x4000;
		ram_page=(mbc_get_sram()-get_sram())/0x2000;

		write_state(fd, &page,sizeof(int)); // rom_page
		write_state(fd, &ram_page,sizeof(int)); // ram_page
		write_state(fd, cpu_dat+0,sizeof(int));//int_page	//color
		write_state(fd, cpu_dat+1,sizeof(int));//vram_page	//color

		int dmy=0;

		write_state(fd, cpu_get_c_regs(),sizeof(struct cpu_regs)); // cpu_reg
		write_state(fd, &g_regs,sizeof(struct gb_regs));//sys_reg
		write_state(fd, &cg_regs,sizeof(struct gbc_regs));//col_reg		//color
		write_state(fd, lcd_get_pal(0),sizeof(word)*(8*4*2));//palette	//color
		int halt=((*cpu_get_halt())?1:0);
		write_state(fd, &halt,sizeof(int));
		write_state(fd, &dmy,sizeof(int)); // ���̔łł̓V���A���ʐM�ʐM�����܂ł̃N���b�N��

		int mbc_dat=mbc_get_state();
		write_state(fd, &mbc_dat,sizeof(int));//MBC

		int ext_is=mbc_is_ext_ram()?1:0;
		write_state(fd, &ext_is,sizeof(int));

		//���̑����X
		write_state(fd, cpu_dat+2,sizeof(int));	//color
		write_state(fd, cpu_dat+3,sizeof(int));	//color
		write_state(fd, cpu_dat+4,sizeof(int));	//color
		write_state(fd, cpu_dat+5,sizeof(int));	//color
		write_state(fd, cpu_dat+6,sizeof(int));	//color
		write_state(fd, cpu_dat+7,sizeof(int));	//color

		// ver 1.1 �ǉ�
		write_state(fd, apu_get_stat_cpu(),sizeof(struct apu_stat));
		write_state(fd, apu_get_mem(),0x30);
		write_state(fd, apu_get_stat_gen(),sizeof(struct apu_stat));

		byte resurved[256],reload=1;
		memset(resurved,0,256);
//		resurved[0]=1;
		write_state(fd, &reload,1);
		write_state(fd, resurved,256);//�����̂��߂Ɋm��
	}

#ifdef CHEAT_SUPPORT
	if (buf || fd)
		cheat_create_cheat_map();
#endif

	return;
}

#define read_state(fd, out, len) \
{ \
	if(buf){ \
		memcpy(out, buf, len); \
		buf += len; \
	}else{ \
		VirtualFileRead(out, len, 1, fd); \
	} \
}

void gb_restore_state(VIRTUAL_FILE *fd, const byte *buf)
{
	const int tbl_ram[]={1,1,1,4,16,8}; // 0��1�͕ی�
	int gb_type,dmy;
	
	read_state(fd, &gb_type, sizeof(int));
	
	rom_get_info()->gb_type=gb_type;
	
	if (gb_type<=2){
		read_state(fd, cpu_get_ram(),0x2000); // ram
		read_state(fd, cpu_get_vram(),0x2000); // vram
		read_state(fd, get_sram(),tbl_ram[rom_get_info()->ram_size]*0x2000); // sram
		read_state(fd, cpu_get_oam(),0xA0);
		read_state(fd, cpu_get_stack(),0x80);

		int page,ram_page;
		read_state(fd, &page, sizeof(int)); // rom_page
		read_state(fd, &ram_page, sizeof(int)); // ram_page
		mbc_set_page(page,ram_page);

		read_state(fd, cpu_get_c_regs(),sizeof(struct cpu_regs)); // cpu_reg
		cpu_set_c_regs();
		read_state(fd, (void *)&g_regs,sizeof(struct gb_regs)); // sys_reg
		int halt;
		read_state(fd, &halt,sizeof(int));
		*cpu_get_halt()=((halt)?true:false);
		read_state(fd, &dmy,sizeof(int));

		int mbc_dat;
		read_state(fd, &mbc_dat,sizeof(int)); // MBC
		mbc_set_state(mbc_dat);
		int ext_is;
		read_state(fd, &ext_is,sizeof(int));
		mbc_set_ext_is(ext_is?true:false);

		// ver 1.1 �ǉ�
//		byte tmp[256],tester[100];
//		read_state(fd, tmp, 100); // �Ƃ肠�������ׂĂ݂�
//		_memset(tester,0,100);
//		if (_memcmp(tmp,tester,100)!=0){
			// apu ����
//			sceIoLseek(fd, -100, 1);
			read_state(fd, apu_get_stat_cpu(),sizeof(struct apu_stat));
			read_state(fd, apu_get_mem(),0x30);
			read_state(fd, apu_get_stat_gen(),sizeof(struct apu_stat));
//		}

		byte resurved[256];
		read_state(fd, resurved, 256);//�����̂��߂Ɋm��
		
		// RIN�g��
		if(gb_type==2 && sgb_mode){
			int dmy;
			read_state(fd, &dmy, sizeof(int));

			read_state(fd, &bit_received, sizeof(int));
			read_state(fd, &bits_received, sizeof(int));
			read_state(fd, &packets_received, sizeof(int));
			read_state(fd, &sgb_state, sizeof(int));
			read_state(fd, &sgb_index, sizeof(int));
			read_state(fd, &sgb_multiplayer, sizeof(int));
			read_state(fd, &sgb_fourplayers, sizeof(int));
			read_state(fd, &sgb_nextcontrol, sizeof(int));
			read_state(fd, &sgb_readingcontrol, sizeof(int));
			read_state(fd, &sgb_mask, sizeof(int));
			
			read_state(fd, sgb_palette, sizeof(unsigned short)*8*16);
			read_state(fd, sgb_palette_memory, sizeof(unsigned short)*512*4);
			read_state(fd, sgb_buffer, 7*16);
			read_state(fd, sgb_ATF, 18*20);
			read_state(fd, sgb_ATF_list, 45*20*18);
			/*
			read_state(fd, sgb_border, 2048);
			read_state(fd, sgb_borderchar, 32*256);
			read_state(fd, border_tmp, sizeof(border_tmp));
			int i, j, n=0;
			for (i=0; i<224; i++){
				for (j=0; j<256; j++){
					if (i>=40 && i<=183 && j==48) j=208;
					sgb_border_buffer[i*256+j] = border_tmp[n++];
				}
			}
			*/
		}
	}
	else if (gb_type>=3){ // GB Colour / GBA
		read_state(fd, cpu_get_ram(),0x2000*4); // ram
		read_state(fd, cpu_get_vram(),0x2000*2); // vram
		read_state(fd, get_sram(),tbl_ram[rom_get_info()->ram_size]*0x2000); // sram
		read_state(fd, cpu_get_oam(),0xA0);
		read_state(fd, cpu_get_stack(),0x80);

		int cpu_dat[16];

		int page,ram_page;
		read_state(fd, &page, sizeof(int)); // rom_page
		read_state(fd, &ram_page, sizeof(int)); // ram_page
		mbc_set_page(page,ram_page);
		page=(mbc_get_rom()-get_rom())/0x4000;
		ram_page=(mbc_get_sram()-get_sram())/0x2000;

		read_state(fd, cpu_dat+0,sizeof(int));//int_page
		read_state(fd, cpu_dat+1,sizeof(int));//vram_page

		int dmy;
		read_state(fd, cpu_get_c_regs(),sizeof(struct cpu_regs)); // cpu_reg
		cpu_set_c_regs();
		read_state(fd, &g_regs,sizeof(struct gb_regs));//sys_reg
		read_state(fd, &cg_regs,sizeof(struct gbc_regs));//col_reg
		read_state(fd, lcd_get_pal(0),sizeof(word)*(8*4*2));//palette
		int halt;
		read_state(fd, &halt,sizeof(int));
		*cpu_get_halt()=(halt?true:false);
		read_state(fd, &dmy,sizeof(int)); // ���̔łł̓V���A���ʐM�ʐM�����܂ł̃N���b�N��

		int mbc_dat;
		read_state(fd, &mbc_dat,sizeof(int)); // MBC
		mbc_set_state(mbc_dat);
		int ext_is;
		read_state(fd, &ext_is,sizeof(int));
		mbc_set_ext_is(ext_is?true:false);

		//���̑����X
		read_state(fd, cpu_dat+2,sizeof(int));
		read_state(fd, cpu_dat+3,sizeof(int));
		read_state(fd, cpu_dat+4,sizeof(int));
		read_state(fd, cpu_dat+5,sizeof(int));
		read_state(fd, cpu_dat+6,sizeof(int));
		read_state(fd, cpu_dat+7,sizeof(int));
		cpu_restore_state(cpu_dat);

		// ver 1.1 �ǉ�
//		byte tmp[256],tester[100];
//		read_state(fd, tmp,100); // �Ƃ肠�������ׂĂ݂�
//		_memset(tester,0,100);
//		if (_memcmp(tmp,tester,100)!=0){
			// apu ����
//			sceIoLseek(fd, -100, 1);
			read_state(fd, apu_get_stat_cpu(),sizeof(struct apu_stat));
			read_state(fd, apu_get_mem(),0x30);
			read_state(fd, apu_get_stat_gen(),sizeof(struct apu_stat));

//			read_state(fd, tmp,1);
			/* renderer_map_color����lcd_get_mapped_pal�ւ̕ύX�͕s�v�ɂȂ�܂����B ruka
			int i;
			if (tmp[0])
				for (i=0;i<64;i++)
					lcd_get_mapped_pal(i>>2)[i&3]=renderer_map_color(lcd_get_pal(i>>2)[i&3]);
			else{
				for (i=0;i<64;i++)
					lcd_get_pal(i>>2)[i&3]=renderer_unmap_color(lcd_get_pal(i>>2)[i&3]);
				for (i=0;i<64;i++)
					lcd_get_mapped_pal(i>>2)[i&3]=renderer_map_color(lcd_get_pal(i>>2)[i&3]);
			}*/
//		}
		byte resurved[256];
		read_state(fd, resurved,256);//�����̂��߂Ɋm��
	}

#ifdef CHEAT_SUPPORT
	cheat_create_cheat_map();
#endif
	gb_invalidate_all_colors();
}

void gb_refresh_pal()
{
	/* renderer_map_color����lcd_get_mapped_pal�ւ̕ύX�͕s�v�ɂȂ�܂����B ruka
	int i;
	for (i=0;i<64;i++)
		lcd_get_mapped_pal(i>>2)[i&3]=renderer_map_color(lcd_get_pal(i>>2)[i&3]);
	*/
}

#if 0
int gb_run()
{
	int command = 0;
	if (rom_get_loaded()){
		if (g_regs.LCDC&0x80){ // LCDC �N����
			g_regs.LY=(g_regs.LY+1)%154;

			g_regs.STAT&=0xF8;
			if (g_regs.LYC==g_regs.LY){
				g_regs.STAT|=4;
				if (g_regs.STAT&0x40)
					cpu_irq(INT_LCDC);
			}
			if (g_regs.LY==0){
//				gbe_refresh();
				//Frame terminated
				command |= 1;

//				if (now_frame>=skip){
				if (!gbSkip)		{
//					render_screen(vframe);
					now_frame=0;
				}
				else
					now_frame++;
				lcd_clear_win_count();
//				skip=skip_buf;
			}
			if (g_regs.LY>=144){ // VBlank ���Ԓ�
				g_regs.STAT|=1;
				if (g_regs.LY==144){
					cpu_exec(72);
					cpu_irq(INT_VBLANK);
					if (g_regs.STAT&0x10)
						cpu_irq(INT_LCDC);
					cpu_exec(456-80);
				}
				else if (g_regs.LY==153){
					cpu_exec(80);
					g_regs.LY=0;
					cpu_exec(456-80); // �O�̃��C���̂��Ȃ葁�ڂ���0�ɂȂ�悤���B
					g_regs.LY=153;
				}
				else
					cpu_exec(456);
			}
			else{ // VBlank ���ԊO
				g_regs.STAT|=2;
				if (g_regs.STAT&0x20)
					cpu_irq(INT_LCDC);
				cpu_exec(80); // state=2
				g_regs.STAT|=3;
				cpu_exec(169); // state=3

				if (dma_executing){ // HBlank DMA
					if (b_dma_first){
						dma_dest_bank=vram_bank;
						if (dma_src<0x4000)
							dma_src_bank=get_rom();
						else if (dma_src<0x8000)
							dma_src_bank=mbc_get_rom();
						else if (dma_src>=0xA000&&dma_src<0xC000)
							dma_src_bank=mbc_get_sram()-0xA000;
						else if (dma_src>=0xC000&&dma_src<0xD000)
							dma_src_bank=ram-0xC000;
						else if (dma_src>=0xD000&&dma_src<0xE000)
							dma_src_bank=ram_bank-0xD000;
						else dma_src_bank=NULL;
						b_dma_first=false;
					}
					memcpy(dma_dest_bank+(dma_dest&0x1ff0),dma_src_bank+dma_src,16);
//					fprintf(cpu_file,"%03d : dma exec %04X -> %04X rest %d\n",g_regs.LY,cpu_dma_src,cpu_dma_dest,cpu_dma_rest);

					dma_src+=16;
					dma_src&=0xfff0;
					dma_dest+=16;
					dma_dest&=0xfff0;
					dma_rest--;
					if (!dma_rest)
						dma_executing=false;

//					cpu_total_clock+=207*(cpu_speed?2:1);
//					cpu_sys_clock+=207*(cpu_speed?2:1);
//					cpu_div_clock+=207*(cpu_speed?2:1);
//					g_regs.STAT|=3;

//					if (now_frame>=skip && !sgb_mask)
					if (!gbSkip && !sgb_mask)
						lcd_render(vframe,g_regs.LY);

					g_regs.STAT&=0xfc;
					cpu_exec(207); // state=3
				}
				else{
/*					if (lcd_get_sprite_count()){
						if (lcd_get_sprite_count()>=10){
							cpu_exec(129);
							if ((g_regs.STAT&0x08))
								cpu_irq(INT_LCDC);
							g_regs.STAT&=0xfc;
							if (now_frame>=skip)
								lcd_render(vframe,g_regs.LY);
							cpu_exec(78); // state=0
						}
						else{
							cpu_exec(129*lcd_get_sprite_count()/10);
							if ((g_regs.STAT&0x08))
								cpu_irq(INT_LCDC);
							g_regs.STAT&=0xfc;
							if (now_frame>=skip)
								lcd_render(vframe,g_regs.LY);
							cpu_exec(207-(129*lcd_get_sprite_count()/10)); // state=0
						}
					}
					else{
*/						g_regs.STAT&=0xfc;
//						if (now_frame>=skip && !sgb_mask)
						if (!gbSkip && !sgb_mask)
							lcd_render(vframe,g_regs.LY);
						if ((g_regs.STAT&0x08))
							cpu_irq(INT_LCDC);
						cpu_exec(207); // state=0
//					}
				}
			}
		}
		else{ // LCDC ��~��
			g_regs.LY=0;
//			g_regs.LY=(g_regs.LY+1)%154;
			re_render++;
			if (re_render>=154){
				memset(vframe,0xff,(160+16)*144*2);
//				gbe_refresh();

				//Frame terminated
				command |= 1;

//				if (now_frame>=skip){
				if (!gbSkip)		{
//					render_screen(vframe);
					now_frame=0;
				}
				else
					now_frame++;
				lcd_clear_win_count();
				re_render=0;
			}
			g_regs.STAT&=0xF8;
			cpu_exec(456);
		}
	}

	return command;
}
#else
//int skip = 0;

int gb_run()
{
	int cmd = 0;
	if (rom_get_loaded()){
		if (g_regs.LCDC&0x80){ // LCDC �N����
			g_regs.LY=(g_regs.LY+1)%154;

			g_regs.STAT&=0xF8;
			if (g_regs.LYC==g_regs.LY){
				g_regs.STAT|=4;
				if (g_regs.STAT&0x40)
					cpu_irq(INT_LCDC);
			}
			if (g_regs.LY==0){
//				renderer_refresh();
				cmd |= 1;
/*				if (gbSkip){
					render_screen(vframe);
//					now_frame=0;
				}
				else
					now_frame++;*/
				lcd_clear_win_count();
//				skip=skip_buf;
			}
			if (g_regs.LY>=144){ // VBlank ���Ԓ�
				g_regs.STAT|=1;
				if (g_regs.LY==144){
					cpu_exec(72);
					cpu_irq(INT_VBLANK);
					if (g_regs.STAT&0x10)
						cpu_irq(INT_LCDC);
					cpu_exec(456-80);
				}
				else if (g_regs.LY==153){
					cpu_exec(80);
					g_regs.LY=0;
					cpu_exec(456-80); // �O�̃��C���̂��Ȃ葁�ڂ���0�ɂȂ�悤���B
					g_regs.LY=153;
				}
				else
					cpu_exec(456);
			}
			else{ // VBlank ���ԊO
				g_regs.STAT|=2;
				if (g_regs.STAT&0x20)
					cpu_irq(INT_LCDC);
				cpu_exec(80); // state=2
				g_regs.STAT|=3;
				cpu_exec(169); // state=3

				if (dma_executing){ // HBlank DMA
					if (b_dma_first){
						dma_dest_bank=vram_bank;
						if (dma_src<0x4000)
							dma_src_bank=get_rom();
						else if (dma_src<0x8000)
							dma_src_bank=mbc_get_rom();
						else if (dma_src>=0xA000&&dma_src<0xC000)
							dma_src_bank=mbc_get_sram()-0xA000;
						else if (dma_src>=0xC000&&dma_src<0xD000)
							dma_src_bank=ram-0xC000;
						else if (dma_src>=0xD000&&dma_src<0xE000)
							dma_src_bank=ram_bank-0xD000;
						else dma_src_bank=NULL;
						b_dma_first=false;
					}
					memcpy(dma_dest_bank+(dma_dest&0x1ff0),dma_src_bank+dma_src,16);
//					fprintf(cpu_file,"%03d : dma exec %04X -> %04X rest %d\n",g_regs.LY,cpu_dma_src,cpu_dma_dest,cpu_dma_rest);

					dma_src+=16;
					dma_src&=0xfff0;
					dma_dest+=16;
					dma_dest&=0xfff0;
					dma_rest--;
					if (!dma_rest)
						dma_executing=false;

//					cpu_total_clock+=207*(cpu_speed?2:1);
//					cpu_sys_clock+=207*(cpu_speed?2:1);
//					cpu_div_clock+=207*(cpu_speed?2:1);
//					g_regs.STAT|=3;

					if (!gbSkip && !sgb_mask)
						lcd_render(vframe,g_regs.LY);

					g_regs.STAT&=0xfc;
					cpu_exec(207); // state=3
				}
				else{
/*					if (lcd_get_sprite_count()){
						if (lcd_get_sprite_count()>=10){
							cpu_exec(129);
							if ((g_regs.STAT&0x08))
								cpu_irq(INT_LCDC);
							g_regs.STAT&=0xfc;
							if (now_frame>=skip)
								lcd_render(vframe,g_regs.LY);
							cpu_exec(78); // state=0
						}
						else{
							cpu_exec(129*lcd_get_sprite_count()/10);
							if ((g_regs.STAT&0x08))
								cpu_irq(INT_LCDC);
							g_regs.STAT&=0xfc;
							if (now_frame>=skip)
								lcd_render(vframe,g_regs.LY);
							cpu_exec(207-(129*lcd_get_sprite_count()/10)); // state=0
						}
					}
					else{
*/						g_regs.STAT&=0xfc;
						if (!gbSkip && !sgb_mask)
							lcd_render(vframe,g_regs.LY);
						if ((g_regs.STAT&0x08))
							cpu_irq(INT_LCDC);
						cpu_exec(207); // state=0
//					}
				}
			}
		}
		else{ // LCDC ��~��
			g_regs.LY=0;
//			g_regs.LY=(g_regs.LY+1)%154;
			re_render++;
			if (re_render>=154)		{
//				memset(vframe,0xff,(160+16)*144*2);
				if (!gbSkip)
					memset(vframe, 0xff, VFRAME_SIZE);
//				renderer_refresh();
				cmd |= 1;
/*				if (!gbSkip)		{
//					render_screen(vframe);
					now_frame=0;
				}
				else
					now_frame++;*/
				lcd_clear_win_count();
				re_render=0;
			}
			g_regs.STAT&=0xF8;
			cpu_exec(456);
		}

//		if (!menuConfig.sound.perfectSynchro)
//			sound_update_gb(line);
	}
	return cmd;

}

void exiting_lcdc()		{
	if (!gblModeColorIt)
		return;

	char temp[100];
	u32 crc = tileMapGetVramChecksum();
	if (crc != gb_lastVramCrc || gblConfigAutoShowCrc)		{
		OuvreFichierPalette(crc, NULL);
		if (gblConfigAutoShowCrc)		{
			sprintf(temp, "New VRAM CRC: %08x", crc);
			menuDisplayStatusMessage(temp, 600);
		}
		gb_lastVramCrc = crc;
	}
}

#endif

