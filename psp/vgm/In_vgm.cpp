//-----------------------------------------------------------------
// in_vgm
// VGM audio input plugin for Winamp
// http://www.smspower.org/music
// by Maxim <maxim\x40smspower\x2eorg> in 2001-2005
// with help from BlackAura in March and April 2002
// YM2612 PCM additions with Blargg in November 2005
//-----------------------------------------------------------------

#include "../sound_int.h"

//#define YM2612GENS // Use Gens YM2612 rather than MAME

// Relative volumes of sound cores
// PSG = 1
#define YM2413RelativeVol 1  // SMS/Mark III with FM pack - empirical value, real output would help
#define GENSYM2612RelativeVol 4  // Mega Drive/Genesis
#define MAMEYM2612RelativeVol 3  // Mega Drive/Genesis
#define YM2151RelativeVol 4 // CPS1

#define PLUGINNAME "VGM input plugin v0.34"  // " "__DATE__" "__TIME__
#define MINVERSION 0x100
#define REQUIREDMAJORVER 0x100
#define INISECTION "Maxim's VGM input plugin"
#define MINGD3VERSION 0x100
#define REQUIREDGD3MAJORVER 0x100

#define SN76489_NUM_CHANNELS 4
#define YM2413_NUM_CHANNELS 14

// PSG has 4 channels, 2^4-1=0xf
#define SN76489_MUTE_ALLON 0xf
// YM2413 has 14 (9 + 5 percussion), BUT it uses 1=mute, 0=on
#define YM2413_MUTE_ALLON 0
// These two are preliminary and may change
#define YM2612_MUTE_ALLON 0
#define YM2151_MUTE_ALLON 0

/*#include <windows.h>
#include <stdio.h>
#include <float.h>
#include <commctrl.h>
#include <math.h>
#include <zlib.h>
#include <assert.h>*/
#include <oslib/oslib.h>
//#include "in2.h"

#include "vgm.h"

// #define EMU2413_COMPACTION
#include "emu2413.h"

#include "sn76489.h"
//#include "resource.h"

// BlackAura - MAME FM synthesiser (aargh!)
#include "mame_ym2151.h"  // MAME YM2151

#ifdef YM2612GENS
#include "gens_ym2612.h"  // Gens YM2612
#else
#include "mame_fm.h"    // MAME YM2151
#endif

// MAME 0.67 YM2413 core
//#include "mame_ym2413.h"

#define ROUND(x) ((int)(x>0?x+0.5:x-0.5))

typedef unsigned short wchar;  // For Unicode strings

//HANDLE PluginhInst;

/*BOOL WINAPI DllMain(HANDLE hInst, ULONG ul_reason_for_call, LPVOID lpReserved) {
  PluginhInst=hInst;
  return TRUE;
}*/

// post this to the main window at end of file (after playback has stopped)
//#define WM_WA_MPEG_EOF WM_USER+2

// raw configuration
#define NCH 2      // Number of channels
            // NCH 1 doesn't work properly... yet. I might fix it later.
#define SAMPLERATE vgmSampleRate
#define MAX_VOLUME 100  // Number of steps for fadeout; can't have too many because of overflow

//In_Module mod;      // the output module (declared near the bottom of this file)
//char lastfn[MAX_PATH]="";  // currently playing file (used for getting info on the current file)
int paused;        // are we paused?
int vgmSampleRate = 0, vgmSampleRateModified = 1;
//char TextFileName[MAX_PATH];  // holds a filename for the Unicode text file

//#define SampleBufferSize (1024*NCH*2)
//short SampleBuffer[SampleBufferSize];  // sample buffer

#ifdef __cplusplus
extern "C"		{
#endif /* #ifdef __cplusplus */

typedef void* gzFile;
typedef int z_off_t;
gzFile gzopen(const char *path, const char *mode);
int gzread(gzFile file, void* buf, unsigned len);
z_off_t gzseek (gzFile file, z_off_t offset, int whence);
int gzclose (gzFile file);
int gzputc(gzFile file, int c);
int gzgetc (gzFile file);
int gzungetc (int c, gzFile file);

#ifdef __cplusplus
}
#endif /* #ifdef __cplusplus */

//gzFile InputFile;
CFILE *VGMFile;

OPLL *opll;  // EMU2413 structure

// BlackAura - FMChip flags
#define FM_YM2413  0x01  // Bit 0 = YM2413
#define FM_YM2612  0x02  // Bit 1 = YM2612
#define FM_YM2151  0x04  // Bit 2 = TM2151

#define USINGCHIP(chip) (FMChips&chip)
#define CHIPINITED(chip) (FMChipsInited&chip)

#define GD3IDENT 0x20336447 // "Gd3 "

struct TGD3Header {
  unsigned long GD3Ident;     // "Gd3 "
  unsigned long Version;      // 0x000000100 for 1.00
  unsigned long Length;       // Length of string data following this point
};

int killDecodeThread=0;                       // the kill switch for the decode thread
//HANDLE thread_handle=INVALID_HANDLE_VALUE;    // the handle to the decode thread
//DWORD WINAPI __stdcall DecodeThread(void *b); // the decode thread procedure

// forward references
void setoutputtime(int time_in_ms);
int  getoutputtime();

int
  TrackLengthInms,    // Current track length in ms
  PlaybackRate,       // in Hz
  FileRate,           // in Hz
  NumLoops,           // how many times to play looped section
  NumLoopsDone,       // how many loops we've played
  LoopLengthInms,     // length of looped section in ms
  LoopOffset,         // File offset of looped data start
  VGMDataOffset,      // File offset of data start
  PSGClock=0,         // SN76489 clock rate
  YM2413Clock=0,      // FM clock rates
  YM2612Clock=0,      // 
  YM2151Clock=0,      // 
  FMChips=0,          // BlackAura - FM Chips enabled
  FMChipsInited=0,	  // Brunni - FM Chips initialized
  SeekToSampleNumber, // For seeking
  FileInfoJapanese,   // Whether to show Japanese in the info dialogue
  UseMB,              // Whether to open HTML in the MB
  AutoMB,             // Whether to automatically show HTML in MB
  ForceMBOpen,        // Whether to force the MB to open if closed when doing AutoMB
  YM2413HiQ,
  Overdrive,
  ImmediateUpdate,
  PauseBetweenTracksms,
  PauseBetweenTracksCounter,
  LoopingFadeOutms=5000,
  LoopingFadeOutCounter,
  LoopingFadeOutTotal,
  MutePersistent=0,
  MLJapanese,
  MLShowFM,
  MLType,
  SN76489_Mute = SN76489_MUTE_ALLON,
  SN76489_VolumeArray,
  SN76489_Pan [ SN76489_NUM_CHANNELS ], // panning 0..254, 127 is centre
  YM2413_Pan [ YM2413_NUM_CHANNELS ],
  RandomisePanning;
long int 
  YM2413Channels=YM2413_MUTE_ALLON,  // backup when stopped. PSG does it itself.
  YM2612Channels=YM2612_MUTE_ALLON,
  YM2151Channels=YM2151_MUTE_ALLON;
char
  TrackTitleFormat[100],      // Track title formatting
//  CurrentURLFilename[MAX_PATH],  // Filename current URL has been saved to
  CurrentURL[1024];        // Current URL
char
  *FilenameForInfoDlg;      // Filename passed to file info dialogue

// Blargg: PCM data for current file (loaded in play thread)
static unsigned char* pcm_data = NULL;
static unsigned long pcm_data_size;
static unsigned long pcm_pos;

//-----------------------------------------------------------------
// Check if string is a URL
//-----------------------------------------------------------------
/*int IsURL(char *url) {
  if (
    (strstr(url,"http://")==url) ||
    (strstr(url,"ftp://" )==url) ||
    (strstr(url,"www."   )==url)
  ) return 1;
  return 0;
}

//-----------------------------------------------------------------
// Open URL in minibrowser or browser
//-----------------------------------------------------------------
void OpenURL(char *url) {
  FILE *f;

  f=fopen(TextFileName,"wb");
  // Put start text
  fprintf(f,
    "<html><head><META HTTP-EQUIV=\"Refresh\" CONTENT=\"0; URL=%s\"></head><body>Opening %s<br><a href=\"%s\">Click here</a> if page does not open</body></html>",
    url,url,url
  );
  fclose(f);

  if (UseMB) {
    url=malloc(strlen(TextFileName)+9);
    strcpy(url,"file:///");
    strcat(url,TextFileName);
    SendMessage(mod.hMainWindow,WM_USER,(WPARAM)NULL,241);  // open minibrowser
    SendMessage(mod.hMainWindow,WM_USER,(WPARAM)url,241);  // display file
    free(url);
  }
  else ShellExecute(mod.hMainWindow,NULL,TextFileName,NULL,NULL,SW_SHOWNORMAL);
}

char * printtime(char *s,double timeinseconds) {
  int hours=(int)timeinseconds/3600;
  int mins=(int)timeinseconds/60 - hours*60;
  double secs=timeinseconds - mins*60;

  if(hours)
    // unlikely
    sprintf(s,"%d:%02d:%04.1f",hours,mins,secs);
  else if(mins)
    sprintf(s,"%d:%04.1f",mins,secs);
  else
    sprintf(s,"%.1fs",secs);

  return s;
}*/

//-----------------------------------------------------------------
// Show GD3 info as HTML
//-----------------------------------------------------------------
/*void InfoInBrowser(char *filename, int ForceOpen) {
  FILE *f;
  gzFile  *fh;
  struct TVGMHeader  VGMHeader;
  struct TGD3Header  GD3Header;
  wchar *GD3string,*p;
  int i,j;
  char *url;
  char tempstr[256];
  const char What[10][32]={
    "Track title",
    "",
    "Game name",
    "",
    "System name",
    "",
    "Track author",
    "",
    "Game release date",
    "VGM creator"
  };

  // Read in Unicode string
  fh=gzopen(filename,"rb");
  i=gzread(fh,&VGMHeader,sizeof(VGMHeader));

  if (
    (i<sizeof(VGMHeader)) ||                    // file too short/error reading
    (VGMHeader.VGMIdent != VGMIDENT) ||          // no marker
    (VGMHeader.Version<MINVERSION) ||                // below min ver
    ((VGMHeader.Version & REQUIREDMAJORVER)!=REQUIREDMAJORVER) ||  // != required major ver
    (!VGMHeader.GD3Offset)                      // no GD3
  ) {
    gzclose(fh);
    return;
  }
  gzseek(fh,VGMHeader.GD3Offset+0x14,SEEK_SET);
  i=gzread(fh,&GD3Header,sizeof(GD3Header));
  if (
    (i<sizeof(GD3Header)) ||                    // file too short/error reading
    (GD3Header.GD3Ident != GD3IDENT ) ||          // no marker
    (GD3Header.Version<MINGD3VERSION) ||              // below min ver
    ((GD3Header.Version & REQUIREDGD3MAJORVER)!=REQUIREDGD3MAJORVER)// != required major ver
  ) {
    gzclose(fh);
    return;
  }
  p=malloc(GD3Header.Length*2);  // Allocate memory for string data (x2 for Unicode)
  i=gzread(fh,p,GD3Header.Length*2);  // Read it in
  gzclose(fh);

  if (i<sizeof(GD3Header)) return;

  GD3string=p;

  f=fopen(TextFileName,"wb");
  // Put start text
  fputs(
    #include "htmlbefore.txt"
    ,f);

  fprintf(f,"<tr><td class=what>Length</td><td colspan=2 class=is>%s",printtime(tempstr,VGMHeader.TotalLength/44100.0));
  if(VGMHeader.LoopLength>0) {
    if(VGMHeader.TotalLength-VGMHeader.LoopLength>22050) { // ignore intros less than 0.5s since they're probably nothing
      fprintf(f," (%s intro",printtime(tempstr,(VGMHeader.TotalLength-VGMHeader.LoopLength)/44100.0));
      fprintf(f," and %s loop)",printtime(tempstr,VGMHeader.LoopLength/44100.0));
    } else
      fprintf(f," (looped)");
  } else {
    fprintf(f," (no loop)");
  }
  fprintf(f,"</td></tr>");

  // file size
  {
    int filesize;
    HANDLE fh;
    
    fh=CreateFile(filename,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    filesize=GetFileSize(fh,NULL);
    CloseHandle(fh);

    fprintf(f,"<tr><td class=what>Size</td><td class=is colspan=2>%d bytes (%d bps)</td></tr>",filesize,(int)(filesize*8.0/(VGMHeader.TotalLength/44100.0)));
  }

  for (i=0;i<8;++i) {
    if (i%2-1) fprintf(f,"<tr><td class=what>%s</td><td class=is>",What[i]);
    if (wcslen(GD3string))  // have a string
      for (j=0;j<(int)wcslen(GD3string);++j)
        fprintf(f,"&#%d;",*(GD3string+j));
    else
      fputs("&nbsp;",f);

    if (i%2)
      fputs("</td></tr>",f);
    else
      fputs("</td><td class=is>",f);

    GD3string+=wcslen(GD3string)+1;
  }
  for (i=8;i<10;++i) {
    fprintf(f,"<tr><td class=what>%s</td><td colspan=2 class=is>",What[i]);
    if (wcslen(GD3string))  // have a string
      for (j=0;j<(int)wcslen(GD3string);++j)
        fprintf(f,"&#%d;",*(GD3string+j));
    else
      fputs("&nbsp;",f);
    fputs("</td></tr>",f);
    GD3string+=wcslen(GD3string)+1;
  }
  fputs("<tr><td class=what>Notes</td><td colspan=2 class=is>",f);

  for (j=0;j<(int)wcslen(GD3string);++j) {
    if (*(GD3string+j)=='\n')
      fprintf(f,"<br>");
    else
      fprintf(f,"&#%d;",*(GD3string+j));
  }

  fputs("</td></tr>",f);

  // Try for the pack readme
  {
    char *readme;
    char *fn;
    char *p;
    FILE *freadme;
    char *buffer;
    BOOL success = FALSE;

    readme=malloc(strlen(filename)+16); // plenty of space in all weird cases

    if(readme) {
      p=strrchr(filename,'\\'); // p points to the last \ in the filename
      if(p){
        // isolate filename
        fn=malloc(strlen(p));
        if(fn) {
          strcpy(fn,p+1);

          while(!success) {
            p=strstr(p," - "); // find first " - " after current position of p
            if(p) {
              strncpy(readme,filename,p-filename); // copy filename up until the " - "
              strcpy(readme+(p-filename),".txt"); // terminate it with a ".txt\0"

              freadme=fopen(readme,"r"); // try to open file
              if(freadme) {
                success = TRUE;
                fprintf(f,"<tr><td class=what>Pack readme</td><td class=is colspan=2><textarea id=readme cols=50 rows=20 readonly>");
                buffer=malloc(1024);
                while (fgets(buffer,1024,freadme))
                  fputs(buffer,f);                  
                fprintf(f,"</textarea></td></tr>");
                fclose(freadme);
              }
              p++; // make it not find this " - " next iteration
            } else break;
          }
          free(fn);
        }
      }
      free(readme);
    }
  }

  fputs("</table>",f);

  fputs(
    #include "htmlafter.txt"
    ,f
  );
  fclose(f);

  free(p);

  if (UseMB) {
    url=malloc(strlen(TextFileName)+9);
    strcpy(url,"file:///");
    strcat(url,TextFileName);
    if (ForceOpen) SendMessage(mod.hMainWindow,WM_USER,(WPARAM)NULL,241);  // open minibrowser
    SendMessage(mod.hMainWindow,WM_USER,(WPARAM)url,241);  // display file
    free(url);
  }
  else ShellExecute(mod.hMainWindow,NULL,TextFileName,NULL,NULL,SW_SHOWNORMAL);
}*/

