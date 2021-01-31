struct TVGMHeader			{
	int VGMIdent;
	int FileSize, Version, PSGClock, YM2413Clock, GD3Location, TotalLength, LoopOffset, LoopLength;
	//1.01
	int RecordingRate;
	//1.10
	int PSGWhiteNoiseFeedback;
	unsigned char PSGShiftRegisterWidth, reserved1;
	int YM2612Clock, YM2151Clock;
	//1.50
	int VGMDataOffset;
};

//#define VGMIDENT "Vgm "
#define VGMIDENT 0x206d6756
#define VGMDATADELTA (0x40 - 0xc)
#define DWORD long;

/*struct TGD3Header		{
	char IDString[4];
	int Version, Length;
};*/

