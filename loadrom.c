/*
    loadrom.c --
    File loading and management.
*/

#include "shared.h"
#include "psp/pspcommon.h"

extern uLong sram_crc;
char game_name[PATH_MAX];

typedef struct {
    uint32 crc;
	int mode;
    int mapper;
    int display;
//    int territory;
    char *name;
} rominfo_t;

#define MAPPER_NORMAL			MAPPER_SEGA
enum		{
	MODE_NORMAL = 0,					//Auto
	MODE_GG = 1,						//Force Game Gear
	MODE_SMS = 2,						//Force Master System
};

/*rominfo_t game_list[] = {
    {0x29822980, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, "Cosmic Spacehead"},
    {0xB9664AE1, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, "Fantastic Dizzy"},
    {0xA577CE46, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, "Micro Machines"},
    {0x8813514B, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, "Excellent Dizzy (Proto)"},
    {0xAA140C9C, MAPPER_CODIES, DISPLAY_PAL, TERRITORY_EXPORT, "Excellent Dizzy (Proto - GG)"}, 
    {-1        , -1  , NULL},
};*/

rominfo_t game_list[] = {
	//Elles étaient initialement en mode GG mais ne tournent pas en mode GG...
	{0x9942b69b, MODE_SMS, MAPPER_NORMAL, DISPLAY_NTSC, "Castle of Illusion [SMS-GG]"},
	{0x59840fd6, MODE_SMS, MAPPER_NORMAL, DISPLAY_NTSC, "Castle of Illusion [SMS-GG]"},

	{0x7bb81e3d, MODE_GG, MAPPER_NORMAL, DISPLAY_PAL, "Taito Chase H.Q. [SMS-GG]"},
	{0xda8e95a9, MODE_GG, MAPPER_NORMAL, DISPLAY_NTSC, "WWF Wrestlemania Steel Cage Challenge [SMS-GG]"}, 
	{0xcb42bd33, MODE_GG, MAPPER_NORMAL, DISPLAY_NTSC, "WWF Wrestlemania Steel Cage Challenge [SMS-GG]"}, // bad dump
	{0x5877b10d, MODE_GG, MAPPER_NORMAL, DISPLAY_NTSC, "Castle of Illusion [SMS-GG] [Hack]"}, // hack
	{0x44fbe8f6, MODE_GG, MAPPER_NORMAL, DISPLAY_PAL, "Taito Chase H.Q. [SMS-GG] [Hack]"}, // hack
	{0x29822980, MODE_NORMAL, MAPPER_CODIES, DISPLAY_PAL, "Cosmic Spacehead"},
	{0x6caa625b, MODE_NORMAL, MAPPER_CODIES, DISPLAY_PAL, "Cosmic Spacehead"},
	{0x4ed45bda, MODE_NORMAL, MAPPER_CODIES, DISPLAY_PAL, "Nomo's World Series Baseball"},
	{0xaa140c9c, MODE_SMS, MAPPER_CODIES, DISPLAY_PAL, "Excellent Dizzy Collection, The [SMS-GG]"},
	{0x8813514b, MODE_SMS, MAPPER_CODIES, DISPLAY_PAL, "Excellent Dizzy Collection, The [Proto]"},
	{0xb9664ae1, MODE_NORMAL,MAPPER_CODIES, DISPLAY_PAL, "Fantastic Dizzy"},
	{0xc888222b, MODE_GG, MAPPER_CODIES, DISPLAY_PAL, "Fantastic Dizzy [SMS-GG]"},
	{0xA577CE46, MODE_NORMAL, MAPPER_CODIES, DISPLAY_PAL, "Micro Machines"},
	{0x8640e7e8, MODE_NORMAL, MAPPER_KOREAN, DISPLAY_NTSC, "The three dragon"},
	{0x97d03541, MODE_NORMAL, MAPPER_KOREAN, DISPLAY_NTSC, "Sangokushi III (1994)(Game Line Co.)"},
	{0x16537865, MODE_NORMAL, MAPPER_KOREAN, DISPLAY_NTSC, "Dr. HELLO (KR).sms"}, //E3D53E738DD294A4,Dodgeball King,ID=G9511-RP4,Mapper=9,VER=Korean
	{0x5b3b922c, MODE_NORMAL, MAPPER_NORMAL, DISPLAY_PAL, "Sonic 2 [SMS]"},
	{0x95a18ec7, MODE_GG, MAPPER_NORMAL, DISPLAY_PAL, "Sonic 2 [GG]"},
	{-1 , -1 , -1, -1, NULL},
};