/*void UpdateIfPlaying() {
  if (ImmediateUpdate
    &&(mod.outMod)
    &&(mod.outMod->IsPlaying())
  ) setoutputtime(getoutputtime());
}*/

/* some ancient C newsgroup FAQ had this */
double gaussrand()
{
	static double V1, V2, S;
	static int phase = 0;
	double X;

	if(phase == 0) {
		do {
			double U1 = (double)rand() / RAND_MAX;
			double U2 = (double)rand() / RAND_MAX;

			V1 = 2 * U1 - 1;
			V2 = 2 * U2 - 1;
			S = V1 * V1 + V2 * V2;
			} while(S >= 1 || S == 0);

		X = V1 * sqrt(-2 * log(S) / S);
	} else
		X = V2 * sqrt(-2 * log(S) / S);

	phase = 1 - phase;

	return X;
}

int random_stereo()
{
  // return a random integer from 0 to 255 for stereo
  // Normal distribution, mean 127, S.D. 50
  int n = (int)(127 + gaussrand() * 50);
  if ( n > 255 ) n = 254;
  if ( n < 0 ) n = 0;
  return n;
}

//-----------------------------------------------------------------
// Configuration dialogue
//-----------------------------------------------------------------
/*#define NumCfgTabChildWnds 5
HWND CfgTabChildWnds[NumCfgTabChildWnds];  // Holds child windows' HWnds
// Defines to make it easier to place stuff where I want
#define CfgPlayback CfgTabChildWnds[0]
#define CfgTags     CfgTabChildWnds[1]
#define CfgPSG      CfgTabChildWnds[2]
#define Cfg2413     CfgTabChildWnds[3]
#define CfgOthers   CfgTabChildWnds[4]
// Dialogue box tabsheet handler
BOOL CALLBACK ConfigDialogProc(HWND DlgWin,UINT wMessage,WPARAM wParam,LPARAM lParam);
void MakeTabbedDialogue(HWND hWndMain) {
  // Variables
  TC_ITEM NewTab;
  HWND TabCtrlWnd=GetDlgItem(hWndMain,tcMain);
  RECT TabDisplayRect,TabRect;
  HIMAGELIST il;
  int i;
  // Load images for tabs
  InitCommonControls(); // required before doing imagelist stuff
  il=ImageList_LoadImage(PluginhInst,(LPCSTR)tabicons,16,0,RGB(255,0,255),IMAGE_BITMAP,LR_CREATEDIBSECTION);
  TabCtrl_SetImageList(TabCtrlWnd,il);
  // Add tabs
	NewTab.mask=TCIF_TEXT | TCIF_IMAGE;
  NewTab.pszText="Playback";
  NewTab.iImage=0;
  TabCtrl_InsertItem(TabCtrlWnd,0,&NewTab);
  NewTab.pszText="GD3 tags";
  NewTab.iImage=1;
  TabCtrl_InsertItem(TabCtrlWnd,1,&NewTab);
  NewTab.pszText="PSG";
  NewTab.iImage=2;
  TabCtrl_InsertItem(TabCtrlWnd,2,&NewTab);
  NewTab.pszText="YM2413";
  NewTab.iImage=3;
  TabCtrl_InsertItem(TabCtrlWnd,3,&NewTab);
  NewTab.pszText="YM2612/YM2151";
  NewTab.iImage=4;
  TabCtrl_InsertItem(TabCtrlWnd,4,&NewTab);
  // Get display rect
  GetWindowRect(TabCtrlWnd,&TabDisplayRect);
  GetWindowRect(hWndMain,&TabRect);
  OffsetRect(&TabDisplayRect,-TabRect.left-GetSystemMetrics(SM_CXDLGFRAME),-TabRect.top-GetSystemMetrics(SM_CYDLGFRAME)-GetSystemMetrics(SM_CYCAPTION));
  TabCtrl_AdjustRect(TabCtrlWnd,FALSE,&TabDisplayRect);
  
  // Create child windows (resource hog, I don't care)
  CfgPlayback =CreateDialog(PluginhInst,(LPCTSTR) DlgCfgPlayback,hWndMain,ConfigDialogProc);
  CfgTags     =CreateDialog(PluginhInst,(LPCTSTR) DlgCfgTags,    hWndMain,ConfigDialogProc);
  CfgPSG      =CreateDialog(PluginhInst,(LPCTSTR) DlgCfgPSG,     hWndMain,ConfigDialogProc);
  Cfg2413     =CreateDialog(PluginhInst,(LPCTSTR) DlgCfgYM2413,  hWndMain,ConfigDialogProc);
  CfgOthers   =CreateDialog(PluginhInst,(LPCTSTR) DlgCfgOthers,  hWndMain,ConfigDialogProc);
  // Enable WinXP styles
  {
    HINSTANCE dllinst=LoadLibrary("uxtheme.dll");
    if (dllinst) {
      FARPROC EnableThemeDialogTexture    =GetProcAddress(dllinst,"EnableThemeDialogTexture"),
              SetThemeAppProperties       =GetProcAddress(dllinst,"GetThemeAppProperties"),
              IsThemeDialogTextureEnabled =GetProcAddress(dllinst,"IsThemeDialogTextureEnabled");
      // I try to test if the app is in a themed XP but without a manifest to allow control theming, but the one which
      // should tell me (GetThemeAppProperties) returns STAP_ALLOW_CONTROLS||STAP_ALLOW_NONCLIENT when it should only return
      // STAP_ALLOW_NONCLIENT (as I understand it). None of the other functions help either :(
      if (
        (IsThemeDialogTextureEnabled)&&(EnableThemeDialogTexture)&& // All functions found
        (IsThemeDialogTextureEnabled(hWndMain))) { // and app is themed
        for (i=0;i<NumCfgTabChildWnds;++i) EnableThemeDialogTexture(CfgTabChildWnds[i],6); // then draw pages with theme texture
        }
      FreeLibrary(dllinst);
    }
  }

  // Put them in the right place, and hide them
  for (i=0;i<NumCfgTabChildWnds;++i) 
    SetWindowPos(CfgTabChildWnds[i],HWND_TOP,TabDisplayRect.left,TabDisplayRect.top,TabDisplayRect.right-TabDisplayRect.left,TabDisplayRect.bottom-TabDisplayRect.top,SWP_HIDEWINDOW);
  // Show the first one, though
  SetWindowPos(CfgTabChildWnds[0],HWND_TOP,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE|SWP_SHOWWINDOW);
}

// Dialogue box callback function
BOOL CALLBACK ConfigDialogProc(HWND DlgWin,UINT wMessage,WPARAM wParam,LPARAM lParam) {
  switch (wMessage) {  // process messages
    case WM_INITDIALOG: { // Initialise dialogue
      int i;
      if (GetWindowLong(DlgWin,GWL_STYLE)&WS_CHILD) return FALSE;
      MakeTabbedDialogue(DlgWin);

      SetWindowText(DlgWin,PLUGINNAME " configuration");

      // Playback tab -----------------------------------------------------------
#define WND CfgPlayback
      // Set loop count
      SetDlgItemInt(WND,ebLoopCount,NumLoops,FALSE);
      // Set fadeout length
      SetDlgItemInt(WND,ebFadeOutLength,LoopingFadeOutms,FALSE);
      // Set between-track pause length
      SetDlgItemInt(WND,ebPauseLength,PauseBetweenTracksms,FALSE);
      // Playback rate
      switch (PlaybackRate) {
        case 0:
          CheckRadioButton(WND,rbRateOriginal,rbRateOther,rbRateOriginal);
          break;
        case 50:
          CheckRadioButton(WND,rbRateOriginal,rbRateOther,rbRate50);
          break;
        case 60:
          CheckRadioButton(WND,rbRateOriginal,rbRateOther,rbRate60);
          break;
        default:
          CheckRadioButton(WND,rbRateOriginal,rbRateOther,rbRateOther);
          break;
      }
      EnableWindow(GetDlgItem(WND,ebPlaybackRate),(IsDlgButtonChecked(WND,rbRateOther)?TRUE:FALSE));
      if (PlaybackRate!=0) {
        char tempstr[18];  // buffer for itoa
        SetDlgItemText(WND,ebPlaybackRate,itoa(PlaybackRate,tempstr,10));
      } else {
        SetDlgItemText(WND,ebPlaybackRate,"60");
      }
      // Volume overdrive
      CheckDlgButton(WND,cbOverDrive,Overdrive);
      // Persistent muting checkbox
      CheckDlgButton(WND,cbMutePersistent,MutePersistent);
      // randomise panning checkbox
      CheckDlgButton(WND,cbRandomisePanning,RandomisePanning);
#undef WND

      // GD3 tab ----------------------------------------------------------------
#define WND CfgTags
      // Set title format text
      SetDlgItemText(WND,ebTrackTitle,TrackTitleFormat);
      // Media Library options
      CheckDlgButton(WND,cbMLJapanese,MLJapanese);
      CheckDlgButton(WND,cbMLShowFM,MLShowFM);
      SetDlgItemInt(WND,ebMLType,MLType,FALSE);
      // Now Playing settings
      CheckDlgButton(WND,cbUseMB        ,UseMB);
      CheckDlgButton(WND,cbAutoMB       ,AutoMB);
      CheckDlgButton(WND,cbForceMBOpen  ,ForceMBOpen);
      EnableWindow(GetDlgItem(WND,cbAutoMB     ),UseMB);
      EnableWindow(GetDlgItem(WND,cbForceMBOpen),UseMB & AutoMB);
#undef WND

      // PSG tab ----------------------------------------------------------------
#define WND CfgPSG
      if (PSGClock) { // Check PSG channel checkboxes
        for (i=0;i<SN76489_NUM_CHANNELS;i++) CheckDlgButton(WND,cbTone1+i,((SN76489_Mute & (1<<i))>0));
      } else {  // or disable them
        for (i=0;i<SN76489_NUM_CHANNELS;i++) EnableWindow(GetDlgItem(WND,cbTone1+i),FALSE);
        EnableWindow(GetDlgItem(WND,btnRandomPSG),FALSE);
        EnableWindow(GetDlgItem(WND,btnCentrePSG),FALSE);
        EnableWindow(GetDlgItem(WND,slSNCh0),FALSE);
        EnableWindow(GetDlgItem(WND,slSNCh1),FALSE);
        EnableWindow(GetDlgItem(WND,slSNCh2),FALSE);
        EnableWindow(GetDlgItem(WND,slSNCh3),FALSE);
      }
      CheckDlgButton(WND,cbSmoothPSGVolume,SN76489_VolumeArray);
      // Panning
      SendDlgItemMessage(CfgPSG,slSNCh0,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(CfgPSG,slSNCh1,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(CfgPSG,slSNCh2,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(CfgPSG,slSNCh3,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(CfgPSG,slSNCh0,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(CfgPSG,slSNCh1,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(CfgPSG,slSNCh2,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(CfgPSG,slSNCh3,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(CfgPSG,slSNCh0,TBM_SETPOS,TRUE,SN76489_Pan[0]);
      SendDlgItemMessage(CfgPSG,slSNCh1,TBM_SETPOS,TRUE,SN76489_Pan[1]);
      SendDlgItemMessage(CfgPSG,slSNCh2,TBM_SETPOS,TRUE,SN76489_Pan[2]);
      SendDlgItemMessage(CfgPSG,slSNCh3,TBM_SETPOS,TRUE,SN76489_Pan[3]);
#undef WND

      // YM2413 tab -------------------------------------------------------------
#define WND Cfg2413
      if USINGCHIP(FM_YM2413) {  // Check YM2413 FM channel checkboxes
        for (i=0;i<YM2413_NUM_CHANNELS;i++) CheckDlgButton(WND,cbYM24131+i,!((YM2413Channels & (1<<i))>0));
        CheckDlgButton(WND,cbYM2413ToneAll,((YM2413Channels&0x1ff )==0));
        CheckDlgButton(WND,cbYM2413PercAll,((YM2413Channels&0x3e00)==0));
      } else {
        for (i=0;i<YM2413_NUM_CHANNELS;i++) EnableWindow(GetDlgItem(WND,cbYM24131+i),FALSE);
        EnableWindow(GetDlgItem(WND,cbYM2413ToneAll),FALSE);
        EnableWindow(GetDlgItem(WND,cbYM2413PercAll),FALSE);
        EnableWindow(GetDlgItem(WND,lblExtraTone),FALSE);
        EnableWindow(GetDlgItem(WND,lblExtraToneNote),FALSE);
        EnableWindow(GetDlgItem(WND,gbYM2413),FALSE);
        EnableWindow(GetDlgItem(WND,sl2413ch1),FALSE);
        EnableWindow(GetDlgItem(WND,sl2413ch2),FALSE);
        EnableWindow(GetDlgItem(WND,sl2413ch3),FALSE);
        EnableWindow(GetDlgItem(WND,sl2413ch4),FALSE);
        EnableWindow(GetDlgItem(WND,sl2413ch5),FALSE);
        EnableWindow(GetDlgItem(WND,sl2413ch6),FALSE);
        EnableWindow(GetDlgItem(WND,sl2413ch7),FALSE);
        EnableWindow(GetDlgItem(WND,sl2413ch8),FALSE);
        EnableWindow(GetDlgItem(WND,sl2413ch9),FALSE);
        EnableWindow(GetDlgItem(WND,sl2413hh ),FALSE);
        EnableWindow(GetDlgItem(WND,sl2413cym),FALSE);
        EnableWindow(GetDlgItem(WND,sl2413tt ),FALSE);
        EnableWindow(GetDlgItem(WND,sl2413sd ),FALSE);
        EnableWindow(GetDlgItem(WND,sl2413bd ),FALSE);
        EnableWindow(GetDlgItem(WND,btnCentre2413),FALSE);
        EnableWindow(GetDlgItem(WND,btnRandom2413),FALSE);
      }                             
      // Panning
      SendDlgItemMessage(Cfg2413,sl2413ch1,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(Cfg2413,sl2413ch2,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(Cfg2413,sl2413ch3,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(Cfg2413,sl2413ch4,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(Cfg2413,sl2413ch5,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(Cfg2413,sl2413ch6,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(Cfg2413,sl2413ch7,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(Cfg2413,sl2413ch8,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(Cfg2413,sl2413ch9,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(Cfg2413,sl2413hh ,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(Cfg2413,sl2413cym,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(Cfg2413,sl2413tt ,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(Cfg2413,sl2413sd ,TBM_SETRANGE,0,MAKELONG(0,254));
      SendDlgItemMessage(Cfg2413,sl2413bd ,TBM_SETRANGE,0,MAKELONG(0,254));

      SendDlgItemMessage(Cfg2413,sl2413ch1,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(Cfg2413,sl2413ch2,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(Cfg2413,sl2413ch3,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(Cfg2413,sl2413ch4,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(Cfg2413,sl2413ch5,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(Cfg2413,sl2413ch6,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(Cfg2413,sl2413ch7,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(Cfg2413,sl2413ch8,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(Cfg2413,sl2413ch9,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(Cfg2413,sl2413hh ,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(Cfg2413,sl2413cym,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(Cfg2413,sl2413tt ,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(Cfg2413,sl2413sd ,TBM_SETTICFREQ,127,0);
      SendDlgItemMessage(Cfg2413,sl2413bd ,TBM_SETTICFREQ,127,0);

      SendDlgItemMessage(Cfg2413,sl2413ch1,TBM_SETPOS,TRUE,YM2413_Pan[0]);
      SendDlgItemMessage(Cfg2413,sl2413ch2,TBM_SETPOS,TRUE,YM2413_Pan[1]);
      SendDlgItemMessage(Cfg2413,sl2413ch3,TBM_SETPOS,TRUE,YM2413_Pan[2]);
      SendDlgItemMessage(Cfg2413,sl2413ch4,TBM_SETPOS,TRUE,YM2413_Pan[3]);
      SendDlgItemMessage(Cfg2413,sl2413ch5,TBM_SETPOS,TRUE,YM2413_Pan[4]);
      SendDlgItemMessage(Cfg2413,sl2413ch6,TBM_SETPOS,TRUE,YM2413_Pan[5]);
      SendDlgItemMessage(Cfg2413,sl2413ch7,TBM_SETPOS,TRUE,YM2413_Pan[6]);
      SendDlgItemMessage(Cfg2413,sl2413ch8,TBM_SETPOS,TRUE,YM2413_Pan[7]);
      SendDlgItemMessage(Cfg2413,sl2413ch9,TBM_SETPOS,TRUE,YM2413_Pan[8]);
      SendDlgItemMessage(Cfg2413,sl2413hh ,TBM_SETPOS,TRUE,YM2413_Pan[9]);
      SendDlgItemMessage(Cfg2413,sl2413cym,TBM_SETPOS,TRUE,YM2413_Pan[10]);
      SendDlgItemMessage(Cfg2413,sl2413tt ,TBM_SETPOS,TRUE,YM2413_Pan[11]);
      SendDlgItemMessage(Cfg2413,sl2413sd ,TBM_SETPOS,TRUE,YM2413_Pan[12]);
      SendDlgItemMessage(Cfg2413,sl2413bd ,TBM_SETPOS,TRUE,YM2413_Pan[13]);

      CheckDlgButton(WND,cbYM2413HiQ,YM2413HiQ);
#undef WND

      // Other chips tab -------------------------------------------------------
#define WND CfgOthers
      if USINGCHIP(FM_YM2612) {  // Check YM2612 FM channel checkboxes
        CheckDlgButton(WND,cbYM2612All,(YM2612Channels==0));
      } else {
        EnableWindow(GetDlgItem(WND,cbYM2612All),FALSE);
        EnableWindow(GetDlgItem(WND,gbYM2612),FALSE);
      }
      if USINGCHIP(FM_YM2151) {  // Check YM2151 FM channel checkboxes
        CheckDlgButton(WND,cbYM2151All,(YM2151Channels==0));
      } else {
        EnableWindow(GetDlgItem(WND,cbYM2151All),FALSE);
        EnableWindow(GetDlgItem(WND,gbYM2151),FALSE);
      }
#undef WND

      // Stuff not in tabs ------------------------------------------------------
      // Immediate update checkbox
      CheckDlgButton(DlgWin,cbMuteImmediate,ImmediateUpdate);
      

      SetFocus(DlgWin);

      return (TRUE);
    }
    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        case IDOK: {    // OK button
          int i;
          BOOL MyBool;

          // Playback tab ------------------------------------------------------
          // Loop count
          i=GetDlgItemInt(CfgPlayback,ebLoopCount,&MyBool,FALSE);
          if (MyBool) NumLoops=i;
          // Fadeout length
          i=GetDlgItemInt(CfgPlayback,ebFadeOutLength,&MyBool,FALSE);
          if (MyBool) LoopingFadeOutms=i;
          // Between-track pause length
          i=GetDlgItemInt(CfgPlayback,ebPauseLength,&MyBool,FALSE);
          if (MyBool) PauseBetweenTracksms=i;
          // Playback rate
          PlaybackRate=0;
          if (IsDlgButtonChecked(CfgPlayback,rbRate50)) {
            PlaybackRate=50;
          } else if (IsDlgButtonChecked(CfgPlayback,rbRate60)) {
            PlaybackRate=60;
          } else if (IsDlgButtonChecked(CfgPlayback,rbRateOther)) {
            i=GetDlgItemInt(CfgPlayback,ebPlaybackRate,&MyBool,TRUE);
            if ((MyBool) && (i>0) && (i<=6000)) PlaybackRate=i;
          }
          // Persistent muting checkbox
          MutePersistent=IsDlgButtonChecked(CfgPlayback,cbMutePersistent);
          // Randomise panning checkbox
          RandomisePanning=IsDlgButtonChecked(CfgPlayback,cbRandomisePanning);

          // Tags tab ----------------------------------------------------------
          // Track title format
          GetDlgItemText(CfgTags,ebTrackTitle,TrackTitleFormat,100);
          // Media Library options
          MLJapanese=IsDlgButtonChecked(CfgTags,cbMLJapanese);
          MLShowFM  =IsDlgButtonChecked(CfgTags,cbMLShowFM);
          i=GetDlgItemInt(CfgTags,ebMLType,&MyBool,FALSE);
          if (MyBool) MLType=i;


        }
        EndDialog(DlgWin,0);
        return (TRUE);
      case IDCANCEL:  // [X] button, Alt+F4, etc
        EndDialog(DlgWin,1);
        return (TRUE) ;

      // PSG tab -------------------------------------------------------------------
      case cbTone1: case cbTone2: case cbTone3: case cbTone4:
        SN76489_Mute=
           (IsDlgButtonChecked(CfgPSG,cbTone1)     )
          |(IsDlgButtonChecked(CfgPSG,cbTone2) << 1)
          |(IsDlgButtonChecked(CfgPSG,cbTone3) << 2)
          |(IsDlgButtonChecked(CfgPSG,cbTone4) << 3);
        SN76489_SetMute(0,SN76489_Mute);
        UpdateIfPlaying();
        break;
      case cbSmoothPSGVolume:
        SN76489_VolumeArray=IsDlgButtonChecked(CfgPSG,cbSmoothPSGVolume);
        SN76489_SetVolType(0,SN76489_VolumeArray);
        UpdateIfPlaying();
        break;
      case btnCentrePSG: // centre PSG fake stereo sliders
        SendDlgItemMessage(CfgPSG,slSNCh0,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(CfgPSG,slSNCh1,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(CfgPSG,slSNCh2,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(CfgPSG,slSNCh3,TBM_SETPOS,TRUE,127);
        SendMessage(DlgWin,WM_HSCROLL,TB_ENDTRACK,0);
        break;
      case btnRandomPSG: // randomise PSG fake stereo sliders
        SendDlgItemMessage(CfgPSG,slSNCh0,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(CfgPSG,slSNCh1,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(CfgPSG,slSNCh2,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(CfgPSG,slSNCh3,TBM_SETPOS,TRUE,random_stereo());
        SendMessage(DlgWin,WM_HSCROLL,TB_ENDTRACK,0);
        break;

       
       // YM2413 tab ----------------------------------------------------------------
      case cbYM24131:   case cbYM24132:   case cbYM24133:   case cbYM24134:
      case cbYM24135:   case cbYM24136:   case cbYM24137:   case cbYM24138:
      case cbYM24139:   case cbYM241310:  case cbYM241311:  case cbYM241312:
      case cbYM241313:  case cbYM241314: {
        int i;
        YM2413Channels=0;
        for (i=0;i<YM2413_NUM_CHANNELS;i++) YM2413Channels|=(!IsDlgButtonChecked(Cfg2413,cbYM24131+i))<<i;
        if USINGCHIP(FM_YM2413) {
          EMU2413_OPLL_setMask(opll,YM2413Channels);
          UpdateIfPlaying();
        }
        CheckDlgButton(Cfg2413,cbYM2413ToneAll,((YM2413Channels&0x1ff )==0));
        CheckDlgButton(Cfg2413,cbYM2413PercAll,((YM2413Channels&0x3e00)==0));
        break;
      }
      case cbYM2413ToneAll: {
        int i;
        const int Checked=IsDlgButtonChecked(Cfg2413,cbYM2413ToneAll);
        for (i=0;i<9;i++) CheckDlgButton(Cfg2413,cbYM24131+i,Checked);
        PostMessage(Cfg2413,WM_COMMAND,cbYM24131,0);
        break;
      }
      case cbYM2413PercAll: {
        int i;
        const int Checked=IsDlgButtonChecked(Cfg2413,cbYM2413PercAll);
        for (i=0;i<5;i++) CheckDlgButton(Cfg2413,cbYM241310+i,Checked);
        PostMessage(Cfg2413,WM_COMMAND,cbYM24131,0);
        break;
      }
      case cbYM2413HiQ:
        YM2413HiQ=IsDlgButtonChecked(Cfg2413,cbYM2413HiQ);
        if USINGCHIP(FM_YM2413) {
          EMU2413_OPLL_set_quality(opll,YM2413HiQ);
          UpdateIfPlaying();
        }
        break;
      case btnCentre2413: // centre YM2413 fake stereo sliders
        SendDlgItemMessage(Cfg2413,sl2413ch1,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch2,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch3,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch4,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch5,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch6,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch7,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch8,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch9,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413hh ,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413cym,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413tt ,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413sd ,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413bd ,TBM_SETPOS,TRUE,127);
        SendMessage(DlgWin,WM_HSCROLL,TB_ENDTRACK,0);
        break;
      case btnRandom2413: // randomise YM2413 fake stereo sliders
        SendDlgItemMessage(Cfg2413,sl2413ch1,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch2,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch3,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch4,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch5,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch6,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch7,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch8,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch9,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413hh ,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413cym,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413tt ,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413sd ,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413bd ,TBM_SETPOS,TRUE,random_stereo());
        SendMessage(DlgWin,WM_HSCROLL,TB_ENDTRACK,0);
        break;

      // Playback tab --------------------------------------------------------------
      case rbRateOriginal:
      case rbRate50:
      case rbRate60:
      case rbRateOther:
        CheckRadioButton(CfgPlayback,rbRateOriginal,rbRateOther,LOWORD(wParam));
        EnableWindow(GetDlgItem(CfgPlayback,ebPlaybackRate),((LOWORD(wParam)==rbRateOther)?TRUE:FALSE));
        if (LOWORD(wParam)==rbRateOther) SetFocus(GetDlgItem(CfgPlayback,ebPlaybackRate));
        break;
      case cbOverDrive:
        Overdrive=IsDlgButtonChecked(CfgPlayback,cbOverDrive);
        UpdateIfPlaying();
        break;

      // Tags tab -----------------------------------------------------------------
      case cbUseMB:
        UseMB=IsDlgButtonChecked(CfgTags,cbUseMB);
        EnableWindow(GetDlgItem(CfgTags,cbAutoMB     ),UseMB);
        EnableWindow(GetDlgItem(CfgTags,cbForceMBOpen),UseMB & AutoMB);
        break;
      case cbAutoMB:
        AutoMB=IsDlgButtonChecked(CfgTags,cbAutoMB);
        EnableWindow(GetDlgItem(CfgTags,cbForceMBOpen),UseMB & AutoMB);
        break;
      case cbForceMBOpen:
        ForceMBOpen=IsDlgButtonChecked(CfgTags,cbForceMBOpen);
        break;

      // Others tab
      case cbYM2612All:
        YM2612Channels=!IsDlgButtonChecked(CfgOthers,cbYM2612All);
        UpdateIfPlaying();
        break;
      case cbYM2151All:
        YM2151Channels=!IsDlgButtonChecked(CfgOthers,cbYM2151All);
        UpdateIfPlaying();
        break;

      // Stuff not in tabs --------------------------------------------------------
      case cbMuteImmediate:
        ImmediateUpdate=IsDlgButtonChecked(DlgWin,cbMuteImmediate);
        UpdateIfPlaying();
        break;
      case btnReadMe: {
          char FileName[MAX_PATH];
          char *PChar;
          GetModuleFileName(PluginhInst,FileName,MAX_PATH);  // get *dll* path
          GetFullPathName(FileName,MAX_PATH,FileName,&PChar);  // make it fully qualified plus find the filename bit
          strcpy(PChar,"in_vgm.txt");
          if ((int)ShellExecute(mod.hMainWindow,NULL,FileName,NULL,NULL,SW_SHOWNORMAL)<=32)
            MessageBox(DlgWin,"Error opening in_vgm.txt from plugin folder",mod.description,MB_ICONERROR+MB_OK);
        }
        break;
      }
    break; // end WM_COMMAND handling

    case WM_HSCROLL: // trackbar notifications
    {
      int i;
      // Get PSG panning controls
      SN76489_Pan[0]=SendDlgItemMessage(CfgPSG,slSNCh0,TBM_GETPOS,0,0);
      SN76489_Pan[1]=SendDlgItemMessage(CfgPSG,slSNCh1,TBM_GETPOS,0,0);
      SN76489_Pan[2]=SendDlgItemMessage(CfgPSG,slSNCh2,TBM_GETPOS,0,0);
      SN76489_Pan[3]=SendDlgItemMessage(CfgPSG,slSNCh3,TBM_GETPOS,0,0);
      if (PSGClock)
        SN76489_SetPanning(0,SN76489_Pan[0],SN76489_Pan[1],SN76489_Pan[2],SN76489_Pan[3]);

      // Get YM2413 panning controls
      YM2413_Pan[ 0]=SendDlgItemMessage(Cfg2413,sl2413ch1,TBM_GETPOS,0,0);
      YM2413_Pan[ 1]=SendDlgItemMessage(Cfg2413,sl2413ch2,TBM_GETPOS,0,0);
      YM2413_Pan[ 2]=SendDlgItemMessage(Cfg2413,sl2413ch3,TBM_GETPOS,0,0);
      YM2413_Pan[ 3]=SendDlgItemMessage(Cfg2413,sl2413ch4,TBM_GETPOS,0,0);
      YM2413_Pan[ 4]=SendDlgItemMessage(Cfg2413,sl2413ch5,TBM_GETPOS,0,0);
      YM2413_Pan[ 5]=SendDlgItemMessage(Cfg2413,sl2413ch6,TBM_GETPOS,0,0);
      YM2413_Pan[ 6]=SendDlgItemMessage(Cfg2413,sl2413ch7,TBM_GETPOS,0,0);
      YM2413_Pan[ 7]=SendDlgItemMessage(Cfg2413,sl2413ch8,TBM_GETPOS,0,0);
      YM2413_Pan[ 8]=SendDlgItemMessage(Cfg2413,sl2413ch9,TBM_GETPOS,0,0);
      YM2413_Pan[ 9]=SendDlgItemMessage(Cfg2413,sl2413hh ,TBM_GETPOS,0,0);
      YM2413_Pan[10]=SendDlgItemMessage(Cfg2413,sl2413cym,TBM_GETPOS,0,0);
      YM2413_Pan[11]=SendDlgItemMessage(Cfg2413,sl2413tt ,TBM_GETPOS,0,0);
      YM2413_Pan[12]=SendDlgItemMessage(Cfg2413,sl2413sd ,TBM_GETPOS,0,0);
      YM2413_Pan[13]=SendDlgItemMessage(Cfg2413,sl2413bd ,TBM_GETPOS,0,0);
      if (USINGCHIP(FM_YM2413))
        for ( i=0; i< YM2413_NUM_CHANNELS; ++i )
          EMU2413_OPLL_set_pan( opll, i, YM2413_Pan[i] );

      if((LOWORD(wParam)==TB_THUMBPOSITION)||(LOWORD(wParam)==TB_ENDTRACK)) UpdateIfPlaying();
      break;
    }
    case WM_NOTIFY:
      switch (LOWORD(wParam)) {
        case tcMain:
          switch (((LPNMHDR)lParam)->code) {
            case TCN_SELCHANGING:  // hide current window
              SetWindowPos(CfgTabChildWnds[TabCtrl_GetCurSel(GetDlgItem(DlgWin,tcMain))],HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_HIDEWINDOW);
              break;
            case TCN_SELCHANGE:  // show current window
              {
                int i=TabCtrl_GetCurSel(GetDlgItem(DlgWin,tcMain));
                SetWindowPos(CfgTabChildWnds[i],HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
//							SetFocus(CfgTabChildWnds[i]);
              }
              break;
          }
          break;
      }
      return TRUE;
  }
  return (FALSE) ;    // return FALSE to signify message not processed
}

void config(HWND hwndParent) {
  DialogBox(mod.hDllInstance, "CONFIGDIALOGUE", hwndParent, ConfigDialogProc);
}

//-----------------------------------------------------------------
// About dialogue
//-----------------------------------------------------------------
void about(HWND hwndParent)
{
  MessageBox(hwndParent,
    PLUGINNAME "\n\n"
    "Build date: "__DATE__"\n\n"
    "by Maxim in 2001, 2002, 2003 and 2004\n"
    "maxim@mwos.cjb.net\n"
    "http://www.smspower.org/music\n\n"
    "Current status:\n"
    "PSG - emulated as a perfect device, leading to slight differences in sound compared to the real thing. Noise pattern is 100% accurate, unlike almost every other core out there :P\n"
    "YM2413 - via EMU2413 0.61 (Mitsutaka Okazaki) (http://www.angel.ne.jp/~okazaki/ym2413).\n"
    "YM2612 - via Gens 2.10 core (Stephane Dallongeville) (http://gens.consolemul.com).\n"
    "YM2151 - via MAME FM core (Jarek Burczynski, Hiro-shi), thanks to BlackAura.\n\n"
    "Don\'t be put off by the pre-1.0 version numbers. This is a non-commercial project and as such it is permanently in beta.\n\n"
    "Thanks also go to:\n"
    "Bock, Heliophobe, Mike G, Steve Snake, Dave, Charles MacDonald, Ville Helin, John Kortink, fx^, DukeNukem\n\n"
    "  ...and Zhao Yuehua xxx wo ai ni"
    ,mod.description,MB_ICONINFORMATION|MB_OK);
}*/