int load_rom(char *filename)
{
    int i;
    int size;
	const char *gbFileTypes[] = {
		".gb",
		".sgb",
		".gbc",
		".cgb",
	};
	char *extension;
	int is_gb = 0;

	soundPause = 1;
	gblModeColorIt = 0;

    if(cart.rom)
    {
        free(cart.rom);
        cart.rom = NULL;
    }

    if(check_zip(filename))
    {
        char name[PATH_MAX];
        cart.rom = loadFromZipByName(filename, name, &size);
        if(!cart.rom) return 0;
        strcpy(game_name, name);
    }
    else
    {
        FILE *fd = NULL;

        fd = fopen(filename, "rb");
        if(!fd) return 0;

        /* Seek to end of file, and get size */
        fseek(fd, 0, SEEK_END);
        size = ftell(fd);
        fseek(fd, 0, SEEK_SET);

        cart.rom = malloc(size);
        if(!cart.rom) return 0;
        fread(cart.rom, size, 1, fd);

        fclose(fd);

		strcpy(game_name, filename);
    }

	//Auto-detect game boy file type
	extension = getFileNameExtension(game_name);

	for (i=0;i<numberof(gbFileTypes);i++)		{
		if (!strlwrmixcmp(gbFileTypes[i], extension))
			break;
	}
	if (i < numberof(gbFileTypes))
		is_gb = 1;

	if (is_gb)				{
		int ramsize;
		gb_init();
		gblMachineType = EM_GBC;

		ramsize = 0;
//		sram_crc = 0;
//		ramsize = machine_manage_sram(SRAM_LOAD, 0);
		//0 octet de RAM au début
		if (!rom_load_rom(cart.rom, size, sram_space, ramsize))
			return 0;
		gb_reset();
		tilePalInit();
		if (menuConfig.gameboy.colorization)		{
			gblModeColorIt = CheckFichierPalette();
			if (gblModeColorIt)
				OuvreFichierPalette(0, "init");
		}
		else
			gblModeColorIt = 0;
	}
	else		{
		/* Don't load games smaller than 16K */
		if(size < 0x4000) return 0;

		/* Take care of image header, if present */
		if((size / 512) & 1)
		{
			size -= 512;
			memmove(cart.rom, cart.rom + 512, size);
		}

		cart.pages = (size / 0x4000);
		cart.crc = crc32(0L, cart.rom, size);

		/* Assign default settings (US NTSC machine) */
		cart.mapper     = MAPPER_SEGA;
		sms.display     = DISPLAY_NTSC;
		sms.territory   = TERRITORY_EXPORT;
		sms.console		= CONSOLE_NONE;

	//	oslDebug("%x", cart.crc);

		/* Look up mapper in game list */
		for(i = 0; game_list[i].name != NULL; i++)
		{
			if(cart.crc == game_list[i].crc)
			{
				cart.mapper     = game_list[i].mapper;
				//Bizarrement le son plante si on lance deux jeux d'un type différent à la suite...
	//            sms.display     = game_list[i].display;
				if (game_list[i].mode == MODE_GG)
					sms.console	= CONSOLE_GG;
				if (game_list[i].mode == MODE_SMS)
					sms.console	= CONSOLE_SMS;
	//            sms.territory   = game_list[i].territory;
			}
		}

		/* Figure out game image type */
		if (sms.console == CONSOLE_NONE)		{
			if(strcasecmp(strrchr(game_name, '.'), ".gg") == 0)
				sms.console = CONSOLE_GG;
			else
				sms.console = CONSOLE_SMS;
		}

		system_assign_device(PORT_A, DEVICE_PAD2B);
		system_assign_device(PORT_B, DEVICE_PAD2B);

		gblMachineType = EM_SMS;
		system_init();
	}


    return 1;
}