//-----------------------------------------------------------------
// Initialisation (one-time)
//-----------------------------------------------------------------
void VGM_Init() {
  int i;
/*  char INIFileName[MAX_PATH];
  char *PChar;
  char buffer[1024];

  GetModuleFileName(mod.hDllInstance,INIFileName,MAX_PATH);  // get exe path
  GetFullPathName(INIFileName,MAX_PATH,INIFileName,&PChar);  // make it fully qualified plus find the filename bit
  strcpy(PChar,"plugin.ini");  // Change to plugin.ini

  GetTempPath(MAX_PATH,TextFileName);
  strcat(TextFileName,"GD3.html");
  GetShortPathName(TextFileName,TextFileName,MAX_PATH);

  NumLoops            =GetPrivateProfileInt(INISECTION,"NumLoops"             ,2    ,INIFileName);
  LoopingFadeOutms    =GetPrivateProfileInt(INISECTION,"Fade out length"      ,5000 ,INIFileName);
  PauseBetweenTracksms=GetPrivateProfileInt(INISECTION,"Pause between tracks" ,2000 ,INIFileName);
  PlaybackRate        =GetPrivateProfileInt(INISECTION,"Playback rate"        ,0    ,INIFileName);
  FileInfoJapanese    =GetPrivateProfileInt(INISECTION,"Japanese in info box" ,0    ,INIFileName);
  UseMB               =GetPrivateProfileInt(INISECTION,"Use Minibrowser"      ,1    ,INIFileName);
  AutoMB              =GetPrivateProfileInt(INISECTION,"Auto-show HTML"       ,0    ,INIFileName);
  ForceMBOpen         =GetPrivateProfileInt(INISECTION,"Force MB open"        ,0    ,INIFileName);
  YM2413HiQ           =GetPrivateProfileInt(INISECTION,"High quality YM2413"  ,0    ,INIFileName);
  Overdrive           =GetPrivateProfileInt(INISECTION,"Overdrive"            ,1    ,INIFileName);
  ImmediateUpdate     =GetPrivateProfileInt(INISECTION,"Immediate update"     ,1    ,INIFileName);
  SN76489_VolumeArray =GetPrivateProfileInt(INISECTION,"PSG volume curve"     ,0    ,INIFileName);
  MLJapanese          =GetPrivateProfileInt(INISECTION,"ML Japanese"          ,0    ,INIFileName);
  MLShowFM            =GetPrivateProfileInt(INISECTION,"ML show FM"           ,1    ,INIFileName);
  MLType              =GetPrivateProfileInt(INISECTION,"ML type"              ,0    ,INIFileName);

  GetPrivateProfileString(INISECTION,"Title format","%t (%g) - %a",TrackTitleFormat,100,INIFileName);*/

  for ( i = 0; i < SN76489_NUM_CHANNELS; ++i ) SN76489_Pan[i] = 127;
  for ( i = 0; i < YM2413_NUM_CHANNELS; ++i ) YM2413_Pan[i] = 127;
  pcm_data = NULL;
}

//-----------------------------------------------------------------
// Deinitialisation (one-time)
//-----------------------------------------------------------------
void quit() {
/*  char INIFileName[MAX_PATH];
  char *PChar;  // pointer to INI string
  char tempstr[1024];  // buffer for itoa

  GetModuleFileName(mod.hDllInstance,INIFileName,MAX_PATH);  // get exe path
  GetFullPathName(INIFileName,MAX_PATH,INIFileName,&PChar);  // make it fully qualified plus find the filename bit
  strcpy(PChar,"plugin.ini");  // Change to plugin.ini

  WritePrivateProfileString(INISECTION,"NumLoops"            ,itoa(NumLoops             ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Fade out length"     ,itoa(LoopingFadeOutms     ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Pause between tracks",itoa(PauseBetweenTracksms ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Playback rate"       ,itoa(PlaybackRate         ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Japanese in info box",itoa(FileInfoJapanese     ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Title format"        ,TrackTitleFormat                      ,INIFileName);
  WritePrivateProfileString(INISECTION,"Use Minibrowser"     ,itoa(UseMB                ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Auto-show HTML"      ,itoa(AutoMB               ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Force MB open"       ,itoa(ForceMBOpen          ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"High quality YM2413" ,itoa(YM2413HiQ            ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Overdrive"           ,itoa(Overdrive            ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Immediate update"    ,itoa(ImmediateUpdate      ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"PSG volume curve"    ,itoa(SN76489_VolumeArray  ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"ML Japanese"         ,itoa(MLJapanese           ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"ML show FM"          ,itoa(MLShowFM             ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"ML type"             ,itoa(MLType               ,tempstr,10),INIFileName);
  sprintf(tempstr, "%d,%d,%d,%d", SN76489_Pan[0], SN76489_Pan[1], SN76489_Pan[2], SN76489_Pan[3]);
  WritePrivateProfileString(INISECTION,"SN76489 panning"     ,tempstr,INIFileName);
  sprintf(tempstr, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", YM2413_Pan[0], YM2413_Pan[1], YM2413_Pan[2], YM2413_Pan[3], YM2413_Pan[4], YM2413_Pan[5], YM2413_Pan[6], YM2413_Pan[7], YM2413_Pan[8], YM2413_Pan[9], YM2413_Pan[10], YM2413_Pan[11], YM2413_Pan[12], YM2413_Pan[13]);
  WritePrivateProfileString(INISECTION,"YM2413 panning"      ,tempstr,INIFileName);
  WritePrivateProfileString(INISECTION,"Randomise panning"   ,itoa(RandomisePanning     ,tempstr,10),INIFileName);

  DeleteFile(TextFileName);*/
}

//-----------------------------------------------------------------
// Pre-extension check file claiming
//-----------------------------------------------------------------
/*int isourfile(char *fn) {
  // First, check for URLs
  gzFile *f;
  char *p=strrchr(fn,'.');
  if ( (p) && ( (stricmp(p,".vgm")==0) || (stricmp(p,".vgz")==0) ) && IsURL(fn) ) return TRUE;

  // second, check the file itself
  f = gzopen( fn, "rb" );
  if (f) {
    unsigned long i = 0;
    gzread( f, &i, sizeof(i) );
    gzclose( f );
    if ( i == VGMIDENT )
      return TRUE;
  }

  return FALSE;
}*/

/*
//-----------------------------------------------------------------
// File download callback function
//-----------------------------------------------------------------
BOOL CALLBACK DownloadDialogProc(HWND DlgWin,UINT wMessage,WPARAM wParam,LPARAM lParam) {
    switch (wMessage) {  // process messages
    case WM_INITDIALOG:  // Initialise dialogue
      return (TRUE);
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDCANCEL:  // [X] button, Alt+F4, etc
                    EndDialog(DlgWin,1);
                    return (TRUE) ;
        }
    }
    return (FALSE);    // return FALSE to signify message not processed
}
*/

/*char *URLEncode(char *s) {
  // URL encode string s
  // caller must free returned string
  char *hexchars="0123456789ABCDEF";
  char *result=malloc(strlen(s)*3+1),*p,*q;
  
  strcpy(result,s); // fill it up to start with

  q=strstr(s,"://");

  if(!q) {
    *result=0;
    return result;
  }

  if(strchr(CurrentURL,'%')) return result; // do nothing if there's a % in it already since it's probably already URL encoded

  p=result+(q-s+2);

  for (q+=2;*q!=0;q++)
		if ((*q<'/' && *q!='-' && *q!='.') ||
		  (*q<'A' && *q>'9') ||
			(*q>'Z' && *q<'a' && *q!='_') ||
			(*q>'z')) {
        *p++='%';
        *p++=hexchars[*q>>4];
        *p++=hexchars[*q&0xf];
    } else *p++=*q;
  *p=0;

  return result;
}*/

char *getgd3string(char *buffer,int length,CFILE *f) {
  // read in values
  unsigned short c=1; // non-zero

  while(length>0) {
    c=f->readc();
    c+=(f->readc())<<8;
    if(c>0x15f) c='?';
	if (buffer)
	    *buffer++=(char)(c & 0xff);
    --length;
    if(c==0) break;
  }
  if (buffer)
	  *buffer++='\0'; // terminate string if it was cut off
  // eat extra chars up to the terminating \0\0
  while(c!=0) {
    c=f->readc();
    c+=(f->readc())<<8;
  }
  return buffer;
}


//-----------------------------------------------------------------
// Play filename
//-----------------------------------------------------------------
int VGM_play(char *fn, CFILE *r, SONGINFO * info, unsigned int flags) { 
  int maxlatency;
  int thread_id;
//  HANDLE f;
  int FileSize;
  struct TVGMHeader VGMHeader;
  int i;

//  if ((!MutePersistent) && (*lastfn) && (strcmp(fn,lastfn)!=0)) {
    // If file has changed, reset channel muting (if not blocked)
    SN76489_Mute  =SN76489_MUTE_ALLON;
    YM2413Channels=YM2413_MUTE_ALLON;
    YM2612Channels=YM2612_MUTE_ALLON;
    YM2151Channels=YM2151_MUTE_ALLON;
//  }

  // If wanted, randomise panning
  if ( RandomisePanning )
  {
    for ( i = 0; i < SN76489_NUM_CHANNELS; ++i )
      SN76489_Pan[i] = random_stereo();
    for ( i = 0; i < YM2413_NUM_CHANNELS; ++i )
      YM2413_Pan[i] = random_stereo();
  }

//  strcpy(lastfn,fn);
  
  // Blargg - free PCM data
  if (pcm_data) free( pcm_data );
  pcm_data = NULL;
  pcm_data_size = 0;
  pcm_pos = 0;

  // Get file size for bitrate calculations
  FileSize = r->get_length();

  VGMFile = r;
/*  InputFile=gzopen(fn, "rb");  // Open file - read, binary

  if (InputFile==NULL) {
    return -1;
  }  // File not opened/found, advance playlist*/

  // Read header
  i = VGMFile->read(&VGMHeader,sizeof(VGMHeader));
//  i=gzread(InputFile,&VGMHeader,sizeof(VGMHeader));

  // Check it read OK
  if (i<sizeof(VGMHeader)) {
/*    char msgstring[1024];
    sprintf(msgstring,"File too short:\n%s",fn);
    MessageBox(mod.hMainWindow,msgstring,mod.description,0);*/
//    gzclose(InputFile);
//    InputFile=NULL;
    return -1;
  }

  // Check for VGM marker
  if (VGMHeader.VGMIdent != VGMIDENT) {
/*    char msgstring[1024];
    sprintf(msgstring,"VGM marker not found in \"%s\"",fn);
    MessageBox(mod.hMainWindow,msgstring,mod.description,0);*/
//    gzclose(InputFile);
//    InputFile=NULL;
    return -1;
  }

  // Check version
  if ((VGMHeader.Version<MINVERSION) || ((VGMHeader.Version & REQUIREDMAJORVER)!=REQUIREDMAJORVER)) {
/*    char msgstring[1024];
    sprintf(msgstring,"Unsupported VGM version found in \"%s\" (%x).\n\nDo you want to try to play it anyway?",fn,VGMHeader.Version);

    if (MessageBox(mod.hMainWindow,msgstring,mod.description,MB_YESNO+MB_DEFBUTTON2)==IDNO) {*/
//      gzclose(InputFile);
//      InputFile=NULL;
      return -1;
//    }
  }

  // Fix stuff for older version files:
  // Nothing to fix for 1.01
  if ( VGMHeader.Version < 0x0110 ) {
    VGMHeader.PSGWhiteNoiseFeedback = 0x0009;
    VGMHeader.PSGShiftRegisterWidth = 16;
    VGMHeader.YM2612Clock = VGMHeader.YM2413Clock;
    VGMHeader.YM2151Clock = VGMHeader.YM2413Clock;
  }

  // handle VGM data offset
  if ( VGMHeader.VGMDataOffset == 0 )
    VGMDataOffset = 0x40;
  else
    // header has a value, but it is relative; make it absolute
    VGMDataOffset = VGMHeader.VGMDataOffset + VGMDATADELTA;

  // Get length
  if (VGMHeader.TotalLength==0) {
    TrackLengthInms=0;
  } else {
    TrackLengthInms=(int)(VGMHeader.TotalLength/44.1);
  }

  // Get loop data
  if (VGMHeader.LoopLength==0) {
    LoopLengthInms=0;
    LoopOffset=0;
  } else {
    LoopLengthInms=(int)(VGMHeader.LoopLength/44.1);
    LoopOffset=VGMHeader.LoopOffset+0x1c;
  }

  // Get clock values
  PSGClock=VGMHeader.PSGClock;
  YM2413Clock=VGMHeader.YM2413Clock;
  YM2612Clock=VGMHeader.YM2612Clock;
  YM2151Clock=VGMHeader.YM2151Clock;

  // BlackAura - Disable all FM chips
  FMChips=0;

  //Changement de fréquence => désinitialise tout
  if (vgmSampleRateModified)
	  FMChipsInited = 0;

  // Get rate
  FileRate=VGMHeader.RecordingRate;


  char *GD3;
  int upper;
  const int MAX_LENGTH = 127;
  char textline[MAX_LENGTH+1];

  //Va savoir pourquoi le 0x14...
  VGMFile->seek(VGMHeader.GD3Location + 0x14);
  VGMFile->read(textline, 4);
  textline[4]='\0';
  if(strcmp(textline,"Gd3 ")==0) { // we have a GD3 tag
	//Move 8 bytes further
	VGMFile->seek(8, SEEK_CUR);

    getgd3string(textline,MAX_LENGTH,VGMFile); // track name
    strcpy(menuSoundTitle, textline);
    getgd3string(NULL,0,VGMFile);			   // skip japanese
    getgd3string(textline,MAX_LENGTH,VGMFile); // game name
    strcpy(menuSoundArtist, textline);
 /*   getgd3string(NULL,0,VGMFile);
    getgd3string(textline,MAX_LENGTH,VGMFile); // system name
    getgd3string(NULL,0,VGMFile);
    getgd3string(textline,MAX_LENGTH,VGMFile); // author*/

  }

	  // Open output plugin
/*  maxlatency = mod.outMod->Open(SAMPLERATE,NCH,16, -1,-1);
  if (maxlatency < 0) {  // error opening device
    gzclose(InputFile);
    InputFile=NULL;
    return 1;
  }

  // Set info
  if (TrackLengthInms==0) {
    mod.SetInfo(0,SAMPLERATE/1000,NCH,1);
  } else {
    mod.SetInfo(
      (int)(FileSize*8000.0/1024/TrackLengthInms+0.5),  // Bitrate /Kbps (+0.5 for rounding)
      SAMPLERATE/1000,      // Sampling rate /kHz
      NCH,            // Channels
      1);              // Synched (?)
  }

  // Open page in MB if wanted
  if (UseMB & AutoMB) {
    InfoInBrowser(fn,ForceMBOpen);
    SendMessage(mod.hMainWindow,WM_USER,1,248); // block any more things opening in there
  }

  // initialize vis stuff
  mod.SAVSAInit(maxlatency,SAMPLERATE);
  mod.VSASetInfo(NCH,SAMPLERATE);

  mod.outMod->SetVolume(-666); // set the output plug-ins default volume*/

  VGMFile->seek(VGMDataOffset);
//  gzseek(InputFile,VGMDataOffset,SEEK_SET);

  // FM Chip startups are done whenever a chip is used for the first time

  // Start up SN76489 (if used)
  if (PSGClock) {
    VGM_SN76489_Init(0,PSGClock,SAMPLERATE);
    VGM_SN76489_Config(0,SN76489_Mute,SN76489_VolumeArray,VGMHeader.PSGWhiteNoiseFeedback, VGMHeader.PSGShiftRegisterWidth, ((int)(VGMHeader.YM2612Clock/1000000)==7?0:1) ); // nasty hack: boost noise except for YM2612 music
    VGM_SN76489_SetPanning(0,SN76489_Pan[0],SN76489_Pan[1],SN76489_Pan[2],SN76489_Pan[3]);
  }

  // Reset some stuff
  paused=0;
  NumLoopsDone=0;
  SeekToSampleNumber=-1;
  PauseBetweenTracksCounter=-1;  // signals "not pausing"; 0+ = samples left to pause
  LoopingFadeOutTotal=-1;      // signals "haven't started fadeout yet"

  // Start up decode thread
  killDecodeThread=0;
//  thread_handle = CreateThread(NULL,0,(LPTHREAD_START_ROUTINE) DecodeThread,(void *) &killDecodeThread,0,&thread_id);
  // Set it to high priority to avoid breaks
//  SetThreadPriority(thread_handle,THREAD_PRIORITY_ABOVE_NORMAL);
//  SetThreadPriority(thread_handle,THREAD_PRIORITY_HIGHEST);
 
  return 0; 
}

//-----------------------------------------------------------------
// Pause
//-----------------------------------------------------------------
/*void pause() {
  paused=1;
  mod.outMod->Pause(1);
}

//-----------------------------------------------------------------
// Unpause
//-----------------------------------------------------------------
void unpause() {
  paused=0;
  mod.outMod->Pause(0);
}

//-----------------------------------------------------------------
// Is it paused?
//-----------------------------------------------------------------
int ispaused() {
  return paused;
}

//-----------------------------------------------------------------
// Stop
//-----------------------------------------------------------------
void stop() {
  SeekToSampleNumber=0;  // Fixes near-eof errors - it breaks it out of the wait-for-output-to-stop loop in DecodeThread
  if (thread_handle != INVALID_HANDLE_VALUE) {  // If the playback thread is going
    killDecodeThread=1;  // Set the flag telling it to stop
    if (WaitForSingleObject(thread_handle,INFINITE) == WAIT_TIMEOUT) {
      MessageBox(mod.hMainWindow,"error asking thread to die!",mod.description,0);
      TerminateThread(thread_handle,0);
    }
    CloseHandle(thread_handle);
    thread_handle = INVALID_HANDLE_VALUE;
  }
  if (InputFile!=NULL) {
    gzclose(InputFile);  // Close input file
    InputFile=NULL;
  }

  mod.outMod->Close();  // close output plugin

  mod.SAVSADeInit();  // Deinit vis

  // Stop YM2413
  if USINGCHIP(FM_YM2413) 
    EMU2413_OPLL_delete(opll);

  // Stop YM2612
  if USINGCHIP(FM_YM2612)
#ifdef YM2612GENS
    YM2612_End();
#else
    YM2612Shutdown();
#endif

  // Stop YM2151
  if USINGCHIP(FM_YM2151)
    YM2151Shutdown();

  // Stop SN76489
  // not needed

  SendMessage(mod.hMainWindow,WM_USER,0,248); // let Now Playing work as normal
}

//-----------------------------------------------------------------
// Get track length in ms
//-----------------------------------------------------------------
int getlength() {
  int l=(int)((TrackLengthInms+NumLoops*LoopLengthInms)*((PlaybackRate&&FileRate)?(double)FileRate/PlaybackRate:1));
  if (l>mod.outMod->GetOutputTime())
    return l;
  else return -1000;
}

//-----------------------------------------------------------------
// Get playback position in ms - sent to output plugin
//-----------------------------------------------------------------
int getoutputtime() {
  return mod.outMod->GetOutputTime();
}

//-----------------------------------------------------------------
// Seek
//-----------------------------------------------------------------
void setoutputtime(int time_in_ms) {
//  int IntroLengthInms=TrackLengthInms-LoopLengthInms;
  long int YM2413Channels;

  if (InputFile==NULL) return;

  if (getlength()<0) return; // disable seeking on fadeout/silence

  mod.outMod->Pause(1);

  if USINGCHIP(FM_YM2413) {  // If using YM2413, reset it
    int i;
    YM2413Channels=EMU2413_OPLL_toggleMask(opll,0);
    EMU2413_OPLL_reset(opll);
    EMU2413_OPLL_setMask(opll,YM2413Channels);
    for ( i = 0; i < YM2413_NUM_CHANNELS; ++i )
      EMU2413_OPLL_set_pan( opll, i, YM2413_Pan[i] );
  }

  if USINGCHIP(FM_YM2612) {
#ifdef YM2612GENS
    YM2612_Reset();
#else
    YM2612ResetChip(0);
#endif
  }

  if USINGCHIP(FM_YM2151) {
    YM2151ResetChip(0);
  }

  gzseek(InputFile,VGMDataOffset,SEEK_SET);
  NumLoopsDone=0;

  if (LoopLengthInms>0) {  // file is looped
    // See if I can skip some loops
    while (time_in_ms>TrackLengthInms) {
      ++NumLoopsDone;
      time_in_ms-=LoopLengthInms;
    }
    SeekToSampleNumber=(int)(time_in_ms*44.1);
    mod.outMod->Flush(time_in_ms+NumLoopsDone*LoopLengthInms);
  } else {        // Not looped
    SeekToSampleNumber=(int)(time_in_ms*44.1);
    mod.outMod->Flush(time_in_ms);

    if (time_in_ms>TrackLengthInms) NumLoopsDone=NumLoops+1; // for seek-past-eof in non-looped files
  }

  // If seeking beyond EOF...
  if (NumLoopsDone>NumLoops) {
    // Tell Winamp it's the end of the file
    while (1) {
      mod.outMod->CanWrite();  // hmm... does something :P
      if (!mod.outMod->IsPlaying()) {  // if the buffer has run out
        PostMessage(mod.hMainWindow,WM_WA_MPEG_EOF,0,0);  // tell WA it's EOF
        return;
      }
      Sleep(10);  // otherwise wait 10ms and try again
    }
  }

  if (!paused) mod.outMod->Pause(0);
}

//-----------------------------------------------------------------
// Set volume - sent to output plugin
//-----------------------------------------------------------------
void setvolume(int volume) {
  mod.outMod->SetVolume(volume);
}

//-----------------------------------------------------------------
// Set balance - sent to output plugin
//-----------------------------------------------------------------
void setpan(int pan) {
  mod.outMod->SetPan(pan);
}*/

//-----------------------------------------------------------------
// File info dialogue
//-----------------------------------------------------------------
// Dialogue box callback function
wchar GD3Strings[11][1024*2];
/*BOOL CALLBACK FileInfoDialogProc(HWND DlgWin,UINT wMessage,WPARAM wParam,LPARAM lParam) {
  const int DlgFields[11]={ebTitle,ebTitle,ebName,ebName,ebSystem,ebSystem,ebAuthor,ebAuthor,ebDate,ebCreator,ebNotes};
    switch (wMessage) {  // process messages
    case WM_INITDIALOG:  {  // Initialise dialogue
      // Read VGM/GD3 info
      gzFile  *fh;
      struct TVGMHeader  VGMHeader;
      struct TGD3Header  GD3Header;
      int i;
      unsigned int FileSize;

      SendDlgItemMessage(DlgWin,rbEnglish+FileInfoJapanese,BM_SETCHECK,1,0);

      if (IsURL(FilenameForInfoDlg)) {
        // Filename is a URL
        if (
          (strcmp(FilenameForInfoDlg,CurrentURLFilename)==0) ||
          (strcmp(FilenameForInfoDlg,CurrentURL)==0)
          ) {
          // If it's the current file, look at that temp file
          SetDlgItemText(DlgWin,ebFilename,CurrentURL);
          strcpy(FilenameForInfoDlg,CurrentURLFilename);
        } else {
          // If it's not the current file, no info
          SetDlgItemText(DlgWin,ebFilename,FilenameForInfoDlg);
          SetDlgItemText(DlgWin,ebNotes,"Information unavailable for this URL");
          return (TRUE);
        }
      } else {
        // Filename is not a URL
        SetDlgItemText(DlgWin,ebFilename,FilenameForInfoDlg);
      }

      // Get file size
      {
        HANDLE f=CreateFile(FilenameForInfoDlg,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
        char tempstr[100];
        FileSize=GetFileSize(f,NULL);
        CloseHandle(f);

        sprintf(tempstr,"%d bytes",FileSize);
        SetDlgItemText(DlgWin,ebSize,tempstr);
      }

      for (i=0;i<11;++i) GD3Strings[i][0]=L'\0';  // clear strings

      fh=gzopen(FilenameForInfoDlg,"rb");
      if (fh==0) {  // file not opened
        char msgstring[1024];
        sprintf(msgstring,"Unable to open:\n%s",FilenameForInfoDlg);
        MessageBox(mod.hMainWindow,msgstring,mod.description,0);
        return (TRUE);
      }
      i=gzread(fh,&VGMHeader,sizeof(VGMHeader));

      if (i<sizeof(VGMHeader)) {
        // File too short/error reading
        char msgstring[1024];
        sprintf(msgstring,"File too short:\n%s",FilenameForInfoDlg);
        MessageBox(mod.hMainWindow,msgstring,mod.description,0);
        return (TRUE);
      } else if (VGMHeader.VGMIdent != VGMIDENT) {
        // VGM marker incorrect
        char msgstring[1024];
        sprintf(msgstring,"VGM marker not found in:\n%s",FilenameForInfoDlg);
        MessageBox(mod.hMainWindow,msgstring,mod.description,0);
        return (TRUE);
      } else if ((VGMHeader.Version<MINVERSION) || ((VGMHeader.Version & REQUIREDMAJORVER)!=REQUIREDMAJORVER)) {
        // VGM version incorrect
        char msgstring[1024];
        sprintf(msgstring,"Unsupported VGM version (%x) in %s",VGMHeader.Version,FilenameForInfoDlg);
        MessageBox(mod.hMainWindow,msgstring,mod.description,0);
        return (TRUE);
      } else {  // VGM header exists
        char tempstr[256];
        char tempstr2[256];
        sprintf(tempstr,"%x.%02x",VGMHeader.Version>>8,VGMHeader.Version&0xff);
        SetDlgItemText(DlgWin,ebVersion,tempstr);

        // change the chip boxes for versions less than 1.10 so they are in the old sense
        if (VGMHeader.Version<0x0110)
        {
          SetDlgItemText(DlgWin,rbYM2413,"FM chips");
          ShowWindow(GetDlgItem(DlgWin,rbYM2612),FALSE);
          ShowWindow(GetDlgItem(DlgWin,rbYM2151),FALSE);
        }
        SendDlgItemMessage(DlgWin,rbSN76489,BM_SETCHECK,(VGMHeader.PSGClock!=0),0);
        SendDlgItemMessage(DlgWin,rbYM2413,BM_SETCHECK,(VGMHeader.YM2413Clock!=0),0);
        SendDlgItemMessage(DlgWin,rbYM2612,BM_SETCHECK,(VGMHeader.YM2612Clock!=0),0);
        SendDlgItemMessage(DlgWin,rbYM2151,BM_SETCHECK,(VGMHeader.YM2151Clock!=0),0);

        sprintf(
          tempstr,
          "%d bytes (%d bps)",
          FileSize,
          (int)(FileSize*8.0/(VGMHeader.TotalLength/44100.0))
        );
        SetDlgItemText(DlgWin,ebSize,tempstr);

        sprintf(tempstr,"%s total (inc. ",printtime(tempstr2,VGMHeader.TotalLength/44100.0));
        printtime(tempstr2,VGMHeader.LoopLength/44100.0);
        strcat(tempstr,tempstr2);
        strcat(tempstr," loop)");
        SetDlgItemText(DlgWin,ebLength,tempstr);

        if (VGMHeader.GD3Offset>0) {
          // GD3 tag exists
          gzseek(fh,VGMHeader.GD3Offset+0x14,SEEK_SET);
          i=gzread(fh,&GD3Header,sizeof(GD3Header));
          if ((i==sizeof(GD3Header)) &&
            (GD3Header.GD3Ident == GD3IDENT) &&
            (GD3Header.Version>=MINGD3VERSION) &&
            ((GD3Header.Version & REQUIREDGD3MAJORVER)==REQUIREDGD3MAJORVER)) {
              // GD3 is acceptable version
              wchar *p,*GD3string;
              int i;

              p=malloc(GD3Header.Length);  // Allocate memory for string data
              gzread(fh,p,GD3Header.Length);  // Read it in
              GD3string=p;

              for (i=0;i<11;++i) {
                wcscpy(GD3Strings[i],GD3string);
                GD3string+=wcslen(GD3string)+1;
              }

              // special handling for any strings?
              // Notes: change \n to \r\n so Windows shows it properly
              {  
                wchar *wp=GD3Strings[10]+wcslen(GD3Strings[10]);
                while (wp>=GD3Strings[10]) {
                  if (*wp==L'\n') {
                    memmove(wp+1,wp,(wcslen(wp)+1)*2);
                    *wp=L'\r';                    
                  }
                  wp--;
                }
              }

              free(p);  // Free memory for string buffer

              // Set non-language-changing fields here
              for (i=8;i<11;++i) {
                if (!SetDlgItemTextW(DlgWin,DlgFields[i],GD3Strings[i])) {
                  // Widechar text setting failed, try WC2MB
                  char MBCSstring[1024*2]="";
                  WideCharToMultiByte(CP_ACP,0,GD3Strings[i],-1,MBCSstring,1024*2,NULL,NULL);
                  SetDlgItemText(DlgWin,DlgFields[i],MBCSstring);
                }
              }

              PostMessage(DlgWin,WM_COMMAND,rbEnglish+FileInfoJapanese,0);
          } else {  // Unacceptable GD3 version
            char msgstring[1024];
            sprintf(msgstring,"File too short or unsupported GD3 version (%x) in %s",GD3Header.Version,FilenameForInfoDlg);
            MessageBox(mod.hMainWindow,msgstring,mod.description,0);
            return (TRUE);
          }
        } else {  // no GD3 tag
          SetDlgItemText(DlgWin,ebNotes,"No GD3 tag or incompatible GD3 version");
          EnableWindow(GetDlgItem(DlgWin,btnUnicodeText),FALSE);  // disable button if it can't be used
        }
      }
      gzclose(fh);



      return (TRUE);
    }
    case WM_COMMAND:
            switch (LOWORD(wParam)) {
        case IDOK:  // OK button
          FileInfoJapanese=IsDlgButtonChecked(DlgWin,rbJapanese);
                    EndDialog(DlgWin,0);  // return 0 = OK
                    return (TRUE);
                case IDCANCEL:  // [X] button, Alt+F4, etc
                    EndDialog(DlgWin,1);  // return 1 = Cancel, stops further dialogues being opened
                    return (TRUE);
        case btnConfigure:
          config(DlgWin);
          break;
        case rbEnglish:
        case rbJapanese:
          {
            int i;
            const int x=(LOWORD(wParam)==rbJapanese?1:0);
            for (i=0;i<4;++i) {
              if (!SetDlgItemTextW(DlgWin,DlgFields[i*2+x],GD3Strings[i*2+x])) {
                char MBCSstring[1024*2]="";
                WideCharToMultiByte(CP_ACP,0,GD3Strings[i*2+x],-1,MBCSstring,1024*2,NULL,NULL);
                SetDlgItemText(DlgWin,DlgFields[i*2+x],MBCSstring);
              }
            }
          }
          break;
        case btnUnicodeText:
          InfoInBrowser(FilenameForInfoDlg,1);
          break;
        default:
          break;
        }
      break;
    }
    return (FALSE) ;    // return FALSE to signify message not processed
}

int infoDlg(char *fn, HWND hwnd)
{
  FilenameForInfoDlg=fn;
  return DialogBox(mod.hDllInstance, "FILEINFODIALOGUE", hwnd, FileInfoDialogProc);
}

//-----------------------------------------------------------------
// Get file info for playlist/title display
//-----------------------------------------------------------------
void getfileinfo(char *filename, char *title, int *length_in_ms) {
  long int  TrackLength;
  char    TrackTitle[1024],
        FileToUse[MAX_PATH],
        JustFileName[MAX_PATH];

  // if filename is blank then we want the current file
  if ((filename==NULL) || (*filename=='\0'))
    strcpy(FileToUse,lastfn);
  else
    strcpy(FileToUse,filename);

  if (IsURL(FileToUse)) {
    if (title) strcpy(title,FileToUse);
    if (length_in_ms) *length_in_ms=-1000;
    return;
  }

  {  // trim to just filename
    char *p=FileToUse+strlen(FileToUse);
    while (*p != '\\' && p >= FileToUse) p--;
    strcpy(JustFileName,++p);
  }

  {  // get GD3 info
    gzFile  *fh;
    struct TVGMHeader  VGMHeader;
    struct TGD3Header  GD3Header;
    int i;

    fh=gzopen(FileToUse,"rb");
    if (fh==0) {  // file not opened
      if (title) sprintf(title,"Unable to open %s",FileToUse);
      if (length_in_ms) *length_in_ms=-1000;
      return;
    }
    i=gzread(fh,&VGMHeader,sizeof(VGMHeader));

    if (i<sizeof(VGMHeader)) {
      sprintf(TrackTitle,"File too short: %s",JustFileName);
    } else if (VGMHeader.VGMIdent != VGMIDENT) {
      // VGM marker incorrect
      sprintf(TrackTitle,"VGM marker not found in %s",JustFileName);
    } else if ((VGMHeader.Version<MINVERSION) || ((VGMHeader.Version & REQUIREDMAJORVER)!=REQUIREDMAJORVER)) {
      // VGM version incorrect
      sprintf(TrackTitle,"Unsupported version (%x) in %s",VGMHeader.Version,JustFileName);
    } else {
      // VGM header OK
      TrackLength=(long int) (
        (VGMHeader.TotalLength+NumLoops*VGMHeader.LoopLength)
        /44.1
        *((PlaybackRate&&VGMHeader.RecordingRate)?(double)VGMHeader.RecordingRate/PlaybackRate:1)
//        +PauseBetweenTracksms+(VGMHeader.LoopOffset?LoopingFadeOutms:0)
      );

      if (VGMHeader.GD3Offset>0) {
        // GD3 tag exists
        gzseek(fh,VGMHeader.GD3Offset+0x14,SEEK_SET);
        i=gzread(fh,&GD3Header,sizeof(GD3Header));
        if ((i==sizeof(GD3Header)) &&
          (GD3Header.GD3Ident == GD3IDENT) &&
          (GD3Header.Version>=MINGD3VERSION) &&
          ((GD3Header.Version & REQUIREDGD3MAJORVER)==REQUIREDGD3MAJORVER)) {
            // GD3 is acceptable version
            wchar *p,*GD3string;
            const char strings[]="tgsadc";  // Title Game System Author Date Creator
            char GD3strings[10][256];
            const char What[10][12]={"track title","track title","game","game","system","system","author","author","date","creator"};
            int i;

            p=malloc(GD3Header.Length);  // Allocate memory for string data
            gzread(fh,p,GD3Header.Length);  // Read it in
            GD3string=p;

            for (i=0;i<10;++i) {
              // Get next string and move the pointer to the next one
              WideCharToMultiByte(CP_ACP,0,GD3string,-1,GD3strings[i],256,NULL,NULL);
              GD3string+=wcslen(GD3string)+1;
            }

            free(p);  // Free memory for string buffer

            for (i=0;i<10;++i) if (!GD3strings[i][0]) {  // Handle empty fields...
              // First, see if I can substitute the other language
              if (i<8) strcpy(GD3strings[i],GD3strings[(i%2?i-1:i+1)]);
              // if it is still blank, put "Unknown xxx"
              if (!GD3strings[i][0]) {
                strcpy(GD3strings[i],"Unknown ");
                strcat(GD3strings[i],What[i]);
              }
            }

            strcpy(TrackTitle,TrackTitleFormat);

            i=0;
            while (i<6) {
              char SearchStr[]="%x";
              char *pos;
              // Search for format strings
              SearchStr[1]=strings[i];
              pos=strstr(TrackTitle,SearchStr);
              if (pos!=NULL) {  // format string found
                // redo this to use one string?
                char After[1024];
                *pos='\0';
                strcpy(After,TrackTitle);  // copy text before it
                if ((*(pos+2)=='j') && (i<4)) {
                  strcat(After,GD3strings[i*2+1]);  // add GD3 string
                  strcat(After,pos+3);  // add text after it
                } else {
                  if (i==5) {
                    strcat(After,GD3strings[9]);
                  } else {
                    strcat(After,GD3strings[i*2]);  // add GD3 string
                  }
                  strcat(After,pos+2);  // add text after it
                }
                strcpy(TrackTitle,After);
              } else {
                i++;
              }
            }
        } else {
          // Problem with GD3
          sprintf(TrackTitle,"GD3 invalid: %s",JustFileName);
        }
      } else {  // No GD3 tag, so use filename
        strcpy(TrackTitle,JustFileName);
      }
    }
    gzclose(fh);
  }

  if (title) strcpy(title,TrackTitle);
  if (length_in_ms) *length_in_ms=TrackLength;
}

void debuglog(int number) {
  FILE *f;
  f=fopen("c:\\in_vgm log.txt","a");
  fprintf(f,"%d\n",number);
  fclose(f);
}*/

//-----------------------------------------------------------------
// Input-side EQ - not used
//-----------------------------------------------------------------
void eq_set(int on, char data[10], int preamp) {}

//-----------------------------------------------------------------
// Decode thread
//-----------------------------------------------------------------
//#define ReadByte() gzgetc(InputFile)
#define ReadByte() VGMFile->readc()
int SamplesTillNextRead=0;
float WaitFactor,FractionalSamplesTillNextRead=0;

#define FIXED_SAMPLERATE 44100
int currentSampleValue = 0;

int VGM_DecodeThread(short *SampleBuffer, int length, int *finallen)			{
  if ((PlaybackRate==0) || (FileRate==0)) {
    WaitFactor=1.0;
  } else {
    WaitFactor=(float)FileRate/PlaybackRate;
  }

//  while (! *((int *)b) ) {
      int x, real_len;

      unsigned char b1,b2;

	  real_len = 0;
      for (x=0;x<length;++x) {
//        if (PauseBetweenTracksCounter==-1) // if not pausing between tracks
        // Read file, write stuff
        while (!SamplesTillNextRead && PauseBetweenTracksCounter==-1) {
		  b1 = ReadByte();
          switch (b1) {
          case 0x4f:  // GG stereo
            b1=ReadByte();
            if (PSGClock) VGM_SN76489_GGStereoWrite(0,(char)b1);
            break;
          case 0x50:  // SN76489 write
            b1=ReadByte();
            if (PSGClock) VGM_SN76489_Write(0,(char)b1);
            break;
          case 0x51:  // YM2413 write
            b1=ReadByte();
            b2=ReadByte();
            if (YM2413Clock) {
              if (!USINGCHIP(FM_YM2413)) {  // BlackAura - If YM2413 emu not started, start it
				int i;
				if (!CHIPINITED(FM_YM2413))			{
	                // Start the emulator up
	                opll=EMU2413_OPLL_new(YM2413Clock,SAMPLERATE);
					EMU2413_OPLL_reset(opll);
					EMU2413_OPLL_reset_patch(opll,0);
					EMU2413_OPLL_setMask(opll,YM2413Channels);
					for ( i = 0; i< YM2413_NUM_CHANNELS; ++i )
					  EMU2413_OPLL_set_pan( opll, i, YM2413_Pan[i] );
					EMU2413_OPLL_set_quality(opll,YM2413HiQ);
					FMChipsInited|=FM_YM2413;
				}
                // Set the flag for it
                FMChips|=FM_YM2413;
              }
              EMU2413_OPLL_writeReg(opll,b1,b2);  // Write to the chip
            }
            break;
          case 0x52:  // YM2612 write (port 0)
            b1=ReadByte();
            b2=ReadByte();
            if (YM2612Clock) {
              if (!USINGCHIP(FM_YM2612)) {
				if (!CHIPINITED(FM_YM2612))			{
#ifdef YM2612GENS
					YM2612_Init(YM2612Clock,SAMPLERATE,0);
#else
					YM2612Init(1,YM2612Clock,SAMPLERATE,NULL,NULL);
#endif
					FMChipsInited|=FM_YM2612;
				}
				FMChips|=FM_YM2612;
              }
#ifdef YM2612GENS
              YM2612_Write(0,b1);
              YM2612_Write(1,b2);
#else
              YM2612Write(0,0,b1);
              YM2612Write(0,1,b2);
#endif
            }
            break;
          case 0x53:  // YM2612 write (port 1)
            b1=ReadByte();
            b2=ReadByte();
            if (YM2612Clock) {
              if (!USINGCHIP(FM_YM2612)) {
				if (!CHIPINITED(FM_YM2612))			{
#ifdef YM2612GENS
					YM2612_Init(YM2612Clock,SAMPLERATE,0);
#else
					YM2612Init(1,YM2612Clock,SAMPLERATE,NULL,NULL);
#endif
					FMChipsInited|=FM_YM2612;
				}
				FMChips|=FM_YM2612;
              }
#ifdef YM2612GENS
              YM2612_Write(2,b1);
              YM2612_Write(3,b2);
#else
              YM2612Write(0,2,b1);
              YM2612Write(0,3,b2);
#endif
            }
            break;
          case 0x54:  // BlackAura - YM2151 write
            b1=ReadByte();
            b2=ReadByte();
            if (YM2151Clock) {
              if (!USINGCHIP(FM_YM2151)) {
			    if (!CHIPINITED(FM_YM2151))			{
	                YM2151Init(1,YM2151Clock,SAMPLERATE);
	                FMChipsInited|=FM_YM2151;
				}
	            FMChips|=FM_YM2151;
              }
              YM2151WriteReg(0,b1,b2);
            }
            break;
          case 0x61:  // Wait n samples
            b1=ReadByte();
            b2=ReadByte();
            SamplesTillNextRead=b1 | (b2 << 8);
            break;
          case 0x62:  // Wait 1/60 s
            SamplesTillNextRead=735;
            break;
          case 0x63:  // Wait 1/50 s
            SamplesTillNextRead=882;
            break;
          // No-data waits
          case 0x70:
          case 0x71:
          case 0x72:
          case 0x73:
          case 0x74:
          case 0x75:
          case 0x76:
          case 0x77:
          case 0x78:
          case 0x79:
          case 0x7a:
          case 0x7b:
          case 0x7c:
          case 0x7d:
          case 0x7e:
          case 0x7f:
            SamplesTillNextRead=(b1 & 0xf) + 1;
            break;

          // (YM2612 sample then short delay)s
          case 0x81:
          case 0x82:
          case 0x83:
          case 0x84:
          case 0x85:
          case 0x86:
          case 0x87:
          case 0x88:
          case 0x89:
          case 0x8A:
          case 0x8B:
          case 0x8C:
          case 0x8D:
          case 0x8E:
          case 0x8F:
            SamplesTillNextRead = b1 & 0x0f;
            // fall through
          case 0x80:  // YM2612 write (port 0) 0x2A (PCM)
            if (YM2612Clock && pcm_data) {
              b1 = 0x2A;
              b2 = pcm_data [pcm_pos++];
              oslAssert( pcm_pos <= pcm_data_size );

              if (!USINGCHIP(FM_YM2612)) {
				if (!CHIPINITED(FM_YM2612))			{
#ifdef YM2612GENS
					YM2612_Init(YM2612Clock,SAMPLERATE,0);
#else
					YM2612Init(1,YM2612Clock,SAMPLERATE,NULL,NULL);
#endif
					FMChipsInited|=FM_YM2612;
				}
				FMChips|=FM_YM2612;
              }
#ifdef YM2612GENS
              YM2612_Write(0,b1);
              YM2612_Write(1,b2);
#else
              YM2612Write(0,0,b1);
              YM2612Write(0,1,b2);
#endif
            }
            break;
            
          case 0xe0: { // Set position in PCM data
            unsigned char buf [3];
			VGMFile->read(buf, sizeof buf);
//            gzread( InputFile, buf, sizeof buf );
            pcm_pos = buf [2] * 0x10000L + buf [1] * 0x100L + buf [0];
            oslAssert( pcm_pos < pcm_data_size );
            break;
          }
          
          case 0x67: { // data block at beginning of file
            unsigned char buf[6];
            unsigned long data_size;
			VGMFile->read(buf, sizeof buf);
//            gzread( InputFile, buf, sizeof(buf));
            oslAssert( buf[0] == 0x66 );
            data_size = (buf[5] << 24) | (buf[4] << 16) | (buf[3] << 8) | buf[2];
            switch (buf[1]) {
            case 0x00:
              if ( !pcm_data )
              {
                pcm_data_size = data_size;
            	  pcm_data = (unsigned char*)malloc( pcm_data_size );
            	  if ( pcm_data )
            	  {
					  VGMFile->read(pcm_data, pcm_data_size);
//            		  gzread( InputFile, pcm_data, pcm_data_size );
            		  break; // <-- exits out of (nested) case block on successful load
            	  }
              }
              // ignore data block for subsequent blocks and also on malloc() failure
			  VGMFile->seek(pcm_data_size, SEEK_CUR);
//        	    gzseek( InputFile, pcm_data_size, SEEK_CUR );
              break;
            default:
              // skip unknown data blocks
			  VGMFile->seek(data_size, SEEK_CUR);
//              gzseek( InputFile, data_size, SEEK_CUR );
              break;
            }
            
        	  break;
          }

          case 0x66:  // End of data
            ++NumLoopsDone;  // increment loop count
            // If there's no looping then go to the inter-track pause
            if (LoopOffset==0) {
              if ((PauseBetweenTracksms)&&(PauseBetweenTracksCounter==-1)) {
                // I want to output silence
                PauseBetweenTracksCounter=(long)PauseBetweenTracksms*44100/1000;
              } else {
                // End track
/*                while (1) {
                  mod.outMod->CanWrite();  // hmm... does something :P
                  if (!mod.outMod->IsPlaying()) {  // if the buffer has run out
                    PostMessage(mod.hMainWindow,WM_WA_MPEG_EOF,0,0);  // tell WA it's EOF
                    return 0;
                  }
                  Sleep(10);  // otherwise wait 10ms and try again
                  if (SeekToSampleNumber>-1) break;
                }*/
				return 1;
              }
			  VGMFile->unreadc(0x66);
//              gzungetc(0x66,InputFile);
            } else {
              // if there is looping, and the required number of loops have played, then go to fadeout
              if ((NumLoopsDone>NumLoops)&&(LoopingFadeOutTotal==-1)) {
                // Start fade out
                LoopingFadeOutTotal=(long)LoopingFadeOutms*44100/1000;  // number of samples to fade over
                LoopingFadeOutCounter=LoopingFadeOutTotal;
              }
              // Loop the file
			  VGMFile->seek(LoopOffset, SEEK_SET);
//              gzseek(InputFile,LoopOffset,SEEK_SET);
            }
            break;

          default:
            // Unknown commands
            if ( b1 >= 0x30 && b1 <= 0x4e )
			  VGMFile->seek(1, SEEK_CUR);
//              gzseek(InputFile,1,SEEK_CUR);
            else if ( b1 >= 0x55 && b1 <= 0x5f )
			  VGMFile->seek(2, SEEK_CUR);
//              gzseek(InputFile,2,SEEK_CUR);
            else if ( b1 >= 0xa0 && b1 <= 0xbf )
			  VGMFile->seek(2, SEEK_CUR);
//              gzseek(InputFile,2,SEEK_CUR);
            else if ( b1 >= 0xc0 && b1 <= 0xdf )
			  VGMFile->seek(3, SEEK_CUR);
//              gzseek(InputFile,3,SEEK_CUR);
            else if ( b1 >= 0xe1 && b1 <= 0xff )
			  VGMFile->seek(4, SEEK_CUR);
//              gzseek(InputFile,4,SEEK_CUR);
            //assert(FALSE); // I want to blow up here when debugging
            break;
          }  // end case

          // debug
//          if(SamplesTillNextRead)debuglog(SamplesTillNextRead);

          FractionalSamplesTillNextRead+=SamplesTillNextRead*WaitFactor;
          SamplesTillNextRead=(int)FractionalSamplesTillNextRead;
          FractionalSamplesTillNextRead-=SamplesTillNextRead;

          if (SeekToSampleNumber>-1) {  // If a seek is wanted
            SeekToSampleNumber-=SamplesTillNextRead;  // Decrease the required seek by the current delay
            SamplesTillNextRead=0;
            if (SeekToSampleNumber<0) {
              SamplesTillNextRead=-SeekToSampleNumber;
              SeekToSampleNumber=-1;
            }
            continue;
          }
        }  // end while

        //SamplesTillNextRead<<=4; // speed up by 2^4 = 16

		currentSampleValue+=SAMPLERATE;
		if (currentSampleValue >= FIXED_SAMPLERATE)			{
			currentSampleValue -= FIXED_SAMPLERATE;

			// Write sample
			#if NCH == 2
			// Stereo
			if (PauseBetweenTracksCounter==-1) {
			  int NumChipsUsed=0;
			  signed int l=0,r=0;

			  // PSG
			  if (PSGClock) {
				NumChipsUsed++;
				VGM_SN76489_UpdateOne(0,&l,&r);
			  }

			  if (YM2413Clock) {
				// YM2413
				if USINGCHIP(FM_YM2413) {
				  int channels[2];
				  NumChipsUsed++;
				  EMU2413_OPLL_calc_stereo(opll, (e_int32*)channels);
				  l += channels[0] / YM2413RelativeVol;
				  r += channels[1] / YM2413RelativeVol;
				}
			  }

			  if (YM2612Clock) {
				// YM2612
				if USINGCHIP(FM_YM2612) {
	#ifdef YM2612GENS
				  int *Buffer[2];
				  int Left=0,Right=0;
	#else
				  signed short *Buffer[2];
				  signed short Left,Right;
	#endif
				  NumChipsUsed++;
				  Buffer[0]=&Left;
				  Buffer[1]=&Right;
				  if (YM2612Channels==0) {
	#ifdef YM2612GENS
					YM2612_Update(Buffer,1);
					YM2612_DacAndTimers_Update(Buffer,1);
					Left /=GENSYM2612RelativeVol;
					Right/=GENSYM2612RelativeVol;
	#else
					YM2612UpdateOne(0,Buffer,1);
					Left /=MAMEYM2612RelativeVol;
					Right/=MAMEYM2612RelativeVol;
	#endif
				  } else
					Left=Right=0;  // Dodgy muting until per-channel gets done
	            
				  l+=Left;
				  r+=Right;
				}
			  }

			  if (YM2151Clock) {
				// YM2151
				if USINGCHIP(FM_YM2151) {
				  signed short *mameBuffer[2];
				  signed short mameLeft;
				  signed short mameRight;
				  NumChipsUsed++;
				  mameBuffer[0]=&mameLeft ;
				  mameBuffer[1]=&mameRight;
				  if (YM2151Channels==0) {
					YM2151UpdateOne(0,mameBuffer,1);
					mameLeft /=YM2151RelativeVol;
					mameRight/=YM2151RelativeVol;
				  } else
					mameLeft=mameRight=0;  // Dodgy muting until per-channel gets done

				  l+=mameLeft ;
				  r+=mameRight;
				}
			  }

			  if ((Overdrive)&&(NumChipsUsed)) {
				l=l*8/NumChipsUsed;
				r=r*8/NumChipsUsed;
			  }

			  if (LoopingFadeOutTotal!=-1) { // Fade out
				long v;
				// Check if the counter has finished
				if (LoopingFadeOutCounter<=0) {
				  // if so, go to pause between tracks
				  PauseBetweenTracksCounter=(long)PauseBetweenTracksms*44100/1000;
				} else {
				  // Alter volume
				  v=LoopingFadeOutCounter*MAX_VOLUME/LoopingFadeOutTotal;
				  l=(long)l*v/MAX_VOLUME;
				  r=(long)r*v/MAX_VOLUME;
				  // Decrement counter
				  --LoopingFadeOutCounter;
				}
			  }

			  l *= 6;
			  r *= 6;

			  // Clip values
			  if (l>+32767) l=+32767;  else if (l<-32767) l=-32767;
			  if (r>+32767) r=+32767;  else if (r<-32767) r=-32767;

			  SampleBuffer[2*real_len]  =l;
			  SampleBuffer[2*real_len+1]=r;
			  real_len++;

			} else {
			  // Pause between tracks
			  // output silence
			  SampleBuffer[2*real_len]  =0;
			  SampleBuffer[2*real_len+1]=0;
			  real_len++;
			  --PauseBetweenTracksCounter;
			  if (PauseBetweenTracksCounter<=0) {
	/*            while (1) {
				  if (SeekToSampleNumber>-1) break;
				  mod.outMod->CanWrite();  // hmm... does something :P
				  if (!mod.outMod->IsPlaying()) {  // if the buffer has run out
					PostMessage(mod.hMainWindow,WM_WA_MPEG_EOF,0,0);  // tell WA it's EOF
					return 0;
				  }
				  Sleep(10);  // otherwise wait 10ms and try again
				}*/
				//Terminé
				return 1;
			  }
			}
			#else
			// Mono - not working
			#endif
		}

        --SamplesTillNextRead;
      }
  
/*      x=mod.outMod->GetWrittenTime();  // returns time written in ms (used for synching up vis stuff)
      // Check these two later (not important)
      mod.SAAddPCMData ((char *)SampleBuffer,NCH,16,x);  // Add to vis
      mod.VSAAddPCMData((char *)SampleBuffer,NCH,16,x);  // Add to vis

      samplesinbuffer=mod.dsp_dosamples(
        (short *)SampleBuffer,  // Samples
        samplesinbuffer/NCH,  // No. of samples (?)
        16,            // Bits per sample
        NCH,          // Number of channels
        SAMPLERATE        // Sampling rate
      );
      mod.outMod->Write(
        (char *)SampleBuffer,  // Buffer
        samplesinbuffer*NCH*2  // Size of data in bytes
      );*/
//    }
//    else Sleep(50);
//  }
  *finallen = real_len;
  return 0;
}

//-----------------------------------------------------------------
// Plugin definition structure
//-----------------------------------------------------------------
/*In_Module mod = 
{
  IN_VER,
  PLUGINNAME,
  0,  // hMainWindow
  0,  // hDllInstance
  "vgm;vgz\0VGM Audio File (*.vgm;*.vgz)\0",
  1,  // is_seekable
  1,  // uses output
  config,
  about,
  init,
  quit,
  getfileinfo,
  infoDlg,
  isourfile,
  play,
  pause,
  unpause,
  ispaused,
  stop,
  getlength,
  getoutputtime,
  setoutputtime,
  setvolume,
  setpan,
  0,0,0,0,0,0,0,0,0, // vis stuff
  0,0, // dsp
  eq_set,
  NULL,    // setinfo
  0 // out_mod
};

__declspec(dllexport) In_Module *winampGetInModule2() {
  return &mod;
}*/

/*#define NUMGD3TAGS 11

struct TFileTagInfo {
  char *filename;
  char* tags[NUMGD3TAGS];
  long int length;
} LastFileInfo;

enum {
  GD3X_DUMMYBASEVALUE = -1000, // make sure we have values below 0
  GD3X_TRACKNO,
  // above here don't need to read the file
  GD3X_UNKNOWN,
  // below here need the VGM header
  GD3X_LENGTH,
  // below here need the GD3 tag, first must be 0
  GD3_TITLEEN = 0, GD3_TITLEJP,
  GD3_GAMEEN  , GD3_GAMEJP,
  GD3_SYSTEMEN, GD3_SYSTEMJP,
  GD3_AUTHOREN, GD3_AUTHORJP,
  GD3_DATE,
  GD3_RIPPER,
  GD3_NOTES
};

wchar *getGD3(int index,wchar *block,int blocklen) {
  wchar *p=block;

  // seek to required position
  while((index>0) && (p<block+blocklen)) {
    p+=wcslen(p)+1;
    index--;
  }

  return p;
}

int getTrackNumber(char *filename) {
  // attempts to guess the track number for the given file
  // by looking in the corresponding m3u
  // assumes a filename "Streets of Rage II - Never Return Alive.vgz"
  // will have a playlist "Streets of Rage II.m3u"
  // returns track number
  // or 0 for not found
  FILE *f;
  char *playlist;
  char *p;
  char *fn;
  char buff[1024]; // buffer for reading lines from file
  int number=0;
  int linenumber;

  playlist=malloc(strlen(filename)+16); // plenty of space in all weird cases

  if(playlist) {
    p=strrchr(filename,'\\'); // p points to the last \ in the filename
    if(p){
      // isolate filename
      fn=malloc(strlen(p));
      if(fn) {
        strcpy(fn,p+1);

        while(number==0) {
          p=strstr(p," - "); // find first " - " after current position of p
          if(p) {
            strncpy(playlist,filename,p-filename); // copy filename up until the " - "
            strcpy(playlist+(p-filename),".m3u"); // terminate it with a ".m3u\0"

            f=fopen(playlist,"r"); // try to open file
            if(f) {
              linenumber=1;
              // read through file, a line at a time
              while(fgets(buff,1024,f) && (number==0)) {    // fgets will read in all characters up to and including the line break
                if(strnicmp(buff,fn,strlen(fn))==0) {
                  // found it!
                  number=linenumber;
                }
                if((strlen(buff)>3)&&(buff[0]!='#')) linenumber++; // count lines that are (probably) valid but not #EXTINF lines
              }

              fclose(f);
            }
            p++; // make it not find this " - " next iteration
          } else break;
        }
        free(fn);
      }
    }
    free(playlist);
  }
  return number;
}

void LoadInfo(char *filename)
{
  // load info from filename into fileinfo
  gzFile  *fh;
  struct TVGMHeader VGMHeader;
  int i;

  // mark struct as unusable
  if (LastFileInfo.filename)
    free(LastFileInfo.filename);
  LastFileInfo.filename = NULL;

  fh=gzopen(filename,"rb");
  if (fh==0) {  // file not opened
    return;
  }

  i=gzread(fh,&VGMHeader,sizeof(VGMHeader));

  if (  (i<sizeof(VGMHeader))
      || (VGMHeader.VGMIdent != VGMIDENT)
      || ((VGMHeader.Version<MINVERSION) || ((VGMHeader.Version & REQUIREDMAJORVER)!=REQUIREDMAJORVER))
  ) {
    gzclose(fh);
    return;
  }

  // VGM header OK

  LastFileInfo.length = (long) (
    (VGMHeader.TotalLength+NumLoops*VGMHeader.LoopLength)
    /44.1
    *((PlaybackRate&&VGMHeader.RecordingRate)?(double)VGMHeader.RecordingRate/PlaybackRate:1)
  );

  // blank GD3 tags
  for (i=0;i<NUMGD3TAGS;++i) {
    if (LastFileInfo.tags[i]) free( LastFileInfo.tags[i] );
    LastFileInfo.tags[i] = NULL;
  }

  if (VGMHeader.GD3Offset>0) {
    // GD3 tag exists
    struct TGD3Header  GD3Header;
    gzseek(fh,VGMHeader.GD3Offset+0x14,SEEK_SET);
    i=gzread(fh,&GD3Header,sizeof(GD3Header));
    if (
      (i==sizeof(GD3Header)) &&
      (GD3Header.GD3Ident == GD3IDENT) &&
      (GD3Header.Version>=MINGD3VERSION) &&
      ((GD3Header.Version & REQUIREDGD3MAJORVER)==REQUIREDGD3MAJORVER)
    ) {
      // GD3 is acceptable version
      wchar *GD3data,*p;

      GD3data=malloc(GD3Header.Length);  // Allocate memory for string data
      gzread(fh,GD3data,GD3Header.Length);  // Read it in

      // convert into struct
      p = GD3data;
      for (i=0; i<NUMGD3TAGS; ++i)
      {
        // get the length
        int len = WideCharToMultiByte(CP_ACP,0,p,-1,NULL,0,NULL,NULL);
        if ( len > 0 )
        {
          // allocate buffer
          LastFileInfo.tags[i] = malloc(len+1);
          // convert tag into it
          if (LastFileInfo.tags[i])
            WideCharToMultiByte(CP_ACP,0,p,-1,LastFileInfo.tags[i],len,NULL,NULL);

          // special cases?
          switch(i) {
          case GD3_GAMEEN:
          case GD3_GAMEJP: // album: append " (FM)" for FM soundtracks if wanted; hack: not for 7MHzish clocks
            if ( MLShowFM 
              && VGMHeader.YM2413Clock 
              && (VGMHeader.YM2413Clock / 1000000)!=7 )
            {
              // re-allocate with a bit on the end and replace original
              char *buffer = malloc(len+6);
              if(buffer)
              {
                sprintf(buffer,"%s (FM)",LastFileInfo.tags[i]);
                free(LastFileInfo.tags[i]);
                LastFileInfo.tags[i] = buffer;
              }
            }
            break;
          case GD3_DATE: // year: make it the first 4 digits
            if(strlen(LastFileInfo.tags[i])>4) LastFileInfo.tags[i][4]=0;
            break;
          }
        }
        // move pointer on
        p += wcslen(p)+1;
      }

      free(GD3data);  // Free memory for string buffer
    }
  }
  gzclose(fh);
  
  // copy filename
  LastFileInfo.filename = malloc(strlen(filename)+1);
  strcpy(LastFileInfo.filename, filename);
  // done!
}*/

/*__declspec(dllexport) int winampGetExtendedFileInfo(char *filename,char *metadata,char *ret,int retlen) {
  // return zero on failure
  int tagindex=GD3X_UNKNOWN;

  // can't do anything with no buffer
  if(!ret || !retlen)
    return 0;

  // first, tags that don't need to look at the file itself
  if(stricmp(metadata,"type")==0) { // type
    _snprintf(ret,retlen,"%d",MLType);
    return 1;
  }
  if(stricmp(metadata,"track")==0) { // track number
    int i=getTrackNumber(filename);
    if(i>0) {
      _snprintf(ret,retlen,"%d",i);
      return 1;
    } else return 0;
  }

  // next, check if we've already looked at this file
  if (!LastFileInfo.filename || strcmp(LastFileInfo.filename,filename)!=0)
  {
    LoadInfo(filename);
    // do another check on that filename in case it failed
    if (!LastFileInfo.filename || strcmp(LastFileInfo.filename,filename)!=0)
      return 0;
  }

  // now start returning stuff

  *ret=0;
  
  if(stricmp(metadata,"artist")==0)        tagindex=GD3_AUTHOREN;   // author
  else if(stricmp(metadata,"length")==0)   tagindex=GD3X_LENGTH;  // length /ms
  else if(stricmp(metadata,"title")==0)    tagindex=GD3_TITLEEN;   // track title
  else if(stricmp(metadata,"album")==0)    tagindex=GD3_GAMEEN;   // game name
  else if(stricmp(metadata,"comment")==0)  tagindex=GD3_NOTES;  // comment
  else if(stricmp(metadata,"year")==0)     tagindex=GD3_DATE;   // year
  else if(stricmp(metadata,"genre")==0)    tagindex=GD3_SYSTEMEN;   // system

//  if(tagindex==GD3_UNKNOWN)MessageBox(0,metadata,"Unknown",0); // debug: get metadata types
    
  if((MLJapanese)&&(tagindex>=GD3_TITLEEN)&&(tagindex<=GD3_AUTHOREN)) tagindex++; // Increment index for Japanese

  switch (tagindex) {
    case GD3X_LENGTH:
      _snprintf(ret,retlen,"%d",LastFileInfo.length);
      break;
    case GD3_TITLEEN: case GD3_TITLEJP: case GD3_GAMEEN: case GD3_GAMEJP: case GD3_SYSTEMEN: case GD3_SYSTEMJP:
    case GD3_AUTHOREN: case GD3_AUTHORJP: case GD3_DATE: case GD3_RIPPER: case GD3_NOTES:
      // copy from LastFileInfo, but check for empty ones and use the alternative
      if (
        ( (LastFileInfo.tags[tagindex] == NULL) || (strlen(LastFileInfo.tags[tagindex]) == 0) )
        &&( tagindex <= GD3_AUTHORJP)
      ) {
        // tweak index to point to alternative
        if(tagindex%2) tagindex--;
        else tagindex++;
      }
      // copy
      strncpy( ret, LastFileInfo.tags[tagindex], retlen );
      break;
    default:
      // do nothing
      return 0;
      break;
  }

  if ( *ret == 0 )
    // empty buffer: we didn't have anything to add
    return 0;
  else
    return 1;
}*/

void VGM_SetFrequency(int frequency)			{
	if (vgmSampleRate != frequency)
		vgmSampleRateModified = 1;
	else
		vgmSampleRateModified = 0;
	vgmSampleRate = frequency;
}

